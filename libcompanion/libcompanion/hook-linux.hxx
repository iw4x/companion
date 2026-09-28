// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

#include <libcompanion/elf.hxx>

namespace companion
{
  // Linux platform layer of the send hook (see hook.hxx). The detour is
  // built by detour.hxx and patched into the i386 code of steamclient.so.
  //

  // Detour the send function at the specified image address. The image must
  // be the live loaded steamclient.so and the function must be as returned
  // by find_function() for this address. On failure, issue diagnostics,
  // leave the function unchanged, and return false.
  //
  bool
  install_send_hook (const elf_image&, std::uint32_t function) noexcept;

  // Start the thread that watches the record directory with inotify and
  // signals a change when a game writes or removes its record. This lets the
  // hook resend a report that Steam sent before the game registered. Without
  // the watcher, such a report is corrected by the next report that Steam
  // sends.
  //
  // On failure, issue diagnostics and return false.
  //
  bool
  start_record_watcher () noexcept;
}
