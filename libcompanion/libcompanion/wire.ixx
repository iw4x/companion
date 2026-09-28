// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

namespace companion
{
  constexpr std::size_t
  varint_size (std::uint64_t v) noexcept
  {
    // Each byte holds 7 bits and zero still takes one byte, which v | 1
    // handles without a branch.
    //
    return (static_cast<std::size_t> (std::bit_width (v | 1)) + 6) / 7;
  }

  constexpr std::size_t
  tag_size (std::uint32_t n) noexcept
  {
    return varint_size (static_cast<std::uint64_t> (n) << 3);
  }
}
