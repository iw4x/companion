// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>

#include <libcompanion/types.hxx>
#include <libcompanion/resend.hxx>
#include <libcompanion/diagnostics.hxx>
#include <libcompanion/games-played.hxx>
#include <libcompanion/registration.hxx>

namespace companion
{
  // Send hook.
  //
  // The hook replaces CWebSocketConnection::BBuildAndAsyncSendFrame() (see
  // locate.hxx), which Steam calls on its network thread to send each frame
  // to the CM server. For a GamesPlayed frame the hook sends the rewritten
  // version (see games-played.hxx) and remembers the report. After any other
  // binary frame it gives the resend logic a chance to send the report again
  // (see resend.hxx). All the other frames are passed to the original
  // function unchanged.
  //
  // The hook logic is platform-independent and is implemented here as a
  // template over the platform layer (see send_platform below). The platform
  // layer installs the detour (see hook-win32.cxx and hook-linux.cxx) and
  // instantiates send_frame() with its own P. All the platform calls are
  // resolved at compile time.
  //
  // Note that the hook is never uninstalled. Steam's network thread can be
  // executing it at any moment, so the module remains loaded until the
  // Steam client exits.
  //

  // Signature of the hooked member function:
  //
  // bool CWebSocketConnection::BBuildAndAsyncSendFrame (EWebSocketOpCode,
  //                                                     const uint8*,
  //                                                     uint32)
  //
  // The object pointer is passed as the first argument. This matches the
  // member function calling convention on x64 (there is only one) and on
  // i386 with GCC (this is pushed on the stack first, the same as cdecl).
  //
  using send_frame_function = bool (*) (void*,
                                        std::uint32_t,
                                        const std::uint8_t*,
                                        std::uint32_t);

  // Binary frame opcode (EWebSocketOpCode).
  //
  inline constexpr std::uint32_t websocket_binary (0x2);

  // Platform layer interface. P provides the following static members:
  //
  // original()        call the original function through its trampoline
  // lookup()          registration lookup (see registration_lookup)
  // current_thread()  return the calling thread id
  // now()             return the current monotonic time
  // write_diag()      write the diagnostics line to the platform sink
  // diag_prefix       diagnostics line prefix
  //
  template <typename P>
  concept send_platform =
    requires (void* c,
              std::uint32_t o,
              const std::uint8_t* d,
              std::uint32_t n,
              const diag_line& l)
    {
      { P::original (c, o, d, n) }      -> std::same_as<bool>;
      { P::current_thread () } noexcept -> std::same_as<thread_id>;
      { P::now () } noexcept            -> std::same_as<timestamp>;
      { P::write_diag (l) } noexcept;

      requires std::convertible_to<decltype (&P::lookup),
                                   registration_lookup>;

      requires std::convertible_to<decltype (P::diag_prefix),
                                   std::string_view>;
    };

  // Send the frame on the connection and return the result of the original
  // function for the frame that was actually sent (the original or its
  // rewrite).
  //
  template <send_platform P>
  bool
  send_frame (resend_state&,
              void* connection,
              std::uint32_t opcode,
              const std::uint8_t* data,
              std::uint32_t size) noexcept;
}

#include <libcompanion/hook.txx>
