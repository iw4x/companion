// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only WITH AdditionRef-IW4x-Exception-1.1

#pragma once

#include <string_view>

namespace companion
{
  // Library installation directory relative to the executable's directory
  // or empty if the executable is not installed. The module's location is
  // only known in an installation (see utility-installed.cxx).
  //
  extern const std::string_view install_lib;
}
