// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/pe.hxx>

#include <libcompanion/endian.hxx>
#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  // PE/COFF header layout. Each offset is relative to the structure named
  // by its prefix.
  //
  static constexpr uint16_t dos_magic   (0x5A4D);     // "MZ"
  static constexpr size_t   dos_lfanew  (0x3C);
  static constexpr size_t   dos_size    (0x40);

  static constexpr uint32_t nt_signature (0x00004550); // "PE\0\0"
  static constexpr size_t   nt_file      (4);          // IMAGE_FILE_HEADER
  static constexpr size_t   nt_optional  (24);         // Optional header.

  static constexpr size_t   file_machine       (0);
  static constexpr size_t   file_section_count (2);
  static constexpr size_t   file_optional_size (16);
  static constexpr uint16_t machine_amd64      (0x8664);

  static constexpr size_t   optional_magic      (0);
  static constexpr size_t   optional_image_size (56);
  static constexpr size_t   optional_rva_count  (108);
  static constexpr size_t   optional_exceptions (112 + 3 * 8); // Entry 3.
  static constexpr size_t   optional_size       (optional_exceptions + 8);
  static constexpr uint16_t optional_pe32plus   (0x20B);

  static constexpr size_t   section_size            (40);
  static constexpr size_t   section_virtual_size    (8);
  static constexpr size_t   section_virtual_address (12);
  static constexpr size_t   section_characteristics (36);

  static constexpr uint32_t section_discardable (0x02000000);
  static constexpr uint32_t section_execute     (0x20000000);
  static constexpr uint32_t section_read        (0x40000000);

  // Exception directory structures (RUNTIME_FUNCTION, UNWIND_INFO).
  //
  static constexpr size_t   function_size   (12);
  static constexpr size_t   function_begin  (0);
  static constexpr size_t   function_end    (4);
  static constexpr size_t   function_unwind (8);
  static constexpr uint32_t function_indirect (1);

  static constexpr uint8_t  unwind_chain_info (0x04);
  static constexpr size_t   unwind_codes      (4);

  // Maximum unwind chain length. A longer chain is treated as a cycle.
  //
  static constexpr size_t max_chain_depth (32);

  // Return true if [o, o + n) lies within the bytes. Note that the offset
  // can come from the image, so the check is written to be overflow-safe.
  //
  // We use it to validate complete structures, after which their fields are
  // read with unchecked loads.
  //
  static constexpr bool
  within (bytes b, size_t o, size_t n) noexcept
  {
    return o <= b.size () && b.size () - o >= n;
  }

  // Bounds-checked reads of individual fields outside validated structures.
  //
  static bool
  read8 (bytes b, size_t o, uint8_t& v) noexcept
  {
    if (!within (b, o, 1))
      return false;

    v = b[o];
    return true;
  }

  static bool
  read32 (bytes b, size_t o, uint32_t& v) noexcept
  {
    if (!within (b, o, 4))
      return false;

    v = load32 (b.data () + o);
    return true;
  }

  // Find the optional header of an x64 PE32+ image and validate that it is
  // within the bytes. On success, set o to its offset.
  //
  static pe_outcome
  find_optional (bytes b, size_t& o) noexcept
  {
    if (!within (b, 0, dos_size))
      return pe_outcome::truncated;

    const uint8_t* p (b.data ());

    if (load16 (p) != dos_magic)
      return pe_outcome::dos;

    uint32_t l (load32 (p + dos_lfanew));

    o = static_cast<size_t> (l) + nt_optional;

    if (!within (b, l, nt_optional) || !within (b, o, optional_size))
      return pe_outcome::truncated;

    if (load32 (p + l) != nt_signature)
      return pe_outcome::nt;

    if (load16 (p + l + nt_file + file_machine) != machine_amd64 ||
        load16 (p + o + optional_magic) != optional_pe32plus)
      return pe_outcome::machine;

    return pe_outcome::valid;
  }

  const char*
  pe_outcome_name (pe_outcome o) noexcept
  {
    switch (o)
    {
    case pe_outcome::valid:     return "valid";
    case pe_outcome::truncated: return "truncated";
    case pe_outcome::dos:       return "dos";
    case pe_outcome::nt:        return "nt";
    case pe_outcome::machine:   return "machine";
    case pe_outcome::sections:  return "sections";
    case pe_outcome::functions: return "functions";
    }

    LIBCOMPANION_UNREACHABLE ();
  }

  uint32_t
  pe_image_size (bytes b) noexcept
  {
    size_t o;
    return find_optional (b, o) == pe_outcome::valid
      ? load32 (b.data () + o + optional_image_size)
      : 0;
  }

  pe_outcome
  parse_pe (bytes b, pe_image& x) noexcept
  {
    size_t     o;
    pe_outcome r (find_optional (b, o));

    if (r != pe_outcome::valid)
      return r;

    // Note that find_optional() verified that both headers are within the
    // bytes.
    //
    const uint8_t* p (b.data ());
    size_t         f (o - nt_optional + nt_file);

    uint16_t n  (load16 (p + f + file_section_count));
    uint16_t os (load16 (p + f + file_optional_size));
    uint32_t rc (load32 (p + o + optional_rva_count));

    // The section table immediately follows the optional header, the size
    // of which is specified in the file header.
    //
    size_t t (o + os);

    if (!within (b, t, static_cast<size_t> (n) * section_size))
      return pe_outcome::truncated;

    if (n > pe_section_capacity)
      return pe_outcome::sections;

    x.data = b;
    x.section_count = 0;

    for (size_t i (0); i != n; ++i)
    {
      const uint8_t* s (p + t + i * section_size);

      uint32_t vs (load32 (s + section_virtual_size));
      uint32_t va (load32 (s + section_virtual_address));
      uint32_t c  (load32 (s + section_characteristics));

      // Keep only the sections that are mapped and readable.
      //
      if ((c & section_read) == 0 || (c & section_discardable) != 0)
        continue;

      if (!within (b, va, vs))
        return pe_outcome::sections;

      x.sections[x.section_count++] = pe_section {
        va, vs, (c & section_execute) != 0};
    }

    x.functions = 0;
    x.function_count = 0;

    // An image without the exception directory is valid and has no
    // functions.
    //
    if (rc > 3 && os >= optional_size)
    {
      uint32_t fa (load32 (p + o + optional_exceptions));
      uint32_t fs (load32 (p + o + optional_exceptions + 4));

      if (!within (b, fa, fs))
        return pe_outcome::functions;

      x.functions = fa;
      x.function_count = static_cast<uint32_t> (fs / function_size);
    }

    LIBCOMPANION_ASSERT (x.section_count <= n);
    return pe_outcome::valid;
  }

  // Follow the unwind chain from the function table entry at offset e and
  // set begin to the primary entry's start. Return false if the chain is
  // broken or too long.
  //
  static bool
  primary_function (bytes b, size_t e, uint32_t& begin) noexcept
  {
    for (size_t d (0); d != max_chain_depth; ++d)
    {
      uint32_t u;
      if (!read32 (b, e + function_unwind, u))
        return false;

      // With the low bit set the unwind field is the RVA of another
      // RUNTIME_FUNCTION (plus one).
      //
      if ((u & function_indirect) != 0)
      {
        e = u - function_indirect;
        continue;
      }

      uint8_t h, codes;
      if (!read8 (b, u, h) || !read8 (b, u + 2, codes))
        return false;

      if (((h >> 3) & unwind_chain_info) == 0)
        return read32 (b, e + function_begin, begin);

      // The chained RUNTIME_FUNCTION follows the unwind codes, the count of
      // which is rounded up to even.
      //
      size_t c ((static_cast<size_t> (codes) + 1) & ~size_t (1));
      e = u + unwind_codes + c * 2;
    }

    return false;
  }

  uint32_t
  find_function (const pe_image& x, uint32_t rva) noexcept
  {
    LIBCOMPANION_PRE (within (x.data,
                              x.functions,
                              size_t (x.function_count) * function_size));

    // Note that the precondition guarantees that the table is within the
    // bytes.
    //
    bytes b (x.data);

    auto begin_of = [&b, &x] (uint32_t i) -> uint32_t
    {
      return load32 (b.data () + x.functions + i * function_size);
    };

    // Binary search for the last entry that starts at or before the RVA.
    // The table is sorted by the start address.
    //
    uint32_t l (0), h (x.function_count);

    while (l != h)
    {
      uint32_t m (l + (h - l) / 2);

      if (begin_of (m) <= rva)
        l = m + 1;
      else
        h = m;
    }

    if (l == 0)
      return 0;

    size_t e (x.functions + static_cast<size_t> (l - 1) * function_size);

    if (rva >= load32 (b.data () + e + function_end))
      return 0;

    uint32_t r;
    return primary_function (b, e, r) ? r : 0;
  }
}
