// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/status.hxx>

#include <concepts>
#include <cstring>     // memcpy()
#include <type_traits> // is_trivially_copyable_v

#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  status
  make_status (companion_state s, uint32_t h) noexcept
  {
    LIBCOMPANION_PRE (valid (s));

    return status {status_magic,
                   status_version,
                   sizeof (status),
                   static_cast<uint32_t> (s),
                   companion_build,
                   h};
  }

  unix_status
  make_unix_status (companion_state s, uint32_t h, process_id p) noexcept
  {
    LIBCOMPANION_PRE (valid (s));

    return unix_status {unix_status_magic,
                        unix_status_version,
                        sizeof (unix_status),
                        static_cast<uint32_t> (s),
                        companion_build,
                        h,
                        value (p)};
  }

  // Requirements shared by status and unix_status, which lets decode() be
  // written once for both layouts.
  //
  template <typename S>
  concept status_layout =
    is_trivially_copyable_v<S> &&
    requires (const S& x)
    {
      { x.magic }   -> same_as<const uint32_t&>;
      { x.version } -> same_as<const uint32_t&>;
      { x.size }    -> same_as<const uint32_t&>;
      { x.state }   -> same_as<const uint32_t&>;
      { x.build }   -> same_as<const uint32_t&>;
      { x.hook }    -> same_as<const uint32_t&>;
    };

  static_assert (status_layout<status>);
  static_assert (status_layout<unix_status>);

  // Check the common fields and, if they are valid, store them in the
  // result.
  //
  template <status_layout S>
  static status_outcome
  decode (const S& x,
          uint32_t magic,
          uint32_t version,
          process_id p,
          companion_status& r) noexcept
  {
    if (x.magic != magic)
      return status_outcome::magic;

    if (x.version != version)
      return status_outcome::version;

    if (x.size != sizeof (S))
      return status_outcome::size;

    companion_state s (static_cast<companion_state> (x.state));

    if (!valid (s))
      return status_outcome::state;

    r.state = s;
    r.build = x.build;
    r.hook = x.hook;
    r.steam = p;

    return status_outcome::valid;
  }

  status_outcome
  decode_status (bytes b, process_id p, companion_status& r) noexcept
  {
    if (b.size () < sizeof (status))
      return status_outcome::truncated;

    status x;
    memcpy (&x, b.data (), sizeof (x));

    return decode (x, status_magic, status_version, p, r);
  }

  status_outcome
  decode_unix_status (bytes b, companion_status& r) noexcept
  {
    if (b.size () != sizeof (unix_status))
      return status_outcome::truncated;

    unix_status x;
    memcpy (&x, b.data (), sizeof (x));

    return decode (x,
                   unix_status_magic,
                   unix_status_version,
                   static_cast<process_id> (x.steam_pid),
                   r);
  }
}
