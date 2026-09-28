// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

namespace companion
{
  constexpr std::string_view
  text (const diag_line& l) noexcept
  {
    return std::string_view (l.data, l.size);
  }
}
