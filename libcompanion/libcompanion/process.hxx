// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <cstddef> // size_t

#include <libcompanion/types.hxx>
#include <libcompanion/registration.hxx>

namespace companion
{
  // Linux game registration lookup through the process tree.
  //
  // Steam reports a game with the pid of the process it tracks for it
  // (reaper, pressure-vessel, one of Proton's Wine processes, etc). The
  // game's record is keyed by the pid of its prefix's wineserver as seen in
  // the Steam Runtime container (see protocol.hxx). So we search the
  // reported process and its descendants breadth-first. For each process we
  // try the record under each of its pids, one per pid namespace the
  // process is in (the NSpid line of /proc/<pid>/status). The process start
  // time, which is the same in all the namespaces, distinguishes its record
  // from a stale one of an earlier process with the same pid.
  //
  // The search and the parsing of the /proc file formats only depend on
  // the file contents. The caller provides the reading functions (see
  // process_source): the Linux platform layer in Companion and a
  // process tree description in the tests.
  //
  // Every GamesPlayed report triggers a search on Steam's network thread.
  // To keep that thread responsive the search visits a bounded number of
  // processes (see search_capacity) and uses no dynamic memory.
  //

  // Maximum number of processes to search. A game under Proton consists of
  // a few dozen processes, so a larger tree is something else (for example,
  // a whole desktop session).
  //
  inline constexpr std::size_t search_capacity (256);

  // Maximum number of the Steam client's ancestors to check (see
  // search_outcome).
  //
  inline constexpr std::size_t ancestor_capacity (64);

  // Maximum pid namespace depth (the kernel's MAX_PID_NS_LEVEL).
  //
  inline constexpr std::size_t namespace_capacity (32);

  // Read buffer size. It is sufficient for the stat and status files and
  // for the children lists of about 500 processes.
  //
  inline constexpr std::size_t proc_file_capacity (4096);

  // Reading functions for /proc (as seen by the Steam client) and for the
  // record files.
  //
  // Each function reads the file of the specified process into the buffer
  // and returns the file size. It returns 0 if the file doesn't exist (for
  // example, the process has exited). If the file doesn't fit, then the
  // buffer contains its beginning and the returned size exceeds the buffer
  // size.
  //
  struct process_source
  {
    // Process stat and status files in /proc.
    //
    std::size_t (*stat)   (process_id, mutable_bytes) noexcept;
    std::size_t (*status) (process_id, mutable_bytes) noexcept;

    // Concatenated /proc/<pid>/task/<tid>/children of all the process's
    // tasks.
    //
    std::size_t (*children) (process_id, mutable_bytes) noexcept;

    // Wineserver's record file (see unix_record_path()). The file must be a
    // regular file owned by the current user.
    //
    std::size_t (*record) (process_id wineserver, mutable_bytes) noexcept;
  };

  enum class search_outcome: std::uint8_t
  {
    found,
    related,  // Process is the Steam client or its ancestor (its tree
              // contains every game, so it is not searched).
    none,     // No record in the process tree.
    overflow  // No record in the first search_capacity processes.
  };

  // Return the outcome name (for example, related).
  //
  const char*
  search_outcome_name (search_outcome) noexcept;

  // Search the tree of the specified process for its game's registration.
  // Here self is the Steam client process.
  //
  search_outcome
  find_registration (const process_source&,
                     process_id self,
                     process_id,
                     registration&) noexcept;
}
