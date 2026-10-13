// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only WITH AdditionRef-IW4x-Exception-1.1

#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <filesystem>
#include <string_view>

#include <libcompanion/types.hxx>

namespace companion
{
  // Linux Steam client launch.
  //
  // The Steam client only loads the module at startup and only if it is
  // preloaded (see preload.hxx). So the client is started with the module
  // in LD_PRELOAD and, if it is already running without Companion, it is
  // first shut down. The functions below implement these steps, which the
  // companion program performs in order: check that the module is
  // installed, see if Companion is already active, shut down a running
  // client and wait for it to exit, make the module directory preloadable,
  // and execute the Steam command with the module preloaded.
  //
  // The functions that access the system take the locations they inspect
  // (the /proc directory, the status file) as arguments, which lets the
  // tests substitute their own. They report system errors by throwing
  // std::system_error and issue no diagnostics.
  //
  // These functions are only available on Linux.
  //

  // Return true if the module directory contains the module, that is, the
  // preload_platform/preload_file regular file.
  //
  bool
  module_installed (const std::filesystem::path& directory);

  // Return true if the process with the effective user id holds the lock
  // of the status file, that is, if Companion is active in a running Steam
  // client of this user (see host-linux.cxx).
  //
  // Anyone can create files in /dev/shm, so only a regular file owned by
  // the user counts. A missing file means Companion is not active.
  //
  // Throw std::system_error if unable to inspect the file.
  //
  bool
  companion_active (const std::filesystem::path& status);

  // Name of the Steam client's processes. These are the client itself as
  // well as the steam script while it starts the client.
  //
  inline constexpr std::string_view steam_process ("steam");

  // Return true if a process named steam_process whose effective user id is
  // the specified one is running according to the /proc directory.
  //
  // Note that a process can exit while we inspect it, so we skip the ones
  // that we fail to inspect.
  //
  // Throw std::system_error if unable to read the /proc directory.
  //
  bool
  steam_running (const std::filesystem::path& proc, user_id);

  // Ask the running Steam client to exit by running the Steam command with
  // the -shutdown option and wait for the command to finish. Its output is
  // discarded.
  //
  // Throw std::system_error if unable to run the command.
  //
  void
  request_steam_shutdown (const std::string& steam);

  // Wait for the user's Steam processes to exit (see steam_running()),
  // checking every interval. Return false if they are still running after
  // the timeout.
  //
  // Throw std::system_error if unable to read the /proc directory.
  //
  bool
  wait_steam_exit (const std::filesystem::path& proc,
                   user_id,
                   std::chrono::milliseconds timeout,
                   std::chrono::milliseconds interval);

  // Return true if the dynamic linker can preload from the directory, that
  // is, if its path contains neither spaces nor colons, at which the
  // dynamic linker splits LD_PRELOAD with no way to escape them.
  //
  bool
  preloadable (const std::filesystem::path& directory);

  // Make the module directory preloadable through a symbolic link to it in
  // the runtime directory, named preload_directory, and return the link.
  // An existing link is replaced atomically. The runtime directory should
  // be private to the user (normally XDG_RUNTIME_DIR) and must itself be
  // preloadable.
  //
  // Throw std::system_error if unable to create the link.
  //
  std::filesystem::path
  link_module_directory (const std::filesystem::path& directory,
                         const std::filesystem::path& runtime);

  // Return the LD_PRELOAD value that preloads the module from the
  // directory in front of the existing value, if any. The directory must
  // be preloadable. Note that $PLATFORM is for the dynamic linker to
  // expand.
  //
  std::string
  preload_value (const std::filesystem::path& directory,
                 std::string_view existing);

  // Execute the Steam command with the arguments in place of the calling
  // process, with LD_PRELOAD set to the value unless it is empty. The
  // command is searched for in PATH.
  //
  // Throw std::system_error if unable to execute the command.
  //
  [[noreturn]] void
  execute_steam (const std::string& steam,
                 const std::vector<std::string>& arguments,
                 const std::string& preload);
}
