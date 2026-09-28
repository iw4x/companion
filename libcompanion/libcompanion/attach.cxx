// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/attach.hxx>

#include <cstdint>

using namespace std;

namespace companion
{
  // Characters that rundll32 can leave before and after the process id.
  //
  static constexpr wstring_view attach_whitespace (L" \t\r\n");

  bool
  parse_attach_arguments (wstring_view s, process_id& p) noexcept
  {
    size_t b (s.find_first_not_of (attach_whitespace));
    size_t e (s.find_last_not_of (attach_whitespace));

    if (b == wstring_view::npos)
      return false;

    s = s.substr (b, e - b + 1);

    // Parse by hand to accept only decimal digits and 32-bit values. Both
    // wcstoul() and 32-bit truncation would map some invalid arguments to a
    // valid process id.
    //
    uint64_t v (0);
    for (wchar_t c: s)
    {
      if (c < L'0' || c > L'9')
        return false;

      v = v * 10 + static_cast<uint64_t> (c - L'0');

      if (v > UINT32_MAX)
        return false;
    }

    if (v == 0)
      return false;

    p = static_cast<process_id> (v);
    return true;
  }
}
