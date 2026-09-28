// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/games-played.hxx>

#include <libcompanion/wire.hxx>
#include <libcompanion/endian.hxx>
#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  // GamesPlayed message ids. Different Steam versions use different ones.
  //
  static constexpr uint32_t games_played_emsgs[] = {
    715,  // k_EMsgClientGamesPlayedNoDataBlob
    742,  // k_EMsgClientGamesPlayed
    5410  // k_EMsgClientGamesPlayedWithDataBlob
  };

  // Protobuf field numbers.
  //
  // CMsgClientGamesPlayed:
  //
  static constexpr uint32_t body_game (1); // games_played

  // CMsgClientGamesPlayed.GamePlayed. Here process_id is the process that
  // Steam tracks for the game and process_id_list (only sent by newer
  // versions) is its whole process tree. A registered game can appear in
  // either.
  //
  static constexpr uint32_t game_id           (2);
  static constexpr uint32_t game_extra_info   (7);
  static constexpr uint32_t game_process_id   (9);
  static constexpr uint32_t game_process_list (32);

  // CMsgClientGamesPlayed.ProcessInfo:
  //
  static constexpr uint32_t process_info_id (1); // process_id

  frame_kind
  classify (bytes f) noexcept
  {
    if (f.size () < frame_header_size)
      return frame_kind::other;

    uint32_t e (load32 (f.data ()));

    if ((e & frame_protobuf_flag) == 0)
      return frame_kind::other;

    e &= ~frame_protobuf_flag;

    bool m (false);
    for (uint32_t x: games_played_emsgs)
      m = m || e == x;

    // Make sure the protobuf header is within the frame (the body can be
    // empty).
    //
    if (!m || load32 (f.data () + 4) > f.size () - frame_header_size)
      return frame_kind::other;

    return frame_kind::games_played;
  }

  // Look up the registration of the process id reported by Steam. Steam
  // uses 0 for no process and process ids are 32-bit, so such values are
  // not looked up.
  //
  static bool
  lookup_process (uint64_t p,
                  registration_lookup l,
                  registration& r) noexcept
  {
    if (p == 0 || p > UINT32_MAX)
      return false;

    if (!l (static_cast<process_id> (p), r))
      return false;

    LIBCOMPANION_ASSERT (valid (r));
    return true;
  }

  // Scan the ProcessInfo entry for a registered process id and, if found,
  // set the registration and found. Return false if the entry is malformed.
  //
  static bool
  lookup_process_list (bytes l,
                       registration_lookup f,
                       registration& r,
                       bool& found) noexcept
  {
    wire_reader rd {l};
    wire_field  x;

    while (!found && read_field (rd, x))
    {
      if (x.number == process_info_id && x.type == wire_type::varint)
        found = lookup_process (x.value, f, r);
    }

    return !rd.failed;
  }

  // Scan the GamePlayed entry for a registered process id and, if found, set
  // the registration and found. Return false if the entry is malformed.
  //
  // Note that the scan stops at the first registered process id. The rest of
  // the entry is validated when it is rewritten (see write_game()).
  //
  static bool
  find_registration (bytes g,
                     registration_lookup f,
                     registration& r,
                     bool& found) noexcept
  {
    wire_reader rd {g};
    wire_field  x;

    found = false;

    while (!found && read_field (rd, x))
    {
      if (x.number == game_process_id && x.type == wire_type::varint)
        found = lookup_process (x.value, f, r);

      else if (x.number == game_process_list && x.type == wire_type::length)
      {
        if (!lookup_process_list (x.payload, f, r, found))
          return false;
      }
    }

    return !rd.failed;
  }

  // Return true if the GamePlayed field is one that we replace.
  //
  static constexpr bool
  owned (uint32_t n) noexcept
  {
    return n == game_id || n == game_extra_info;
  }

  // Calculate the GamePlayed entry size after the rewrite. Return 0 if the
  // entry is malformed.
  //
  static size_t
  rewritten_size (bytes g, const registration& r) noexcept
  {
    wire_reader rd {g};
    wire_field  x;
    size_t      n (0);

    while (read_field (rd, x))
    {
      if (!owned (x.number))
        n += x.raw.size ();
    }

    if (rd.failed)
      return 0;

    // Add the replaced fields: game_id is fixed64 and game_extra_info is
    // length-delimited.
    //
    return n +
           tag_size (game_id) + 8 +
           tag_size (game_extra_info) +
           varint_size (r.extra_info_size) + r.extra_info_size;
  }

  // Write the GamePlayed entry as a games_played field with all the
  // occurrences of the replaced fields removed and their new values
  // appended. Return false if the entry is malformed.
  //
  static bool
  write_game (wire_writer& w, bytes g, const registration& r) noexcept
  {
    size_t n (rewritten_size (g, r));
    if (n == 0)
      return false;

    write_length_header (w, body_game, n);

    size_t b (w.size);

    wire_reader rd {g};
    wire_field  x;

    while (read_field (rd, x))
    {
      if (!owned (x.number))
        write_raw (w, x.raw);
    }

    // The CGameID of an app is the app id (in the low 24 bits) with zero
    // type (k_EGameIDTypeApp) and mod id, which is the app id value itself.
    //
    string_view i (extra_info (r));

    write_fixed64 (w, game_id, value (r.app));
    write_length (w,
                  game_extra_info,
                  bytes (reinterpret_cast<const uint8_t*> (i.data ()),
                         i.size ()));

    LIBCOMPANION_ASSERT (w.overflow || w.size - b == n);
    return true;
  }

  // Write the message body with the entries of registered processes
  // rewritten. Return false if the body is malformed or there is nothing to
  // rewrite.
  //
  static bool
  write_body (wire_writer& w, bytes body, registration_lookup f) noexcept
  {
    wire_reader rd {body};
    wire_field  x;
    bool        rewritten (false);

    while (read_field (rd, x))
    {
      registration r;
      bool found (false);

      if (x.number == body_game && x.type == wire_type::length)
      {
        if (!find_registration (x.payload, f, r, found))
          return false;
      }

      if (!found)
      {
        write_raw (w, x.raw);
        continue;
      }

      if (!write_game (w, x.payload, r))
        return false;

      rewritten = true;
    }

    return !rd.failed && rewritten;
  }

  size_t
  rewrite_frame (bytes f, registration_lookup l, mutable_bytes o) noexcept
  {
    LIBCOMPANION_PRE (l != nullptr);

    if (classify (f) != frame_kind::games_played)
      return 0;

    // Copy the message and protobuf headers. The protobuf header doesn't
    // depend on the body, so it remains valid.
    //
    size_t h (frame_header_size + load32 (f.data () + 4));

    wire_writer w {o};
    write_raw (w, f.first (h));

    if (!write_body (w, f.subspan (h), l) || w.overflow)
      return 0;

    LIBCOMPANION_ASSERT (w.size <= o.size ());
    return w.size;
  }
}
