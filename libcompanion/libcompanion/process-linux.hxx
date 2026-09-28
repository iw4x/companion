// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <libcompanion/process.hxx>

namespace companion
{
  // Linux process source (see process.hxx) that reads /proc and the record
  // files in /dev/shm.
  //
  // It runs on Steam's network thread, the same as the search, and doesn't
  // allocate: files are read into the caller's buffer and directories are
  // listed into a stack buffer.
  //
  // A record file is opened without following symlinks and without blocking
  // and is only read if it is a regular file owned by the current user.
  // Anyone can create files in /dev/shm, so anything else with a record
  // name was created by someone else (for example, a symlink to one of the
  // user's files or a FIFO that is never written to).
  //
  extern const process_source proc_source;
}
