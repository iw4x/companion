// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

// Definitions that keep the Linux companion module within its glibc floor.
//
// The module runs in the Steam client on any host, so it can only depend on
// what the oldest supported glibc provides. This floor is glibc 2.36, which
// is the version in Debian 12, the oldest Debian release with long-term
// support. For our own code glibc.hxx (which every translation unit of
// the module includes first) binds each reference to a version available at
// the floor.
//
// The statically linked C++ runtime (libstdc++.a and libgcc_eh.a) is
// compiled against the build host's glibc and its references bind to the
// symbol versions of that glibc. So here we define each symbol that the
// runtime references and the floor lacks. Like everything in the module the
// definitions are hidden. As a result, they resolve the runtime's references
// at link time and require nothing from glibc at runtime.
//
// If the module requires anything else, then the floor test fails (see
// glibc.testscript) and the missing definition should be added here.
//
#include <cctype> // isspace()
#include <cerrno>

#include <libcompanion/contract.hxx>

using namespace std;

// C17 strtoul() under its own name. The <cstdlib> declaration is redirected
// to __isoc23_strtoul() in glibc 2.38 and later, which is defined below.
//
extern "C" unsigned long
c17_strtoul (const char*, char**, int) noexcept __asm__ ("strtoul");

extern "C"
{
  // C23 strtoul() (glibc 2.38 and later), which additionally accepts the 0b
  // and 0B binary prefixes. libstdc++ uses it to parse GLIBCXX_TUNABLES
  // (eh_alloc.cc and debug.cc).
  //
  // The prefix is recognized for the bases 0 and 2 when followed by a binary
  // digit. In all the other cases the result matches C17, which parses the
  // prefix's 0 as the number and stops at the b.
  //
  unsigned long
  __isoc23_strtoul (const char* s, char** e, int b) noexcept
  {
    LIBCOMPANION_PRE (s != nullptr);

    if (b == 0 || b == 2)
    {
      const char* p (s);

      while (isspace (static_cast<unsigned char> (*p)))
        ++p;

      bool minus (*p == '-');

      if (*p == '-' || *p == '+')
        ++p;

      if (p[0] == '0'                   &&
          (p[1] == 'b' || p[1] == 'B') &&
          (p[2] == '0' || p[2] == '1'))
      {
        // The digits after the prefix cannot have their own sign or
        // whitespace, so parse them with C17 strtoul() in base 2. The minus
        // sign negates the result unless it is out of range, the same as in
        // C17.
        //
        int           x (errno);
        unsigned long r;

        errno = 0;
        r = c17_strtoul (p + 2, e, 2);

        if (errno == ERANGE)
          return r;

        errno = x;
        return minus ? 0 - r : r;
      }
    }

    return c17_strtoul (s, e, b);
  }
}
