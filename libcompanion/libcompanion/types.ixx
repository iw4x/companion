// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

namespace companion
{
  constexpr bool
  valid (app_id a) noexcept
  {
    std::uint32_t v (static_cast<std::uint32_t> (a));
    return v != 0 && v <= 0xFFFFFF;
  }

  constexpr std::uint32_t
  value (process_id p) noexcept
  {
    return static_cast<std::uint32_t> (p);
  }

  constexpr std::uint32_t
  value (user_id u) noexcept
  {
    return static_cast<std::uint32_t> (u);
  }

  constexpr std::uint64_t
  value (start_time t) noexcept
  {
    return static_cast<std::uint64_t> (t);
  }

  constexpr std::uint32_t
  value (app_id a) noexcept
  {
    return static_cast<std::uint32_t> (a);
  }
}
