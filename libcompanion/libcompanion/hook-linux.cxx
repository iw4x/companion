// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/hook-linux.hxx>

#include <time.h>        // clock_gettime()
#include <unistd.h>      // read(), syscall()
#include <sys/mman.h>    // mmap(), mprotect(), munmap()
#include <sys/inotify.h>
#include <sys/syscall.h> // SYS_gettid

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstddef>       // size_t
#include <cstring>       // memcpy(), strnlen()
#include <string_view>

#include <libcompanion/hook.hxx>
#include <libcompanion/types.hxx>
#include <libcompanion/detour.hxx>
#include <libcompanion/resend.hxx>
#include <libcompanion/process.hxx>
#include <libcompanion/contract.hxx>
#include <libcompanion/protocol.hxx>
#include <libcompanion/registration.hxx>
#include <libcompanion/utility-linux.hxx>
#include <libcompanion/process-linux.hxx>

using namespace std;

namespace companion
{
  // The module is loaded into the i386 Steam client, so a pointer is an
  // i386_address (see detour.hxx).
  //
  static_assert (sizeof (void*) == sizeof (i386_address),
                 "the Linux companion module is i386 only");

  static i386_address
  address (uintptr_t a) noexcept
  {
    return i386_address (static_cast<uint32_t> (a));
  }

  static i386_address
  address (const void* p) noexcept
  {
    return address (reinterpret_cast<uintptr_t> (p));
  }

  // i386 page size (mprotect() granularity).
  //
  static constexpr uintptr_t page_size (0x1000);

  // The jump is written with a single 8-byte compare-exchange (see
  // write_jump()). On i686 this is lock cmpxchg8b, which requires neither a
  // lock nor libatomic.
  //
  static_assert (atomic_ref<uint64_t>::is_always_lock_free);
  static_assert (atomic_ref<uint64_t>::required_alignment == 8);

  static constexpr uintptr_t quadword_size (sizeof (uint64_t));

  // Trampoline to the original function, the process-wide resend state, and
  // the Steam client process id, which roots the process tree searches (see
  // process.hxx).
  //
  static send_frame_function original_function;
  static resend_state        pending;
  static process_id          self;

  // Linux implementation of send_platform.
  //
  struct linux_platform
  {
    static constexpr string_view diag_prefix {companion::diag_prefix};

    static bool
    original (void* c, uint32_t op, const uint8_t* d, uint32_t n)
    {
      return original_function (c, op, d, n);
    }

    // Search the reported process's tree for a record. The game keeps the
    // record file open while it is registered and the record includes the
    // process start time, so a record that is found is live.
    //
    static bool
    lookup (process_id p, registration& r) noexcept
    {
      search_outcome o (find_registration (proc_source, self, p, r));

      // Most reported processes are other games, so only issue diagnostics
      // for the outcomes that suggest a problem with one of ours.
      //
      if (o == search_outcome::related || o == search_outcome::overflow)
        diag ("no record for process {} ({})",
              value (p),
              search_outcome_name (o));

      return o == search_outcome::found;
    }

    static thread_id
    current_thread () noexcept
    {
      // Call the system call directly (the gettid() wrapper was only added
      // in glibc 2.30).
      //
      return thread_id (static_cast<uint64_t> (syscall (SYS_gettid)));
    }

    static timestamp
    now () noexcept
    {
      timespec t;
      clock_gettime (CLOCK_MONOTONIC, &t);

      return timestamp (static_cast<timestamp::rep> (t.tv_sec) * 1000 +
                        t.tv_nsec / 1000000);
    }

    static void
    write_diag (const diag_line& l) noexcept
    {
      companion::write_diag (l);
    }
  };

  static_assert (send_platform<linux_platform>);

  // Detour function. It is called by Steam, so it doesn't throw.
  //
  // GCC assumes the 16-byte stack alignment of the i386 System V ABI (for
  // example, for SSE spills). We don't rely on the Steam client code to
  // preserve it, so the detour realigns the stack on entry.
  //
  [[gnu::force_align_arg_pointer]] static bool
  detour (void* c, uint32_t op, const uint8_t* d, uint32_t n) noexcept
  {
    return send_frame<linux_platform> (pending, c, op, d, n);
  }

  // Overwrite the start of the function at p with the jump.
  //
  // Other threads can be entering the function concurrently, so they must
  // observe either the old bytes or the complete jump. To achieve this we
  // splice the jump into the aligned quadword that contains it and store the
  // quadword atomically. This requires the jump to fit into one quadword,
  // which is always the case for the 16-byte aligned functions that the
  // linkers produce.
  //
  // The store is a compare-exchange with the value read just before. If it
  // fails, then someone else is patching the function and the trampoline,
  // which was built from the old bytes, is no longer valid.
  //
  static bool
  write_jump (uint8_t* p, const uint8_t (&j)[jump_size]) noexcept
  {
    uintptr_t a (reinterpret_cast<uintptr_t> (p));
    uintptr_t q (a & ~(quadword_size - 1));
    uintptr_t s (a - q);

    if (s + jump_size > quadword_size)
    {
      diag ("unable to hook the send function: it is not aligned");
      return false;
    }

    // Since the quadword is aligned, it is within a single page.
    //
    void* g (reinterpret_cast<void*> (q & ~(page_size - 1)));

    if (mprotect (g, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
    {
      diag ("unable to make the send function writable: {}", last_error ());
      return false;
    }

    atomic_ref<uint64_t> w (*reinterpret_cast<uint64_t*> (q));
    uint64_t             e (w.load (memory_order_relaxed));
    uint64_t             v (e);

    // The CPU is little-endian, so the quadword's in-memory bytes are the
    // code bytes and the jump can be copied at its byte offset.
    //
    memcpy (reinterpret_cast<uint8_t*> (&v) + s, j, jump_size);

    bool r (w.compare_exchange_strong (e, v, memory_order_seq_cst));

    // Restore the code segment protection (see install_send_hook()). If this
    // fails, then the hook remains in place with a writable page.
    //
    if (mprotect (g, page_size, PROT_READ | PROT_EXEC) != 0)
      diag ("unable to restore the send function's protection: {}",
            last_error ());

    if (!r)
      diag ("unable to hook the send function: it changed while hooking");

    return r;
  }

  bool
  install_send_hook (const elf_image& x, uint32_t f) noexcept
  {
    elf_function       fn (find_function (x, f));
    const elf_segment* sg (find_segment (x, fn.address, fn.size));

    LIBCOMPANION_PRE (f != 0 && fn.address == f);
    LIBCOMPANION_PRE (sg != nullptr && sg->executable && !sg->writable);

    // Allocate a separate page for the trampoline. It is writable while we
    // fill it and executable afterwards, never both at once.
    //
    void* m (mmap (nullptr,
                   page_size,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1,
                   0));

    if (m == MAP_FAILED)
    {
      diag ("unable to allocate the trampoline: {}", last_error ());
      return false;
    }

    // The image bytes are the live code, so the image base is the load
    // address and p points to the function itself.
    //
    const uint8_t* b (x.data.data ());
    uint8_t*       p (const_cast<uint8_t*> (b) + f);

    trampoline     t;
    detour_outcome o (build_trampoline (x,
                                        fn,
                                        address (b),
                                        address (m),
                                        t));

    if (o != detour_outcome::valid)
    {
      diag ("unable to relocate the send function: {}",
            detour_outcome_name (o));
      munmap (m, page_size);
      return false;
    }

    memcpy (m, t.code, t.size);

    if (mprotect (m, page_size, PROT_READ | PROT_EXEC) != 0)
    {
      diag ("unable to make the trampoline executable: {}", last_error ());
      munmap (m, page_size);
      return false;
    }

    // Initialize everything the detour uses before publishing the jump. The
    // compare-exchange in write_jump() is a full barrier, so plain stores
    // are sufficient.
    //
    original_function = reinterpret_cast<send_frame_function> (m);
    self = current_process ();

    uint8_t j[jump_size];
    encode_jump (address (p),
                 address (reinterpret_cast<uintptr_t> (&detour)),
                 j);

    if (!write_jump (p, j))
    {
      munmap (m, page_size);
      return false;
    }

    diag ("hooked the send function ({} bytes relocated)", t.replaced);
    return true;
  }

  // Record directory events that indicate a record change.
  //
  // The game writes its record in one go and keeps the file open while it
  // runs. It may later update the app id in place and it removes the file on
  // exit. So we watch for writes (IN_MODIFY, which includes truncation) and
  // removals (IN_DELETE). IN_CLOSE_WRITE would only be delivered on game
  // exit. We also watch for renames (IN_MOVED_*), which cover a record that
  // is moved into or out of place by some other means.
  //
  // IN_CREATE is not needed since a newly created record is empty and the
  // write that follows is reported with IN_MODIFY.
  //
  static constexpr uint32_t record_events (IN_MODIFY     |
                                           IN_DELETE     |
                                           IN_MOVED_FROM |
                                           IN_MOVED_TO);

  // Return true if the inotify events include a change to a record file. A
  // queue overflow is treated as a change since the dropped events may have
  // included one.
  //
  static bool
  record_changed (bytes b) noexcept
  {
    static constexpr string_view prefix (unix_record_prefix);

    // The kernel doesn't split events across reads. Nevertheless, stop at an
    // event that extends past the buffer.
    //
    for (size_t i (0); b.size () - i >= sizeof (inotify_event); )
    {
      inotify_event e;
      memcpy (&e, b.data () + i, sizeof (e));

      const char* n (reinterpret_cast<const char*> (b.data ()) +
                     i + sizeof (e));

      if ((e.mask & IN_Q_OVERFLOW) != 0)
        return true;

      i += sizeof (e);

      if (e.len > b.size () - i)
        break;

      i += e.len;

      // The name is NUL-padded to e.len. An event on the directory itself
      // has no name.
      //
      if (e.len != 0 &&
          string_view (n, strnlen (n, e.len)).starts_with (prefix))
        return true;
    }

    return false;
  }

  static void*
  watch_records (void* a) noexcept
  {
    int fd (static_cast<int> (reinterpret_cast<intptr_t> (a)));

    // Room for a few dozen events. Each read() returns as many complete
    // events as fit.
    //
    alignas (inotify_event) uint8_t b[4096];

    for (;;)
    {
      ssize_t n (read (fd, b, sizeof (b)));

      if (n == -1 && errno == EINTR)
        continue;

      if (n <= 0)
        break;

      if (record_changed (bytes (b, static_cast<size_t> (n))) &&
          note_change (pending))
        diag ("record changed");
    }

    diag ("record watcher stopped: {}", last_error ());
    return nullptr;
  }

  bool
  start_record_watcher () noexcept
  {
    auto_fd fd (inotify_init1 (IN_CLOEXEC));

    if (fd.fd == -1)
    {
      diag ("unable to watch the records: {}", last_error ());
      return false;
    }

    if (inotify_add_watch (fd.fd,
                           unix_record_directory,
                           record_events | IN_ONLYDIR) == -1)
    {
      diag ("unable to watch {}: {}", unix_record_directory, last_error ());
      return false;
    }

    void* a (reinterpret_cast<void*> (static_cast<intptr_t> (fd.fd)));

    if (!start_thread (&watch_records, a))
    {
      diag ("unable to start the record watcher: {}", last_error ());
      return false;
    }

    // The watcher thread reads the descriptor until the Steam client exits,
    // so keep it open.
    //
    release (fd);
    return true;
  }
}
