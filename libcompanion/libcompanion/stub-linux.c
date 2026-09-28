/* Copyright (c) the IW4x authors (see the AUTHORS file).
 * SPDX-License-Identifier: GPL-3.0-only
 */

/* Empty 64-bit stand-in for the Linux companion module.
 *
 * The launcher preloads the module from a directory named after $PLATFORM
 * (see iw4x-steam.in). The 32-bit Steam client loads the module itself and
 * the 64-bit programs that start the client load this library. Without it
 * the dynamic linker would warn in each 64-bit program that it cannot
 * preload the module.
 *
 * Note that ISO C requires a translation unit to contain a declaration.
 */
typedef int iw4x_steam_stub;

/* This file is only built for an x86_64 target (as determined by the build
 * system). If the compiler targets something else, then it was passed an
 * option like -m32 that the build system is unaware of (see
 * libs{iw4x-steam} in the buildfile).
 */
#ifndef __x86_64__
#  error target is not x86_64 (specify config.c.target and config.cxx.target)
#endif
