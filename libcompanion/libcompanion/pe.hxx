// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <cstddef> // size_t

#include <libcompanion/types.hxx>

namespace companion
{
  // Mapped x64 PE32+ image parsing.
  //
  // The only image we parse is the mapped steamclient64.dll and the locator
  // needs two things from it: the sections and the exception directory
  // function table (.pdata). We parse both directly from the image bytes,
  // which makes the locator a pure function of these bytes that can be
  // tested on any host with synthesized images.
  //
  // The bytes start at the image base, so an RVA is an offset into them.
  // The image content is untrusted: every read is bounds-checked and any
  // image that doesn't match our expectations exactly is rejected.
  //

  // Maximum number of sections. This is the format limit and real images
  // have far fewer.
  //
  inline constexpr std::size_t pe_section_capacity (96);

  struct pe_section
  {
    std::uint32_t rva;
    std::uint32_t size;
    bool          executable;
  };

  // Parsed image.
  //
  struct pe_image
  {
    bytes          data;
    std::uint32_t  functions;      // RUNTIME_FUNCTION table RVA.
    std::uint32_t  function_count;
    std::uint16_t  section_count;  // Readable non-discardable sections only.
    pe_section     sections[pe_section_capacity];
  };

  // Image parsing outcome. The failures are listed in the order of the
  // checks.
  //
  enum class pe_outcome: std::uint8_t
  {
    valid,
    truncated, // Headers extend past the image bytes.
    dos,       // Missing MZ signature.
    nt,        // Missing PE signature.
    machine,   // Image other than x64 PE32+.
    sections,  // Section outside the image or too many sections.
    functions  // Function table outside the image.
  };

  // Return the outcome name (for example, truncated).
  //
  const char*
  pe_outcome_name (pe_outcome) noexcept;

  // Return SizeOfImage from the headers or 0 if they are not the headers of
  // an x64 PE32+ image.
  //
  // This is used to determine the extent of a loaded module. The loader maps
  // the headers at the module base, so they can be read before the size is
  // known.
  //
  std::uint32_t
  pe_image_size (bytes headers) noexcept;

  // Parse the image.
  //
  pe_outcome
  parse_pe (bytes, pe_image&) noexcept;

  // Look up the entry point RVA of the function containing the RVA. Return
  // 0 if there is none.
  //
  // A function can be split into several table entries (for example, a
  // cold block or a shrink-wrapped prologue), each chained to the primary
  // entry. The result is the start of the primary entry.
  //
  std::uint32_t
  find_function (const pe_image&, std::uint32_t rva) noexcept;
}
