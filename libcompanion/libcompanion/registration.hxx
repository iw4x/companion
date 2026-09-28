// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string_view>
#include <cstdint>

#include <libcompanion/types.hxx>
#include <libcompanion/protocol.hxx>

namespace companion
{
  // Decoded game registration: the app id to report and the display name
  // (without the terminating NUL). The GamesPlayed rewrite uses it to
  // replace the reported game (see games-played.hxx).
  //
  struct registration
  {
    app_id       app;
    std::uint8_t extra_info_size;
    char         extra_info[extra_info_capacity - 1];
  };

  static_assert (sizeof (registration) == 4 + extra_info_capacity);

  // Return the display name.
  //
  constexpr std::string_view
  extra_info (const registration&) noexcept;

  // Return true if the display name can be sent to Steam unchanged. It must
  // be non-empty, fit into k_cchGameExtraInfoMax with the NUL, be valid
  // UTF-8, and contain no control characters (C0, DEL, or C1).
  //
  bool
  valid_extra_info (std::string_view) noexcept;

  // Return true if the app id and the display name are both valid.
  //
  bool
  valid (const registration&) noexcept;

  // Record decoding outcome. The failures are listed in the order of the
  // checks and each one identifies the first check that failed.
  //
  // The record is written by the game (possibly a different version of it),
  // so these outcomes are expected conditions.
  //
  enum class registration_outcome: std::uint8_t
  {
    valid,
    truncated,  // Too short (for a file, any size other than exact).
    magic,      // Not published yet or not a record.
    version,    // Unknown layout version.
    size,       // Size doesn't match the version's layout.
    process,    // Record of a different process.
    start_time, // Stale record of an earlier process with the same pid.
    app,        // App id that doesn't fit into a CGameID.
    extra_info  // Unterminated or invalid display name.
  };

  // Decode the Windows record of the specified game process.
  //
  // The bytes are a copy of the record made after the magic was loaded with
  // acquire ordering (see protocol.hxx). The copy can extend past the record
  // (a mapped view has the page granularity) and the extra bytes are
  // ignored.
  //
  // The registration is only changed if the outcome is valid.
  //
  registration_outcome
  decode_record (bytes, process_id, registration&) noexcept;

  // Decode the Linux record of the specified wineserver process with the
  // specified start time.
  //
  // The bytes are the record file content. The game writes the file in one
  // go, so its size must equal the record size. A shorter file is normally
  // one that is still being written.
  //
  // The registration is only changed if the outcome is valid.
  //
  registration_outcome
  decode_unix_record (bytes, process_id, start_time, registration&) noexcept;
}

#include <libcompanion/registration.ixx>
