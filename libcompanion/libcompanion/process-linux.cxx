// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only WITH AdditionRef-IW4x-Exception-1.1

#include <libcompanion/process-linux.hxx>

#include <fcntl.h>       // open(), openat(), fcntl()
#include <unistd.h>      // read(), pread(), geteuid(), syscall()
#include <sys/stat.h>    // fstat(), fstatat()
#include <sys/syscall.h> // SYS_getdents64

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>        // snprintf()
#include <cstddef>       // size_t
#include <cstring>       // memcpy()
#include <string_view>

#include <libcompanion/name.hxx>
#include <libcompanion/types.hxx>
#include <libcompanion/endian.hxx>
#include <libcompanion/contract.hxx>
#include <libcompanion/utility-linux.hxx>

using namespace std;

namespace companion
{
  // /proc path formats for format_key() (see name.hxx).
  //
  static constexpr const char* stat_format        ("/proc/%u/stat");
  static constexpr const char* status_format      ("/proc/%u/status");
  static constexpr const char* task_format        ("/proc/%u/task");
  static constexpr const char* environment_format ("/proc/%u/environ");
  static constexpr const char* root_format        ("/proc/%u/root");

  static constexpr size_t proc_path_capacity (32);

  using proc_path = array<char, proc_path_capacity>;

  static_assert (details::key_format_size (stat_format) <=
                 proc_path_capacity);
  static_assert (details::key_format_size (status_format) <=
                 proc_path_capacity);
  static_assert (details::key_format_size (task_format) <=
                 proc_path_capacity);
  static_assert (details::key_format_size (environment_format) <=
                 proc_path_capacity);
  static_assert (details::key_format_size (root_format) <=
                 proc_path_capacity);

  static proc_path
  format_proc (const char* f, process_id p) noexcept
  {
    return details::format_key<char, proc_path_capacity> (f, value (p));
  }

  // Suffix appended to a task id to form its children file path (relative
  // to the task directory).
  //
  static constexpr string_view children_name ("/children");

  // Layout of the entries (struct linux_dirent64) that getdents64() writes
  // into the buffer. Each entry is 8-byte aligned and has the record size
  // and the NUL-terminated name at fixed offsets.
  //
  static constexpr size_t dirent_size_offset (16);
  static constexpr size_t dirent_name_offset (19);
  static constexpr size_t dirent_buffer_size (2048);

  // Maximum number of digits in a (32-bit) task id.
  //
  static constexpr size_t max_task_digits (10);

  // Return true if the call failed with EINTR.
  //
  static bool
  interrupted (ssize_t r) noexcept
  {
    return r == -1 && errno == EINTR;
  }

  // Read the rest of the open file into the buffer and return the size in
  // the process_source convention: the buffer size plus one if the file
  // doesn't fit (we don't read further to find the actual size) and 0 on
  // error.
  //
  static size_t
  read_all (int fd, mutable_bytes b) noexcept
  {
    LIBCOMPANION_PRE (fd != -1);

    size_t n (0);

    while (n != b.size ())
    {
      ssize_t r (read (fd, b.data () + n, b.size () - n));

      if (r > 0)
        n += static_cast<size_t> (r);
      else if (r == 0)
        return n;
      else if (!interrupted (r))
        return 0;
    }

    // The buffer is full. Check whether there is more data.
    //
    uint8_t x;
    ssize_t r;
    while (interrupted (r = read (fd, &x, 1))) ;

    return r > 0 ? n + 1 : n;
  }

  // Open the file and read it (see read_all()).
  //
  static size_t
  read_path (const char* p, mutable_bytes b) noexcept
  {
    auto_fd f (open (p, O_RDONLY | O_CLOEXEC));
    return f.fd != -1 ? read_all (f.fd, b) : 0;
  }

  static size_t
  read_stat (process_id p, mutable_bytes b) noexcept
  {
    return read_path (format_proc (stat_format, p).data (), b);
  }

  static size_t
  read_status (process_id p, mutable_bytes b) noexcept
  {
    return read_path (format_proc (status_format, p).data (), b);
  }

  // Read the children file of the task (tid is its task directory entry
  // name) relative to the task directory.
  //
  static size_t
  read_task_children (int tasks, const char* tid, mutable_bytes b) noexcept
  {
    LIBCOMPANION_PRE (tasks != -1 && tid != nullptr);

    string_view t (tid);

    if (t.empty () || t.size () > max_task_digits)
      return 0;

    char p[max_task_digits + children_name.size () + 1];
    memcpy (p, t.data (), t.size ());
    memcpy (p + t.size (), children_name.data (), children_name.size ());
    p[t.size () + children_name.size ()] = '\0';

    auto_fd f (openat (tasks, p, O_RDONLY | O_CLOEXEC));
    return f.fd != -1 ? read_all (f.fd, b) : 0;
  }

  // Read the children files of all the process's tasks into the buffer one
  // after another.
  //
  // Note that threads can be created or exit while we enumerate them. The
  // result is then the children of a process that is changing, which is no
  // different from the process changing right after we read it.
  //
  static size_t
  read_children (process_id p, mutable_bytes b) noexcept
  {
    auto_fd d (open (format_proc (task_format, p).data (),
                     O_RDONLY | O_DIRECTORY | O_CLOEXEC));

    if (d.fd == -1)
      return 0;

    alignas (8) uint8_t e[dirent_buffer_size];
    size_t n (0);

    for (;;)
    {
      // Use the system call directly (the getdents64() wrapper was only
      // added in glibc 2.30).
      //
      ssize_t r (syscall (SYS_getdents64, d.fd, e, sizeof (e)));

      if (interrupted (r))
        continue;

      if (r <= 0)
        return n;

      for (size_t i (0); i < static_cast<size_t> (r); )
      {
        const uint8_t* x (e + i);
        const char*    t (reinterpret_cast<const char*> (x) +
                          dirent_name_offset);

        i += load16 (x + dirent_size_offset);

        // Skip . and .. (all the other entries are task ids).
        //
        if (t[0] == '.')
          continue;

        n += read_task_children (d.fd, t, b.subspan (n));

        // Stop once the content doesn't fit. Until then everything read so
        // far is in the buffer.
        //
        if (n > b.size ())
          return n;
      }
    }
  }

  // Read the part of the environment that starts at the offset (see
  // process_source).
  //
  static size_t
  read_environment (process_id p, size_t o, mutable_bytes b) noexcept
  {
    auto_fd f (open (format_proc (environment_format, p).data (),
                     O_RDONLY | O_CLOEXEC));

    if (f.fd == -1)
      return 0;

    ssize_t r;
    while (interrupted (r = pread (f.fd,
                                   b.data (),
                                   b.size (),
                                   static_cast<off_t> (o)))) ;

    return r > 0 ? static_cast<size_t> (r) : 0;
  }

  // Wine server lock file path relative to the root directory (without the
  // leading slash), with the device and inode numbers in hexadecimal.
  //
  static constexpr const char* server_format (
    "tmp/.wine-%u/server-%llx-%llx/lock");

  // Room for the longest numbers (32-bit uid, 64-bit device and inode). The
  // format's own length also covers its conversion specifications.
  //
  static constexpr size_t server_path_capacity (
    string_view (server_format).size () + 10 + 16 + 16);

  using server_path = array<char, server_path_capacity>;

  // Return the NUL-terminated lock file path of the server directory.
  //
  static server_path
  format_server (uint32_t uid, uint64_t dev, uint64_t ino) noexcept
  {
    server_path r;
    int n (snprintf (r.data (),
                     r.size (),
                     server_format,
                     uid,
                     static_cast<unsigned long long> (dev),
                     static_cast<unsigned long long> (ino)));

    LIBCOMPANION_ASSERT (n > 0 && static_cast<size_t> (n) < r.size ());
    return r;
  }

  // Look up the wineserver of the prefix (see process_source).
  //
  // Wine names the server directory of a prefix after the prefix
  // directory's device and inode numbers (see init_server_dir() in Wine's
  // dlls/ntdll/unix/server.c). The wineserver holds a write lock on the
  // lock file in this directory while it runs, so the lock's owner is the
  // wineserver.
  //
  // Both paths are as seen by the process, which may be in a container
  // with its own /tmp. So we resolve them in the process's root directory.
  // Note that absolute symlinks in them still resolve in our root. Proton
  // keeps the prefix in the Steam library, which the container shares with
  // the host at the same path, as it does /tmp.
  //
  // @@ Use openat2() with RESOLVE_IN_ROOT (Linux 5.6) once we require it.
  //
  // Wine uses the real user id of the process. If it differs from our
  // effective user id, then the game's record wouldn't be ours to read
  // anyway (see read_record()).
  //
  static process_id
  read_wineserver (process_id p, const char* prefix) noexcept
  {
    LIBCOMPANION_PRE (prefix != nullptr && prefix[0] == '/');

    auto_fd d (open (format_proc (root_format, p).data (),
                     O_PATH | O_DIRECTORY | O_CLOEXEC));

    if (d.fd == -1)
      return process_id (0);

    struct stat s;

    if (fstatat (d.fd, prefix + 1, &s, 0) != 0 || !S_ISDIR (s.st_mode))
      return process_id (0);

    uid_t u (geteuid ());

    auto_fd f (openat (d.fd,
                       format_server (u, s.st_dev, s.st_ino).data (),
                       O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));

    if (f.fd == -1                ||
        fstat (f.fd, &s) != 0     ||
        !S_ISREG (s.st_mode)      ||
        s.st_uid != u)
      return process_id (0);

    // Ask for the owner of a lock that would conflict with a write lock on
    // the whole file. The kernel reports the owner's pid in our pid
    // namespace or 0 if it is not in our namespace.
    //
    struct flock l {};
    l.l_type   = F_WRLCK;
    l.l_whence = SEEK_SET;
    l.l_start  = 0;
    l.l_len    = 0;

    if (fcntl (f.fd, F_GETLK, &l) != 0 || l.l_type == F_UNLCK || l.l_pid <= 0)
      return process_id (0);

    return process_id (static_cast<uint32_t> (l.l_pid));
  }

  // Read the wineserver's record file (see proc_source in the header).
  //
  static size_t
  read_record (process_id p, mutable_bytes b) noexcept
  {
    auto_fd f (open (unix_record_path (p).data (),
                     O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));

    if (f.fd == -1)
      return 0;

    struct stat s;

    if (fstat (f.fd, &s) != 0     ||
        !S_ISREG (s.st_mode)      ||
        s.st_uid != geteuid ())
      return 0;

    return read_all (f.fd, b);
  }

  const process_source proc_source {
    &read_stat,
    &read_status,
    &read_children,
    &read_environment,
    &read_record,
    &read_wineserver};
}
