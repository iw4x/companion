// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <cstddef> // size_t

#include <libcompanion/types.hxx>

namespace companion
{
  // Mapped i386 ELF32 shared object parsing.
  //
  // This is the Linux counterpart of pe.hxx. The Linux Steam client is
  // 32-bit and the only image we parse is the mapped steamclient.so. The
  // locator needs two things from it: the loadable segments and the binary
  // search table of the unwind information (.eh_frame_hdr). We parse both
  // directly from the image bytes so that the locator is a pure function of
  // these bytes (and can be tested on any host).
  //
  // The bytes start at the image load base, so an image address (for
  // example, p_vaddr) is an offset into them. The dynamic linker reserves
  // the whole address range of the image and leaves the gaps between the
  // segments inaccessible. So every read of the image content is both
  // bounds-checked and verified to lie within a readable segment (see
  // find_segment()).
  //
  // The image content is untrusted and any image that doesn't match our
  // expectations exactly is rejected.
  //

  // Maximum number of loadable segments. The format has no limit and the
  // linkers produce at most five (headers, code, read-only data, RELRO, and
  // data).
  //
  inline constexpr std::size_t elf_segment_capacity (16);

  struct elf_segment
  {
    std::uint32_t address;
    std::uint32_t size;       // Memory size (including .bss).
    bool          writable;
    bool          executable;
  };

  // Parsed image.
  //
  struct elf_image
  {
    bytes          data;
    std::uint32_t  header;         // .eh_frame_hdr address or 0 if none.
    std::uint32_t  functions;      // Search table address.
    std::uint32_t  function_count;
    std::uint16_t  segment_count;  // Readable segments only.
    elf_segment    segments[elf_segment_capacity];
  };

  // Function as described by its unwind information.
  //
  struct elf_function
  {
    std::uint32_t address; // 0 if not found.
    std::uint32_t size;
  };

  // Image parsing outcome. The failures are listed in the order of the
  // checks.
  //
  enum class elf_outcome: std::uint8_t
  {
    valid,
    truncated, // Headers extend past the image bytes.
    ident,     // Missing or non-ELFCLASS32/ELFDATA2LSB identification.
    machine,   // Object other than an i386 shared object.
    headers,   // Unsupported or unmapped program headers (see parse_elf()).
    segments,  // Segment outside the image or too many segments.
    functions, // Search table outside a readable segment.
    encoding   // Unsupported search table encoding.
  };

  // Return the outcome name (for example, truncated).
  //
  const char*
  elf_outcome_name (elf_outcome) noexcept;

  // Return the image size (that is, the end address of its last loadable
  // segment) calculated from the headers or 0 if they are not the headers of
  // an i386 shared object.
  //
  // This is used to determine the extent of a loaded image. The dynamic
  // linker maps the headers at the image base, so they can be read before
  // the size is known.
  //
  std::uint32_t
  elf_image_size (bytes headers) noexcept;

  // Parse the image.
  //
  // The ELF and program headers must be mapped at the image base (that is,
  // lie within a loadable segment that maps the start of the file to address
  // 0). The linkers always lay out shared objects this way and this is what
  // allows us to find the program headers without the dynamic linker.
  //
  elf_outcome
  parse_elf (bytes, elf_image&) noexcept;

  // Find the readable segment that fully contains [address, address + size)
  // and return NULL if there is none. Since segments don't overlap, the
  // result is unique.
  //
  const elf_segment*
  find_segment (const elf_image&,
                std::uint32_t address,
                std::size_t size) noexcept;

  // Look up the function containing the address. If there is none, then the
  // result has address 0.
  //
  // The search table contains only the function start addresses and the
  // size comes from the function's FDE. An address past the end of the
  // preceding function (for example, padding or code without unwind
  // information) is not in any function.
  //
  elf_function
  find_function (const elf_image&, std::uint32_t address) noexcept;

  // i386 general-purpose registers in the encoding order (as used in the
  // ModRM reg field and the low bits of a +r opcode).
  //
  enum class i386_register: std::uint8_t
  {
    eax, ecx, edx, ebx, esp, ebp, esi, edi
  };

  // If the code at the specified address is a PC thunk, then set the
  // register it loads and return true.
  //
  // Position-independent i386 code has no PC-relative addressing and
  // obtains its own address from the return address pushed by a call (see
  // locate.hxx for how it then loads the GOT). GCC calls a PC thunk
  // (__x86.get_pc_thunk.<r>), which is mov <r>, [esp]; ret. Code that loads
  // esp is not a PC thunk.
  //
  bool
  pc_thunk (const elf_image&, std::uint32_t address, i386_register&) noexcept;
}
