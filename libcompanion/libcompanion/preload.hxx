// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only WITH AdditionRef-IW4x-Exception-1.1

#pragma once

#include <string_view>

namespace companion
{
  // Linux module preload layout, which the module and the companion program
  // share.
  //
  // The module is installed into the preload_directory subdirectory of the
  // library installation directory, in the preload_platform subdirectory as
  // preload_file. The companion program preloads it as
  // <directory>/$PLATFORM/<file>, which the dynamic linker expands to
  // preload_platform in the 32-bit Steam client (see the libcompanion
  // buildfile for the stand-ins in the other platform subdirectories).
  //
  inline constexpr std::string_view preload_directory ("companion");
  inline constexpr std::string_view preload_platform  ("i686");
  inline constexpr std::string_view preload_file      ("libcompanion.so");
}
