// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

namespace companion
{
  constexpr std::string_view
  extra_info (const registration& r) noexcept
  {
    return std::string_view (r.extra_info, r.extra_info_size);
  }
}
