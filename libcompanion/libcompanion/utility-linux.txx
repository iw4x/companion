// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <string.h> // strerror_r()

#include <utility>  // forward()

namespace companion
{
  template <diag_argument... A>
  void
  diag (std::format_string<A...> f, A&&... a) noexcept
  {
    write_diag (format_diag (diag_prefix, f, std::forward<A> (a)...));
  }
}

template <companion::details::char_format_context C>
typename C::iterator std::formatter<companion::error_number>::
format (companion::error_number e, C& c) const
{
  // This is the GNU strerror_r(), which returns the description. For an
  // unknown error number the description is formatted into the buffer.
  //
  char b[128];
  const char* s (strerror_r (static_cast<int> (e), b, sizeof (b)));

  return std::formatter<std::string_view>::format (s, c);
}
