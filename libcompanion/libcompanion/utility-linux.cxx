// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/utility-linux.hxx>

#include <signal.h>  // sigfillset(), pthread_sigmask()
#include <unistd.h>  // write(), close(), getpid()
#include <pthread.h>

#include <cerrno>
#include <cstdint>

#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  void
  write_diag (const diag_line& l) noexcept
  {
    // The line is much shorter than PIPE_BUF, so a single write() is atomic
    // even if stderr is a pipe that the Steam client also writes to. And an
    // interrupted write writes nothing, so it can simply be retried.
    //
    while (write (STDERR_FILENO, l.data, l.size) == -1 && errno == EINTR) ;
  }

  error_number
  last_error () noexcept
  {
    return error_number (errno);
  }

  process_id
  current_process () noexcept
  {
    return process_id (static_cast<uint32_t> (getpid ()));
  }

  auto_fd::
  ~auto_fd () noexcept
  {
    if (fd != -1)
      close (fd);
  }

  int
  release (auto_fd& f) noexcept
  {
    int r (f.fd);
    f.fd = -1;
    return r;
  }

  bool
  start_thread (void* (*f) (void*), void* a) noexcept
  {
    LIBCOMPANION_PRE (f != nullptr);

    // A new thread inherits the signal mask of the creating thread. So block
    // all the signals while creating the thread and then restore the mask.
    // Note that the synchronous fault signals (SIGSEGV, etc) are delivered
    // to the faulting thread even when blocked.
    //
    sigset_t all;
    sigset_t old;
    sigfillset (&all);

    int r (pthread_sigmask (SIG_SETMASK, &all, &old));

    if (r == 0)
    {
      pthread_t t;
      r = pthread_create (&t, nullptr, f, a);

      if (r == 0)
        pthread_detach (t);

      pthread_sigmask (SIG_SETMASK, &old, nullptr);
    }

    if (r != 0)
    {
      errno = r;
      return false;
    }

    return true;
  }
}
