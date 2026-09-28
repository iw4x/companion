// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Use this header to include <windows.h>. It is included in the lean mode and
// without the min() and max() macros, which conflict with the standard
// library.
//
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <format>
#include <utility>     // forward()
#include <cstddef>     // size_t
#include <string_view>

#include <libcompanion/types.hxx>
#include <libcompanion/diagnostics.hxx>

// Windows utilities shared by the part of the companion module that runs in
// the Steam client (host-win32.cxx and hook-win32.cxx) and the part that runs
// in rundll32 (attach-win32.cxx).
//
namespace companion
{
  // Diagnostics.
  //
  // The module has no console, so the diagnostics are written with
  // OutputDebugString(). They can be viewed with DebugView or an attached
  // debugger and, under Wine, on the terminal with WINEDEBUG=+debugstr.
  //
  inline constexpr std::string_view diag_prefix ("[iw4x-steam64] ");

  void
  write_diag (const diag_line&) noexcept;

  template <diag_argument... A>
  void
  diag (std::format_string<A...>, A&&...) noexcept;

  // RAII type for handles.
  //
  // Note that it is only suitable for the handle kinds that use NULL as the
  // invalid value (processes, threads, events, file mappings). File handles
  // use INVALID_HANDLE_VALUE and are not supported.
  //
  struct auto_handle
  {
    HANDLE handle;

    explicit
    auto_handle (HANDLE h = nullptr) noexcept: handle (h) {}

    ~auto_handle () noexcept;

    auto_handle (const auto_handle&) = delete;
    auto_handle& operator= (const auto_handle&) = delete;
  };

  // Release the ownership of the handle and return it. The handle then
  // remains open until the process exits.
  //
  HANDLE
  release (auto_handle&) noexcept;

  // Steam client executable and the library that contains the send
  // function.
  //
  inline constexpr const wchar_t* steam_executable     (L"steam.exe");
  inline constexpr const wchar_t* steam_client_library (L"steamclient64.dll");

  // Image path buffer capacity in characters. The paths we check fit and a
  // longer path is treated as belonging to some other image.
  //
  inline constexpr std::size_t path_capacity (520);

  // Return true if the image path refers to the Steam client executable.
  // The file name is compared case-insensitively, as by the file system.
  //
  bool
  steam_image (std::wstring_view path) noexcept;

  // Copy the start of the named file mapping (a record or a status, see
  // protocol.hxx) that another process publishes into the buffer.
  //
  // The publisher stores the 32-bit field at the release offset last with
  // release ordering. So we load this field first with acquire ordering and
  // then copy the buffer.
  //
  // Return false if the mapping doesn't exist (the publisher has exited) or
  // is smaller than the buffer.
  //
  bool
  snapshot_mapping (const wchar_t* name,
                    std::size_t release_offset,
                    mutable_bytes) noexcept;
}

#include <libcompanion/utility-win32.txx>
