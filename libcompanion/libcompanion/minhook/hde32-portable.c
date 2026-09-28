/* Copyright (c) the IW4x authors (see the AUTHORS file).
 * SPDX-License-Identifier: GPL-3.0-only
 */

/* HDE i386 decoder built for any target.
 *
 * Upstream compiles hde32.c only for i386 since MinHook decodes the code of
 * its own process. We decode the i386 code of the Linux Steam client in
 * libcompanion, which is platform-independent and tested on any host,
 * including x86_64 (see ../detour.hxx). The decoder is portable C that only
 * reads bytes, so we compile it unchanged and satisfy its architecture
 * check by defining the macro it tests.
 *
 * The standard headers it uses are included first so that the macro cannot
 * affect their declarations. For the same reason it always uses the
 * <windows.h> stand-in (see windows.h).
 */

#include <string.h>
#include <stdint.h>

#if !defined(_M_IX86) && !defined(__i386__)
#  define _M_IX86
#endif

#include "hde32.c"
