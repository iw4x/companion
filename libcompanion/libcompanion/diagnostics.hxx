// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <format>
#include <cstdint>
#include <cstddef>     // size_t, ptrdiff_t
#include <string_view>
#include <type_traits> // remove_cvref_t

namespace companion
{
  // Diagnostics formatting.
  //
  // Companion module runs inside the Steam client, partly on its network
  // thread, so issuing diagnostics does not allocate or throw. Each line is
  // formatted into a fixed-size buffer and passed to the platform sink as a
  // whole (the debugger output on Windows and stderr on Linux; see
  // utility-*.hxx). This header implements the platform-independent
  // formatting.
  //
  // A line consists of the prefix, the message, and the newline, followed by
  // the terminating NUL. A message that doesn't fit is truncated and ends
  // with an ellipsis.
  //

  // Diagnostics line capacity including the newline and NUL.
  //
  inline constexpr std::size_t diag_capacity (512);

  struct diag_line
  {
    std::uint16_t size; // Including the newline but excluding the NUL.
    char          data[diag_capacity];
  };

  static_assert (diag_capacity <= UINT16_MAX);

  // Return the line text including the newline.
  //
  constexpr std::string_view
  text (const diag_line&) noexcept;

  template <typename T>
  concept diag_argument = std::formattable<std::remove_cvref_t<T>, char>;

  // Format the diagnostics line. The prefix is truncated if it doesn't leave
  // room for the ellipsis, newline, and NUL. If formatting fails at runtime,
  // then the message is replaced with <invalid diagnostic>.
  //
  template <diag_argument... A>
  diag_line
  format_diag (std::string_view prefix,
              std::format_string<A...>,
              A&&...) noexcept;
}

#include <libcompanion/diagnostics.ixx>
#include <libcompanion/diagnostics.txx>
