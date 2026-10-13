// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only WITH AdditionRef-IW4x-Exception-1.1

#include <libcompanion/launch.hxx>

#include <fcntl.h>    // open()
#include <unistd.h>   // pipe2(), fork(), dup2(), execvp(), symlink(), getpid()
#include <sys/file.h> // flock()
#include <sys/stat.h> // stat(), fstat()
#include <sys/wait.h> // waitpid()

#include <thread>       // this_thread::sleep_for()
#include <cerrno>
#include <cstdio>       // rename()
#include <cstdlib>      // setenv()
#include <fstream>
#include <algorithm>    // ranges::all_of()
#include <system_error> // system_error, generic_category()

#include <libcompanion/preload.hxx>
#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  namespace
  {
    // RAII type for file descriptors. A negative descriptor is none.
    //
    class auto_fd
    {
    public:
      explicit
      auto_fd (int fd) noexcept: fd_ (fd) {}

      ~auto_fd () noexcept {if (fd_ >= 0) close (fd_);}

      auto_fd (const auto_fd&) = delete;
      auto_fd& operator= (const auto_fd&) = delete;

      int
      get () const noexcept {return fd_;}

    private:
      int fd_;
    };

    // Throw std::system_error for the current errno value.
    //
    [[noreturn]] void
    throw_errno ()
    {
      throw system_error (errno, generic_category ());
    }
  }

  bool
  module_installed (const filesystem::path& d)
  {
    error_code ec;
    return filesystem::is_regular_file (d / preload_platform / preload_file,
                                        ec);
  }

  bool
  companion_active (const filesystem::path& p)
  {
    // Anyone can create files in /dev/shm, so we only consider a regular
    // file that we own. In particular, we don't follow a symbolic link
    // (ELOOP) and don't block on a FIFO.
    //
    auto_fd f (open (p.c_str (),
                     O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));

    if (f.get () == -1)
    {
      if (errno == ENOENT || errno == ELOOP)
        return false;

      throw_errno ();
    }

    struct stat s;
    if (fstat (f.get (), &s) != 0)
      throw_errno ();

    if (!S_ISREG (s.st_mode) || s.st_uid != geteuid ())
      return false;

    // Companion holds an exclusive lock while it runs, so a shared lock is
    // only refused while it does. If we get the lock, then closing the file
    // releases it.
    //
    if (flock (f.get (), LOCK_SH | LOCK_NB) == 0)
      return false;

    if (errno == EWOULDBLOCK)
      return true;

    throw_errno ();
  }

  bool
  steam_running (const filesystem::path& proc, user_id u)
  {
    error_code ec;
    filesystem::directory_iterator i (proc, ec);

    if (ec)
      throw system_error (ec);

    for (const filesystem::directory_entry& e: i)
    {
      // Only the numeric entries are processes.
      //
      const string n (e.path ().filename ().string ());

      if (n.empty () ||
          !ranges::all_of (n, [] (char c) {return c >= '0' && c <= '9';}))
        continue;

      // The process directory is owned by the process's effective user.
      //
      struct stat s;
      if (stat (e.path ().c_str (), &s) != 0 || s.st_uid != value (u))
        continue;

      ifstream f (e.path () / "comm");
      string   c;

      if (getline (f, c) && c == steam_process)
        return true;
    }

    return false;
  }

  void
  request_steam_shutdown (const string& steam)
  {
    // The child reports the exec() failure through the pipe, which the
    // successful exec() closes (O_CLOEXEC). So reading it tells us whether
    // the command started.
    //
    int fds[2];
    if (pipe2 (fds, O_CLOEXEC) != 0)
      throw_errno ();

    auto_fd in (fds[0]);
    pid_t   p;
    {
      auto_fd out (fds[1]);

      if ((p = fork ()) == -1)
        throw_errno ();

      if (p == 0)
      {
        // Note that only async-signal-safe functions may be called here.
        //
        if (int n = open ("/dev/null", O_WRONLY); n != -1)
        {
          dup2 (n, STDOUT_FILENO);
          dup2 (n, STDERR_FILENO);
        }

        char o[] = "-shutdown";
        char* const args[] {const_cast<char*> (steam.c_str ()), o, nullptr};

        execvp (args[0], args);

        // There is nothing we can do if the write fails, in which case the
        // parent takes the command as started.
        //
        int e (errno);
        [[maybe_unused]] ssize_t r (write (out.get (), &e, sizeof (e)));

        _exit (127);
      }
    }

    int     e;
    ssize_t n;
    while ((n = read (in.get (), &e, sizeof (e))) == -1 && errno == EINTR) ;

    int s;
    while (waitpid (p, &s, 0) == -1)
    {
      if (errno != EINTR)
        throw_errno ();
    }

    if (n == static_cast<ssize_t> (sizeof (e)))
      throw system_error (e, generic_category ());
  }

  bool
  wait_steam_exit (const filesystem::path& proc,
                   user_id u,
                   chrono::milliseconds timeout,
                   chrono::milliseconds interval)
  {
    for (auto d (chrono::steady_clock::now () + timeout);
         steam_running (proc, u); )
    {
      if (chrono::steady_clock::now () >= d)
        return false;

      this_thread::sleep_for (interval);
    }

    return true;
  }

  bool
  preloadable (const filesystem::path& d)
  {
    return d.native ().find_first_of (" :") == string::npos;
  }

  filesystem::path
  link_module_directory (const filesystem::path& d,
                         const filesystem::path& runtime)
  {
    LIBCOMPANION_PRE (preloadable (runtime));

    // Replace the existing link atomically: create the new one under a
    // temporary name and rename it over the existing one. Note that
    // rename() fails instead of replacing a directory.
    //
    filesystem::path l (runtime / preload_directory);
    filesystem::path t (l);
    t += '.';
    t += to_string (getpid ());

    unlink (t.c_str ()); // Left behind by an earlier process, if any.

    if (symlink (d.c_str (), t.c_str ()) != 0)
      throw_errno ();

    if (rename (t.c_str (), l.c_str ()) != 0)
    {
      int e (errno);
      unlink (t.c_str ());
      throw system_error (e, generic_category ());
    }

    return l;
  }

  string
  preload_value (const filesystem::path& d, string_view existing)
  {
    LIBCOMPANION_PRE (preloadable (d));

    string r (d.native ());

    if (r.empty () || r.back () != '/')
      r += '/';

    r += "$PLATFORM/";
    r += preload_file;

    if (!existing.empty ())
    {
      r += ' ';
      r += existing;
    }

    return r;
  }

  void
  execute_steam (const string& steam,
                 const vector<string>& arguments,
                 const string& preload)
  {
    if (!preload.empty () &&
        setenv ("LD_PRELOAD", preload.c_str (), 1 /* overwrite */) != 0)
      throw_errno ();

    vector<char*> args;
    args.reserve (arguments.size () + 2);

    args.push_back (const_cast<char*> (steam.c_str ()));

    for (const string& a: arguments)
      args.push_back (const_cast<char*> (a.c_str ()));

    args.push_back (nullptr);

    execvp (args[0], args.data ());
    throw_errno ();
  }
}
