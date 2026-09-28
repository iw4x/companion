// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// We don't export anything from Companion module. Its entry points
// (DllMain() and the rundll32 attach export on Windows, the ELF constructor
// on Linux) and the protocol.hxx layouts are declared without this macro. So
// LIBCOMPANION_SYMEXPORT is empty in every case, which keeps the symbols the
// module exposes in the Steam client's process to those entry points.
//
// We keep the four standard cases so that exporting a symbol only requires
// changing this header.
//
#if defined(LIBCOMPANION_STATIC)         // Using static.
#  define LIBCOMPANION_SYMEXPORT
#elif defined(LIBCOMPANION_STATIC_BUILD) // Building static.
#  define LIBCOMPANION_SYMEXPORT
#elif defined(LIBCOMPANION_SHARED)       // Using shared.
#  define LIBCOMPANION_SYMEXPORT
#elif defined(LIBCOMPANION_SHARED_BUILD) // Building shared.
#  define LIBCOMPANION_SYMEXPORT
#else
// If none of the above macros are defined, then we assume we are being used
// by some third-party build system that cannot/doesn't signal the library
// type.
//
#  define LIBCOMPANION_SYMEXPORT         // Using static or shared.
#endif
