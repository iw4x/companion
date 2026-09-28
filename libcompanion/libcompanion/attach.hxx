// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string_view>

#include <libcompanion/types.hxx>
#include <libcompanion/contract.hxx>
#include <libcompanion/protocol.hxx>

namespace companion
{
  // Attach entry point support (see protocol.hxx).
  //
  // The game runs the entry point with the 64-bit rundll32.exe:
  //
  //   rundll32.exe "<directory>\iw4x-steam64.dll",IW4xAttach <steam pid>
  //
  // Everything after the entry point name is passed as the command line and
  // the entry point result is used as the rundll32 exit code.
  //

  // Parse the attach entry point command line. It must contain the non-zero
  // decimal Steam process id that fits into 32 bits, optionally surrounded by
  // whitespace. Return false if the command line is invalid.
  //
  bool
  parse_attach_arguments (std::wstring_view, process_id&) noexcept;

  // Map the state of a settled (no longer starting) companion to the attach
  // result.
  //
  constexpr attach_result
  to_attach_result (companion_state) noexcept;
}

#include <libcompanion/attach.ixx>
