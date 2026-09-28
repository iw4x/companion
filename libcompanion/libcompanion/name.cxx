// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/name.hxx>

using namespace std;

namespace companion
{
  object_name
  record_name (process_id p) noexcept
  {
    return details::format_key<wchar_t, object_name_capacity> (
      record_name_format, value (p));
  }

  object_name
  status_name (process_id p) noexcept
  {
    return details::format_key<wchar_t, object_name_capacity> (
      status_name_format, value (p));
  }

  object_name
  record_event_name (process_id p) noexcept
  {
    return details::format_key<wchar_t, object_name_capacity> (
      record_event_format, value (p));
  }

  unix_path
  unix_record_path (process_id p) noexcept
  {
    return details::format_key<char, unix_path_capacity> (
      unix_record_format, value (p));
  }

  unix_path
  unix_status_path (user_id u) noexcept
  {
    return details::format_key<char, unix_path_capacity> (
      unix_status_format, value (u));
  }
}
