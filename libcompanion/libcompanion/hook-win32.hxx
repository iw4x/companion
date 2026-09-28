// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace companion
{
  // Windows platform layer of the send hook (see hook.hxx). The detour is
  // installed with MinHook.
  //

  // Detour the send function at the specified address. On failure, issue
  // diagnostics, leave the function unchanged, and return false.
  //
  bool
  install_send_hook (void* function) noexcept;

  // Start the thread that waits on this Steam process's record change event,
  // which a game signals when it registers, updates, or removes its record.
  // This lets the hook resend a report that Steam sent before the game
  // registered. Without the watcher, such a report is corrected by the next
  // report that Steam sends.
  //
  // On failure, issue diagnostics and return false.
  //
  bool
  start_record_watcher () noexcept;
}
