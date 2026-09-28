// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

namespace companion
{
  template <diag_argument... A>
  void
  diag (std::format_string<A...> f, A&&... a) noexcept
  {
    write_diag (format_diag (diag_prefix, f, std::forward<A> (a)...));
  }
}
