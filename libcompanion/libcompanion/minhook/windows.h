/* Copyright (c) the IW4x authors (see the AUTHORS file).
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

/* Stand-in <windows.h> for compiling MinHook's instruction decoder (HDE).
 *
 * HDE is portable. It only depends on Windows through pstdint.h, which
 * includes <windows.h> for the fixed-width integer types and then typedefs
 * them back to their <stdint.h> names. So this header defines exactly these
 * types under the names that pstdint.h expects. Any other use of
 * <windows.h> fails to compile.
 *
 * Note that this directory is only on the include path of the HDE objects
 * and of the detour builder, so the rest of MinHook and the Windows module
 * use the real <windows.h> (see ../buildfile).
 */

#include <stdint.h>

typedef int8_t   INT8;
typedef int16_t  INT16;
typedef int32_t  INT32;
typedef int64_t  INT64;
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
