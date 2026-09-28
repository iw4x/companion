// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <cstring> // memcpy()
#include <utility> // forward()

namespace companion
{
  template <diag_argument... A>
  diag_line
  format_diag (std::string_view p,
               std::format_string<A...> f,
               A&&... a) noexcept
  {
    // Space reserved at the end of the line for the ellipsis, newline, and
    // NUL.
    //
    constexpr std::string_view ellipsis ("...");
    constexpr std::size_t      tail (ellipsis.size () + 2);

    diag_line r;

    std::size_t n (p.size () < diag_capacity - tail
                   ? p.size ()
                   : diag_capacity - tail);

    std::memcpy (r.data, p.data (), n);

    // Format the message into the remaining space. The format_to_n()
    // result contains the untruncated size, which tells us whether to append
    // the ellipsis.
    //
    // The format string is checked at compile time and formatting can still
    // throw at runtime (for example, for an invalid dynamic width). In this
    // case print a placeholder message.
    //
    std::size_t m (diag_capacity - tail - n);

    try
    {
      auto x (std::format_to_n (r.data + n,
                                static_cast<std::ptrdiff_t> (m),
                                f,
                                std::forward<A> (a)...));

      if (static_cast<std::size_t> (x.size) <= m)
        n += static_cast<std::size_t> (x.size);
      else
      {
        n += m;
        std::memcpy (r.data + n, ellipsis.data (), ellipsis.size ());
        n += ellipsis.size ();
      }
    }
    catch (...)
    {
      constexpr std::string_view e ("<invalid diagnostic>");

      std::size_t k (e.size () < m ? e.size () : m);
      std::memcpy (r.data + n, e.data (), k);
      n += k;
    }

    r.data[n++] = '\n';
    r.data[n] = '\0';
    r.size = static_cast<std::uint16_t> (n);

    return r;
  }
}
