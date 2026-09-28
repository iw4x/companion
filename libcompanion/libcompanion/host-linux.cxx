// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

// Linux companion host.
//
// The Steam launcher preloads the module (LD_PRELOAD) into the 32-bit Steam
// client (ubuntu12_32/steam) and so also into any other process that it
// starts along the way. The module first removes itself from LD_PRELOAD so
// that the processes these start in turn (games, Proton, the web helper)
// don't load it. In any process other than the Steam client that is all the
// module does.
//
// Inside the Steam client the startup sequence is: publish the starting
// status (see protocol.hxx), wait for steamclient.so to load, find the send
// function in it, install the send hook, and start the record watcher (see
// hook-linux.hxx). Companion is then idle until a game publishes its
// record.
//
// The status file also ensures there is a single companion per user. The
// companion keeps the file locked while it runs, so a second copy (for
// example, in a second Steam client of the same user) fails to lock it and
// does nothing.
//
#include <libcompanion/utility-linux.hxx>

#include <link.h>     // dl_iterate_phdr(), link_map
#include <time.h>     // nanosleep()
#include <dlfcn.h>    // dlopen(), dlinfo()
#include <fcntl.h>    // open()
#include <unistd.h>   // readlink(), pwrite(), ftruncate(), unlink(), get*uid()
#include <sys/file.h> // flock()
#include <sys/stat.h> // fstat()

#include <mutex>
#include <cerrno>
#include <cstdint>
#include <cstddef>     // size_t
#include <cstdlib>     // getenv(), setenv(), unsetenv()
#include <cstring>     // memcpy()
#include <string_view>

#include <libcompanion/elf.hxx>
#include <libcompanion/name.hxx>
#include <libcompanion/types.hxx>
#include <libcompanion/locate.hxx>
#include <libcompanion/status.hxx>
#include <libcompanion/contract.hxx>
#include <libcompanion/protocol.hxx>
#include <libcompanion/hook-linux.hxx>

using namespace std;

namespace companion
{
  // Module file name as the launcher preloads it.
  //
  static constexpr string_view module_file ("libiw4x-steam.so");

  // Steam client executable and the library that contains the send
  // function.
  //
  static constexpr string_view steam_executable     ("steam");
  static constexpr string_view steam_client_library ("steamclient.so");

  // LD_PRELOAD and path buffer capacities. The values we check fit and a
  // longer one is treated as something else.
  //
  static constexpr size_t preload_capacity (4096);
  static constexpr size_t path_capacity    (4096);

  // Size of the image headers that we can read before the image size is
  // known. The dynamic linker always maps at least the first page of an
  // image.
  //
  static constexpr size_t header_size (0x1000);

  // Maximum time to wait for steamclient.so and the polling interval. The
  // Steam client only loads the library after its bootstrapper finishes
  // updating it, which can take a while on a slow connection.
  //
  static constexpr timestamp load_timeout  (30 * 60 * 1000);
  static constexpr timestamp poll_interval (100);

  // Steam client process id as of the module load (see unload()).
  //
  static process_id steam_process;

  // Status file state.
  //
  // Companion thread creates the status file and the exit handler
  // removes it. These can happen in either order since a client that exits
  // right after starting (for example, one that forwards its arguments to an
  // already running client) can exit while we are creating the file or
  // before we even start. So both are serialized with the mutex and the file
  // is not created once the client is exiting.
  //
  static mutex status_mutex;
  static bool  exiting;          // Protected by status_mutex.
  static int   status_file (-1); // Set with status_mutex locked.

  // Return the path leaf.
  //
  static string_view
  file_name (string_view p) noexcept
  {
    size_t i (p.rfind ('/'));
    return i != string_view::npos ? p.substr (i + 1) : p;
  }

  // Rewrite LD_PRELOAD without the entries that load this module. An entry
  // matches by its file name, so both expanded and unexpanded $PLATFORM
  // directories are handled. Either spaces or colons can separate entries.
  //
  // Note that this function is called before main() when there are no other
  // threads that could be reading the environment.
  //
  static void
  remove_preload () noexcept
  {
    const char* v (getenv ("LD_PRELOAD"));

    if (v == nullptr)
      return;

    string_view s (v);

    if (s.size () >= preload_capacity)
    {
      diag ("LD_PRELOAD is too long to remove {} from it", module_file);
      return;
    }

    // The result is never longer than the original: it contains a subset of
    // the entries with at most as many separators.
    //
    char   b[preload_capacity];
    size_t n (0);
    bool   found (false);

    while (!s.empty ())
    {
      size_t      e (s.find_first_of (" :"));
      string_view x (s.substr (0, e));

      s.remove_prefix (e != string_view::npos ? e + 1 : s.size ());

      if (x.empty ())
        continue;

      if (file_name (x) == module_file)
      {
        found = true;
        continue;
      }

      if (n != 0)
        b[n++] = ' ';

      memcpy (b + n, x.data (), x.size ());
      n += x.size ();
    }

    if (!found)
      return;

    LIBCOMPANION_ASSERT (n < preload_capacity);
    b[n] = '\0';

    if (n != 0)
      setenv ("LD_PRELOAD", b, 1);
    else
      unsetenv ("LD_PRELOAD");
  }

  // Return true if this process is the Steam client.
  //
  static bool
  steam_client () noexcept
  {
    char    p[path_capacity];
    ssize_t n (readlink ("/proc/self/exe", p, sizeof (p)));

    return n > 0                              &&
           static_cast<size_t> (n) < sizeof (p) &&
           file_name (string_view (p, static_cast<size_t> (n))) ==
             steam_executable;
  }

  // Publish the state and the hook.
  //
  // The status is written as a whole and a concurrent read can observe a
  // partial write since file writes are not atomic with respect to reads.
  // The state and the hook are the only fields that change and each is an
  // aligned 32-bit value written in one piece. So a reader always sees a
  // valid status with each of these two fields either old or new.
  //
  static void
  publish (companion_state s, uint32_t hook = 0) noexcept
  {
    if (status_file == -1)
      return;

    unix_status x (make_unix_status (s, hook, current_process ()));

    if (pwrite (status_file, &x, sizeof (x), 0) !=
        static_cast<ssize_t> (sizeof (x)))
      diag ("unable to write the status file: {}", last_error ());
  }

  // Create, lock, and initialize the status file. Return false if another
  // companion holds the lock, if the client is exiting, or on failure.
  //
  // Note that the file name includes the real user id (which the game reads
  // from its /proc/self/status) while the file is owned by the effective
  // user id (which creates it).
  //
  static bool
  create_status () noexcept
  {
    lock_guard<mutex> l (status_mutex);

    if (exiting)
      return false;

    unix_path p (unix_status_path (user_id (getuid ())));

    auto_fd f (open (p.data (),
                     O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC,
                     0600));

    if (f.fd == -1)
    {
      diag ("unable to create {}: {}", p.data (), last_error ());
      return false;
    }

    // Anyone can create files in /dev/shm, so an existing file may not be
    // ours (see process-linux.hxx).
    //
    struct stat s;

    if (fstat (f.fd, &s) != 0     ||
        !S_ISREG (s.st_mode)      ||
        s.st_uid != geteuid ())
    {
      diag ("{} is not ours, staying inert", p.data ());
      return false;
    }

    // Process termination of any kind drops the lock.
    //
    if (flock (f.fd, LOCK_EX | LOCK_NB) != 0)
    {
      if (errno == EWOULDBLOCK)
        diag ("another companion is resident, staying inert");
      else
        diag ("unable to lock {}: {}", p.data (), last_error ());

      return false;
    }

    // Any existing content was left by a companion that has exited and can
    // have a different layout.
    //
    if (ftruncate (f.fd, sizeof (unix_status)) != 0)
    {
      diag ("unable to truncate {}: {}", p.data (), last_error ());
      return false;
    }

    status_file = release (f);

    publish (companion_state::starting);
    return true;
  }

  // dl_iterate_phdr() callback that finds the Steam client library by the
  // file name of its path. For a library loaded with dlopen() the dynamic
  // linker uses the path as the name.
  //
  struct library_match
  {
    bool found;
    char path[path_capacity];
  };

  static int
  match_library (dl_phdr_info* i, size_t, void* d) noexcept
  {
    if (i->dlpi_name == nullptr)
      return 0;

    string_view n (i->dlpi_name);

    if (file_name (n) != steam_client_library || n.size () >= path_capacity)
      return 0;

    library_match& m (*static_cast<library_match*> (d));

    memcpy (m.path, n.data (), n.size ());
    m.path[n.size ()] = '\0';
    m.found = true;

    return 1; // Stop.
  }

  // Return the Steam client library if it is loaded, with its reference
  // count incremented, or NULL if it is not loaded yet.
  //
  // The library has no DT_SONAME and the dynamic linker only knows it by the
  // path it was loaded from. A dlopen() with the bare name would search for
  // a different library. So we first find the path and then call dlopen()
  // with RTLD_NOLOAD, which achieves three things. It verifies that the
  // library is still loaded. It waits for the library loading to complete
  // if it is still in progress (dlopen() holds the lock throughout,
  // including relocation and initialization). And it adds a reference that
  // we never release, which keeps the library loaded under the hook.
  //
  static const link_map*
  find_client_library () noexcept
  {
    library_match m;
    m.found = false;

    dl_iterate_phdr (&match_library, &m);

    if (!m.found)
      return nullptr;

    void* h (dlopen (m.path, RTLD_NOW | RTLD_NOLOAD));

    if (h == nullptr)
      return nullptr;

    link_map* l;
    if (dlinfo (h, RTLD_DI_LINKMAP, &l) != 0)
      return nullptr;

    return l;
  }

  // Poll for the Steam client library until it is loaded or the timeout
  // expires.
  //
  static const link_map*
  wait_client_library () noexcept
  {
    for (timestamp w (0);; w += poll_interval)
    {
      if (const link_map* l = find_client_library ())
        return l;

      if (w >= load_timeout)
        return nullptr;

      timespec d {0, static_cast<long> (poll_interval.count ()) * 1000000};
      while (nanosleep (&d, &d) != 0 && errno == EINTR) ;
    }
  }

  // Parse the loaded Steam client library. On failure, issue diagnostics and
  // return false.
  //
  static bool
  parse (const uint8_t* base, elf_image& x) noexcept
  {
    uint32_t   n (elf_image_size (bytes (base, header_size)));
    elf_outcome o (parse_elf (bytes (base, n), x));

    if (o != elf_outcome::valid)
    {
      diag ("unable to parse steamclient.so: {}", elf_outcome_name (o));
      return false;
    }

    return true;
  }

  // Locate the send function in the Steam client library. Return its image
  // address or issue diagnostics and return 0 if the build is not
  // recognized.
  //
  static uint32_t
  locate (const elf_image& x) noexcept
  {
    locate_result r (locate_send_frame (x));

    if (r.outcome != locate_outcome::found)
    {
      diag ("unable to locate the send function: {} "
            "({} references, {} candidates)",
            locate_outcome_name (r.outcome),
            r.references,
            r.candidates);
      return 0;
    }

    diag ("send function at steamclient.so+{:#x} ({} references)",
          r.rva,
          r.references);

    return r.rva;
  }

  // Companion thread.
  //
  static void*
  run (void*) noexcept
  {
    if (!create_status ())
      return nullptr;

    const link_map* l (wait_client_library ());

    if (l == nullptr)
    {
      diag ("steamclient.so was not loaded within {} minutes",
            load_timeout.count () / 60000);
      publish (companion_state::unsupported);
      return nullptr;
    }

    // The first loadable segment of a shared library maps the start of the
    // file to address 0, so the load bias is the image base (see elf.hxx).
    //
    elf_image x;
    uint32_t  r (0);

    if (!parse (reinterpret_cast<const uint8_t*> (l->l_addr), x) ||
        (r = locate (x)) == 0)
    {
      publish (companion_state::unsupported);
      return nullptr;
    }

    if (!install_send_hook (x, r))
    {
      publish (companion_state::failed);
      return nullptr;
    }

    start_record_watcher ();

    publish (companion_state::active, r);
    diag ("active (build {})", companion_build);
    return nullptr;
  }

  // Module constructor. The dynamic linker calls it before main(), which
  // is no place for real work, so Companion runs on its own thread.
  //
  [[gnu::constructor]] static void
  load () noexcept
  {
    remove_preload ();

    if (!steam_client ())
      return;

    steam_process = current_process ();

    if (!start_thread (&run, nullptr))
      diag ("unable to start Companion: {}", last_error ());
  }

  // Module destructor. Remove the status file and prevent its creation if
  // that hasn't happened yet.
  //
  // This only applies to the Steam client itself. The destructor also runs
  // in a forked child of the client that exits without exec'ing. Such a
  // child must not even lock the mutex since Companion thread may have
  // held it at the time of the fork and nothing would then unlock it.
  //
  // Note that a crashed client leaves the file behind. The next companion
  // takes it over and meanwhile the game recognizes the status as stale by
  // its unix_status::steam_pid.
  //
  [[gnu::destructor]] static void
  unload () noexcept
  {
    if (steam_process != current_process ())
      return;

    lock_guard<mutex> l (status_mutex);
    exiting = true;

    if (status_file != -1)
      unlink (unix_status_path (user_id (getuid ())).data ());
  }
}
