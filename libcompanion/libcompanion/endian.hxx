// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace companion
{
  // Unaligned little-endian integer access.
  //
  // All the data we decode is little-endian: protobuf fixed-width fields,
  // the Steam message header, x86 immediates, and PE and ELF structures.
  //
  // The value is composed from individual bytes, which is valid for any
  // alignment and any aliasing. GCC, Clang, and MSVC compile each function
  // into a single load or store on x86.
  //
  constexpr std::uint16_t
  load16 (const std::uint8_t* p) noexcept
  {
    return static_cast<std::uint16_t> (p[0] | p[1] << 8);
  }

  constexpr std::uint32_t
  load32 (const std::uint8_t* p) noexcept
  {
    return static_cast<std::uint32_t> (p[0])       |
           static_cast<std::uint32_t> (p[1]) << 8  |
           static_cast<std::uint32_t> (p[2]) << 16 |
           static_cast<std::uint32_t> (p[3]) << 24;
  }

  constexpr std::uint64_t
  load64 (const std::uint8_t* p) noexcept
  {
    return static_cast<std::uint64_t> (load32 (p)) |
           static_cast<std::uint64_t> (load32 (p + 4)) << 32;
  }

  constexpr void
  store32 (std::uint8_t* p, std::uint32_t v) noexcept
  {
    p[0] = static_cast<std::uint8_t> (v);
    p[1] = static_cast<std::uint8_t> (v >> 8);
    p[2] = static_cast<std::uint8_t> (v >> 16);
    p[3] = static_cast<std::uint8_t> (v >> 24);
  }

  constexpr void
  store64 (std::uint8_t* p, std::uint64_t v) noexcept
  {
    store32 (p, static_cast<std::uint32_t> (v));
    store32 (p + 4, static_cast<std::uint32_t> (v >> 32));
  }
}
