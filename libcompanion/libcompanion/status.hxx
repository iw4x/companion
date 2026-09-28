// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

#include <libcompanion/types.hxx>
#include <libcompanion/protocol.hxx>

namespace companion
{
  // Decoded companion status (see protocol.hxx).
  //
  // The hook is the send function offset from the start of the Steam client
  // module (steamclient64.dll or steamclient.so) or 0 if the hook is not
  // installed. It is informational and no protocol logic depends on it.
  //
  struct companion_status
  {
    companion_state state;
    std::uint32_t   build;
    std::uint32_t   hook;
    process_id      steam;
  };

  // Return true if the state is one of the known companion_state values.
  //
  constexpr bool
  valid (companion_state) noexcept;

  // Return the fully initialized status structures that the companion
  // publishes.
  //
  // On Windows the state field is the publication point: the companion
  // stores the other fields first and then stores the state with release
  // ordering. A reader loads the state with acquire ordering before copying
  // the rest.
  //
  status
  make_status (companion_state, std::uint32_t hook) noexcept;

  unix_status
  make_unix_status (companion_state,
                    std::uint32_t hook,
                    process_id steam) noexcept;

  // Status decoding outcome. The failures are listed in the order of the
  // checks.
  //
  enum class status_outcome: std::uint8_t
  {
    valid,
    truncated, // Too short (for a file, any size other than exact).
    magic,     // Not published yet or not a status.
    version,   // Unknown layout version.
    size,      // Size doesn't match the version's layout.
    state      // Unknown state.
  };

  // Decode the Windows status of the specified Steam process. As with
  // decode_record(), the bytes are a copy made after the acquire load of
  // the state and can extend past the status.
  //
  // The result is only changed if the outcome is valid.
  //
  status_outcome
  decode_status (bytes, process_id steam, companion_status&) noexcept;

  // Decode the Linux status from the status file content.
  //
  // The result is only changed if the outcome is valid.
  //
  status_outcome
  decode_unix_status (bytes, companion_status&) noexcept;
}

#include <libcompanion/status.ixx>
