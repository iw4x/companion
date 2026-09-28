// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <cstddef> // size_t

#include <libcompanion/types.hxx>
#include <libcompanion/registration.hxx>

namespace companion
{
  // GamesPlayed frame rewriting.
  //
  // Steam reports the running games to the CM server with the
  // CMsgClientGamesPlayed message, one GamePlayed entry per game, and the
  // server shows each entry to friends as the game being played. In each
  // entry that refers to a registered game process we set game_extra_info to
  // the game's display name, which becomes the title friends see, and
  // game_id to the registered app id. The server only shows an app that the
  // account owns, which is why the game registers such an app.
  //
  // The rest of the frame is copied byte for byte.
  //

  // Frame layout: the 8-byte message header (EMsg with the protobuf flag
  // followed by the CMsgProtoBufHeader size), the CMsgProtoBufHeader, and
  // the message body.
  //
  inline constexpr std::size_t   frame_header_size (8);
  inline constexpr std::uint32_t frame_protobuf_flag (0x80000000);

  // Maximum size of a frame that we rewrite (and store for a resend). This
  // is well above a realistic GamesPlayed frame (a few hundred bytes per
  // running game) and a larger frame is sent as is.
  //
  inline constexpr std::size_t frame_capacity (8192);

  enum class frame_kind: std::uint8_t
  {
    other,
    games_played
  };

  // Determine the frame kind from the message header. Steam calls the send
  // hook for every frame, so this function is kept to a few loads and
  // comparisons.
  //
  frame_kind
  classify (bytes frame) noexcept;

  // Find the registration of the specified process. Return false if the
  // process is not registered. A registration that is found must be valid
  // (see valid()).
  //
  using registration_lookup = bool (*) (process_id, registration&) noexcept;

  // Write the rewritten frame to out and return its size. Return 0 if the
  // frame should be sent as is, that is, if it is not a GamesPlayed frame, is
  // malformed, has no entries of registered processes, or the result exceeds
  // out.
  //
  std::size_t
  rewrite_frame (bytes frame,
                 registration_lookup,
                 mutable_bytes out) noexcept;
}
