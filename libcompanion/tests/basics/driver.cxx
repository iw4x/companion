// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/version.hxx>

// Installed library smoke test. Include the generated version header.
//
int
main ()
{
  static_assert (LIBCOMPANION_VERSION != 0);
}
