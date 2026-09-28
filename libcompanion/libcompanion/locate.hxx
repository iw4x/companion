// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string_view>
#include <cstdint>
#include <cstddef> // size_t

#include <libcompanion/pe.hxx>
#include <libcompanion/elf.hxx>

namespace companion
{
  // Send function location.
  //
  // We need the address of CWebSocketConnection::BBuildAndAsyncSendFrame(),
  // through which Steam sends every CM message, in steamclient64.dll (x64,
  // Windows) and steamclient.so (i386, Linux). Byte signatures break with
  // almost every Steam build, so we locate the function by its structure.
  //
  // The function opens a profiler scope named after itself. As a result, the
  // image contains the function name as a string (which we call the anchor)
  // and the function loads the anchor's address. We find all such loads
  // (references) and map each to the containing function using the unwind
  // information. The reference form and the way we pick the function among
  // those that reference the anchor depend on the architecture:
  //
  // x64    The reference is a RIP-relative lea and the function opens the
  //        scope right after its prologue. Functions that inline the send
  //        function also reference the anchor but much further from their
  //        entry. So we keep the functions that reference the anchor within
  //        max_scope_distance bytes of their entry.
  //
  // i386   Position-independent code has no PC-relative addressing. It loads
  //        the GOT address (_GLOBAL_OFFSET_TABLE_) into a register and
  //        addresses data relative to it, with lea r32, [r32 + anchor - GOT]
  //        as the reference. The scope is opened on an out-of-line path that
  //        can be anywhere in the function. In the builds we have examined,
  //        no other function references the anchor. So we keep the functions
  //        that load the GOT themselves, which guarantees that the register
  //        holds the GOT (see locate.cxx for how we recognize the load).
  //
  // The search must end with exactly one function. Any other result means a
  // build we don't recognize and no hook is installed.
  //

  // Send function name, which is also the profiler scope name.
  //
  inline constexpr std::string_view send_frame_anchor (
    "CWebSocketConnection::BBuildAndAsyncSendFrame");

  // Maximum distance between a function's entry and the anchor reference
  // (x64 only).
  //
  inline constexpr std::uint32_t max_scope_distance (0x100);

  // Maximum numbers of anchors and candidate functions that we track. A
  // supported build has one of each, so exceeding these limits means the
  // result is ambiguous.
  //
  inline constexpr std::size_t anchor_capacity    (8);
  inline constexpr std::size_t candidate_capacity (8);

  enum class locate_outcome: std::uint8_t
  {
    found,
    no_anchor,    // No anchor string (no profiler scope in this build).
    no_got,       // No GOT load in any function (i386 only).
    no_reference, // No references to the anchor.
    no_candidate, // No function that passes the architecture's test.
    ambiguous     // Too many anchors or more than one candidate function.
  };

  struct locate_result
  {
    locate_outcome outcome;
    std::uint32_t  rva;        // Function address relative to the image base.
    std::uint32_t  references; // Number of anchor references found.
    std::uint32_t  candidates; // Number of distinct candidate functions.
  };

  // Return the outcome name (for example, no-anchor).
  //
  const char*
  locate_outcome_name (locate_outcome) noexcept;

  // Locate the send function in the x64 PE or i386 ELF image.
  //
  locate_result
  locate_send_frame (const pe_image&) noexcept;

  locate_result
  locate_send_frame (const elf_image&) noexcept;
}
