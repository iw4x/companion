// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only WITH AdditionRef-IW4x-Exception-1.1

#include <companion/utility.hxx>

namespace companion
{
  // The installation directories are only known if the build is configured
  // for install (see buildfile). We keep the path relative so that the
  // installation can be moved as a whole, which the game's installation
  // does with its copy of the program.
  //
#ifdef COMPANION_INSTALL_LIB
  const std::string_view install_lib (COMPANION_INSTALL_LIB);
#else
  const std::string_view install_lib; // Empty.
#endif
}
