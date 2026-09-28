// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

namespace companion
{
  constexpr bool
  valid (companion_state s) noexcept
  {
    return static_cast<std::uint32_t> (s) <=
           static_cast<std::uint32_t> (companion_state::failed);
  }
}
