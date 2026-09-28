// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/elf.hxx>

#include <limits>  // numeric_limits
#include <cstring> // memchr()

#include <libcompanion/endian.hxx>
#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  // ELF header and program header layout (System V ABI and its i386
  // supplement). Each offset is relative to the structure named by its
  // prefix.
  //
  static constexpr uint32_t ident_magic   (0x464C457F); // "\x7F" "ELF"
  static constexpr size_t   ident_class   (4);
  static constexpr size_t   ident_data    (5);
  static constexpr size_t   ident_version (6);
  static constexpr uint8_t  class_32      (1);
  static constexpr uint8_t  data_lsb      (1);
  static constexpr uint8_t  version_current (1);

  static constexpr size_t   header_type       (16);
  static constexpr size_t   header_machine    (18);
  static constexpr size_t   header_phoff      (28);
  static constexpr size_t   header_phentsize  (42);
  static constexpr size_t   header_phnum      (44);
  static constexpr size_t   header_size       (52);
  static constexpr uint16_t type_dyn          (3);
  static constexpr uint16_t machine_386       (3);
  static constexpr uint16_t phnum_extended    (0xFFFF); // PN_XNUM

  static constexpr size_t   program_type   (0);
  static constexpr size_t   program_offset (4);
  static constexpr size_t   program_vaddr  (8);
  static constexpr size_t   program_filesz (16);
  static constexpr size_t   program_memsz  (20);
  static constexpr size_t   program_flags  (24);
  static constexpr size_t   program_size   (32);

  static constexpr uint32_t type_load      (1);
  static constexpr uint32_t type_eh_frame  (0x6474E550); // PT_GNU_EH_FRAME

  static constexpr uint32_t flag_execute (1);
  static constexpr uint32_t flag_write   (2);
  static constexpr uint32_t flag_read    (4);

  // Exception handling pointer encodings (DW_EH_PE_*). The low nibble is
  // the value format and the high nibble is how the value is applied (for
  // example, PC-relative).
  //
  static constexpr uint8_t  encoding_omit   (0xFF);
  static constexpr uint8_t  format_mask     (0x0F);
  static constexpr uint8_t  format_absptr   (0x00);
  static constexpr uint8_t  format_udata2   (0x02);
  static constexpr uint8_t  format_udata4   (0x03);
  static constexpr uint8_t  format_udata8   (0x04);
  static constexpr uint8_t  format_sdata2   (0x0A);
  static constexpr uint8_t  format_sdata4   (0x0B);
  static constexpr uint8_t  format_sdata8   (0x0C);
  static constexpr uint8_t  apply_mask      (0x70);
  static constexpr uint8_t  apply_pcrel     (0x10);
  static constexpr uint8_t  apply_datarel   (0x30);
  static constexpr uint8_t  encoding_indirect (0x80);

  // .eh_frame_hdr layout. The search table is an array of (function start,
  // FDE address) pairs sorted by the function start. Both addresses are
  // relative to the header.
  //
  static constexpr size_t   hdr_version       (0);
  static constexpr size_t   hdr_frame_ptr_enc (1);
  static constexpr size_t   hdr_count_enc     (2);
  static constexpr size_t   hdr_table_enc     (3);
  static constexpr size_t   hdr_size          (4);
  static constexpr uint8_t  hdr_version_1     (1);
  static constexpr size_t   entry_size        (8);
  static constexpr size_t   entry_location    (0);
  static constexpr size_t   entry_fde         (4);

  // Count and table encodings. These are the only encodings the linkers
  // emit and find_function() depends on them.
  //
  static constexpr uint8_t  count_encoding (format_udata4);
  static constexpr uint8_t  table_encoding (apply_datarel | format_sdata4);

  // .eh_frame CIE and FDE layout. The length excludes the length field
  // itself and the all-ones length marks the 64-bit format.
  //
  static constexpr size_t   frame_length   (0);
  static constexpr size_t   frame_id       (4); // CIE id or CIE pointer.
  static constexpr size_t   cie_version    (8);
  static constexpr size_t   fde_begin      (8);
  static constexpr size_t   fde_range      (12);
  static constexpr size_t   fde_size       (16); // Through the range field.
  static constexpr uint32_t length_64      (0xFFFFFFFF);
  static constexpr uint32_t cie_id         (0);
  static constexpr uint8_t  cie_version_1  (1);
  static constexpr uint8_t  cie_version_3  (3);

  // Return true if [o, o + n) lies within the bytes. Note that the offset
  // can come from the image, so the check is written to be overflow-safe.
  //
  static constexpr bool
  within (bytes b, size_t o, size_t n) noexcept
  {
    return o <= b.size () && b.size () - o >= n;
  }

  // Return the encoded value size or 0 if the encoding is LEB128 or
  // unknown.
  //
  static constexpr size_t
  encoded_size (uint8_t e) noexcept
  {
    switch (e & format_mask)
    {
    case format_absptr: return 4; // ELF32.
    case format_udata2:
    case format_sdata2: return 2;
    case format_udata4:
    case format_sdata4: return 4;
    case format_udata8:
    case format_sdata8: return 8;
    }

    return 0;
  }

  // Return true if the encoding is a direct 32-bit PC-relative pointer. This
  // is the encoding the linkers use for i386 FDE pointers and the only one
  // that is independent of the load address. An absolute pointer is
  // relocated by the dynamic linker to the actual load address and cannot
  // be resolved from the image bytes.
  //
  static constexpr bool
  relative_pointer (uint8_t e) noexcept
  {
    return (e & encoding_indirect) == 0 &&
           (e & apply_mask) == apply_pcrel &&
           encoded_size (e) == 4;
  }

  // Find the program header table of an i386 shared object and verify that
  // it is mapped at the image base (see parse_elf()). On success, set the
  // table offset and the number of entries.
  //
  static elf_outcome
  find_program (bytes b, size_t& t, uint16_t& n) noexcept
  {
    if (!within (b, 0, header_size))
      return elf_outcome::truncated;

    const uint8_t* p (b.data ());

    if (load32 (p) != ident_magic ||
        p[ident_class] != class_32 ||
        p[ident_data] != data_lsb ||
        p[ident_version] != version_current)
      return elf_outcome::ident;

    if (load16 (p + header_type) != type_dyn ||
        load16 (p + header_machine) != machine_386)
      return elf_outcome::machine;

    t = load32 (p + header_phoff);
    n = load16 (p + header_phnum);

    // PN_XNUM moves the real count into the section headers, which the
    // dynamic linker doesn't map.
    //
    if (load16 (p + header_phentsize) != program_size ||
        n == phnum_extended)
      return elf_outcome::headers;

    size_t e (static_cast<size_t> (n) * program_size);

    if (!within (b, t, e))
      return elf_outcome::truncated;

    e += t;

    for (size_t i (0); i != n; ++i)
    {
      const uint8_t* h (p + t + i * program_size);

      if (load32 (h + program_type) == type_load &&
          load32 (h + program_offset) == 0 &&
          load32 (h + program_vaddr) == 0 &&
          load32 (h + program_filesz) >= e &&
          (load32 (h + program_flags) & flag_read) != 0)
        return elf_outcome::valid;
    }

    return elf_outcome::headers;
  }

  const char*
  elf_outcome_name (elf_outcome o) noexcept
  {
    switch (o)
    {
    case elf_outcome::valid:     return "valid";
    case elf_outcome::truncated: return "truncated";
    case elf_outcome::ident:     return "ident";
    case elf_outcome::machine:   return "machine";
    case elf_outcome::headers:   return "headers";
    case elf_outcome::segments:  return "segments";
    case elf_outcome::functions: return "functions";
    case elf_outcome::encoding:  return "encoding";
    }

    LIBCOMPANION_UNREACHABLE ();
  }

  uint32_t
  elf_image_size (bytes b) noexcept
  {
    size_t   t;
    uint16_t n;

    if (find_program (b, t, n) != elf_outcome::valid)
      return 0;

    // Calculate in 64 bits to detect an end past 4G, which is invalid.
    //
    uint64_t r (0);

    for (size_t i (0); i != n; ++i)
    {
      const uint8_t* h (b.data () + t + i * program_size);

      if (load32 (h + program_type) != type_load)
        continue;

      uint64_t e (uint64_t (load32 (h + program_vaddr)) +
                  load32 (h + program_memsz));

      if (e > r)
        r = e;
    }

    return r <= numeric_limits<uint32_t>::max ()
      ? static_cast<uint32_t> (r)
      : 0;
  }

  const elf_segment*
  find_segment (const elf_image& x, uint32_t a, size_t n) noexcept
  {
    for (uint16_t i (0); i != x.segment_count; ++i)
    {
      const elf_segment& s (x.segments[i]);

      if (a >= s.address &&
          a - s.address <= s.size &&
          s.size - (a - s.address) >= n)
        return &s;
    }

    return nullptr;
  }

  // Check that [a, a + n) is inside some readable segment.
  //
  static bool
  readable (const elf_image& x, uint32_t a, size_t n) noexcept
  {
    return find_segment (x, a, n) != nullptr;
  }

  // Parse the .eh_frame_hdr at [a, a + n) and set the image search table.
  //
  static elf_outcome
  parse_header (elf_image& x, uint32_t a, uint32_t n) noexcept
  {
    if (n < hdr_size || !readable (x, a, n))
      return elf_outcome::functions;

    const uint8_t* p (x.data.data () + a);

    if (p[hdr_version] != hdr_version_1)
      return elf_outcome::encoding;

    uint8_t fe (p[hdr_frame_ptr_enc]);
    uint8_t ce (p[hdr_count_enc]);
    uint8_t te (p[hdr_table_enc]);

    // A header without the count or the table is valid and has no
    // functions.
    //
    if (ce == encoding_omit || te == encoding_omit)
      return elf_outcome::valid;

    // The .eh_frame pointer is used by the unwinder for a linear search
    // fallback. We skip it, which requires its size.
    //
    size_t fs (fe == encoding_omit ? 0 : encoded_size (fe));

    if ((fe != encoding_omit && fs == 0) ||
        ce != count_encoding ||
        te != table_encoding)
      return elf_outcome::encoding;

    size_t o (hdr_size + fs);

    if (o > n || n - o < 4)
      return elf_outcome::functions;

    uint32_t c (load32 (p + o));
    o += 4;

    if (c > (n - o) / entry_size)
      return elf_outcome::functions;

    x.header = a;
    x.functions = a + static_cast<uint32_t> (o);
    x.function_count = c;

    return elf_outcome::valid;
  }

  elf_outcome
  parse_elf (bytes b, elf_image& x) noexcept
  {
    size_t      t;
    uint16_t    n;
    elf_outcome r (find_program (b, t, n));

    if (r != elf_outcome::valid)
      return r;

    // Note that find_program() verified that the program header table is
    // within the bytes.
    //
    const uint8_t* p (b.data ());

    x.data = b;
    x.header = 0;
    x.functions = 0;
    x.function_count = 0;
    x.segment_count = 0;

    uint32_t ea (0), es (0); // .eh_frame_hdr address and size, if any.
    bool     eh (false);

    for (size_t i (0); i != n; ++i)
    {
      const uint8_t* h (p + t + i * program_size);
      uint32_t       ht (load32 (h + program_type));

      if (ht == type_eh_frame)
      {
        eh = true;
        ea = load32 (h + program_vaddr);
        es = load32 (h + program_memsz);
        continue;
      }

      uint32_t f (load32 (h + program_flags));

      if (ht != type_load || (f & flag_read) == 0)
        continue;

      uint32_t va (load32 (h + program_vaddr));
      uint32_t ms (load32 (h + program_memsz));

      // The segment end must also fit into 32 bits so that image address
      // arithmetic never wraps.
      //
      if (!within (b, va, ms) ||
          uint64_t (va) + ms > numeric_limits<uint32_t>::max () ||
          x.segment_count == elf_segment_capacity)
        return elf_outcome::segments;

      x.segments[x.segment_count++] = elf_segment {
        va, ms, (f & flag_write) != 0, (f & flag_execute) != 0};
    }

    // An image without PT_GNU_EH_FRAME is valid and has no functions.
    //
    if (eh)
      r = parse_header (x, ea, es);

    LIBCOMPANION_ASSERT (x.segment_count <= n);
    return r;
  }

  // Bounded forward-only CIE reader.
  //
  struct cursor
  {
    const uint8_t* data;
    size_t         position;
    size_t         end;
  };

  // Read the next byte. Return false if at the end.
  //
  static bool
  next (cursor& c, uint8_t& v) noexcept
  {
    if (c.position == c.end)
      return false;

    v = c.data[c.position++];
    return true;
  }

  // Skip n bytes. Return false if fewer are left.
  //
  static bool
  skip (cursor& c, size_t n) noexcept
  {
    if (c.end - c.position < n)
      return false;

    c.position += n;
    return true;
  }

  // Read an unsigned LEB128 value that fits into 32 bits. This is
  // sufficient for all the CIE values we read.
  //
  static bool
  next_uleb (cursor& c, uint32_t& v) noexcept
  {
    v = 0;

    for (uint32_t s (0); s < 32; s += 7)
    {
      uint8_t b;
      if (!next (c, b))
        return false;

      v |= static_cast<uint32_t> (b & 0x7F) << s;

      if ((b & 0x80) == 0)
        return s != 28 || b <= 0x0F;
    }

    return false;
  }

  // Skip a signed or unsigned LEB128 value of up to 32 bits.
  //
  static bool
  skip_leb (cursor& c) noexcept
  {
    for (size_t i (0); i != 5; ++i)
    {
      uint8_t b;
      if (!next (c, b))
        return false;

      if ((b & 0x80) == 0)
        return true;
    }

    return false;
  }

  // Extract the FDE pointer encoding from the augmentation of the CIE at the
  // specified address. Return false if the CIE has no 'R' augmentation or
  // contains anything we don't recognize.
  //
  static bool
  cie_encoding (const elf_image& x, uint32_t a, uint8_t& e) noexcept
  {
    if (!readable (x, a, cie_version + 1))
      return false;

    const uint8_t* p (x.data.data () + a);
    uint32_t       l (load32 (p + frame_length));

    if (l == length_64 ||
        l < cie_version - frame_id + 1 ||
        !readable (x, a, size_t (l) + frame_id) ||
        load32 (p + frame_id) != cie_id)
      return false;

    uint8_t v (p[cie_version]);

    if (v != cie_version_1 && v != cie_version_3)
      return false;

    cursor c {p, cie_version + 1, size_t (l) + frame_id};

    // Augmentation string. It must start with 'z', which marks the presence
    // of the augmentation data that specifies the pointer encoding.
    //
    const char* s (reinterpret_cast<const char*> (p + c.position));
    const void* z (memchr (s, '\0', c.end - c.position));

    if (z == nullptr || *s != 'z')
      return false;

    size_t sn (static_cast<size_t> (static_cast<const char*> (z) - s));
    c.position += sn + 1;

    // Skip the code and data alignment factors and the return address
    // register (a byte in version 1 and ULEB128 in version 3). Then read the
    // augmentation data length.
    //
    uint32_t an;
    if (!skip_leb (c) ||
        !skip_leb (c) ||
        !(v == cie_version_1 ? skip (c, 1) : skip_leb (c)) ||
        !next_uleb (c, an))
      return false;

    // Walk the augmentation data, which has one item per letter after 'z'.
    //
    if (c.end - c.position < an)
      return false;

    c.end = c.position + an;

    for (size_t i (1); i != sn; ++i)
    {
      switch (s[i])
      {
      case 'R':
        {
          return next (c, e);
        }
      case 'P':
        {
          // Personality routine encoding followed by the pointer.
          //
          uint8_t pe;
          if (!next (c, pe) ||
              encoded_size (pe) == 0 ||
              !skip (c, encoded_size (pe)))
            return false;

          break;
        }
      case 'L':
        {
          // LSDA pointer encoding (the pointers themselves are in the
          // FDEs).
          //
          if (!skip (c, 1))
            return false;

          break;
        }
      case 'S': // Signal frame.
      case 'B': // AArch64 B key.
        {
          break;
        }
      default:
        {
          return false;
        }
      }
    }

    return false;
  }

  // Read the function range from the FDE at the specified address and
  // verify that the function start matches the search table entry.
  //
  static bool
  function_range (const elf_image& x,
                  uint32_t a,
                  uint32_t begin,
                  uint32_t& range) noexcept
  {
    if (!readable (x, a, fde_size))
      return false;

    const uint8_t* p (x.data.data () + a);
    uint32_t       l (load32 (p + frame_length));
    uint32_t       ci (load32 (p + frame_id));

    // The CIE pointer is the backward distance from the pointer field to
    // the CIE. A zero value identifies a CIE.
    //
    if (l == length_64 ||
        l < fde_size - frame_id ||
        ci <= frame_id ||
        ci - frame_id > a)
      return false;

    uint8_t e;
    if (!cie_encoding (x, a - (ci - static_cast<uint32_t> (frame_id)), e) ||
        !relative_pointer (e))
      return false;

    // PC-relative arithmetic modulo 2^32, the same as for 32-bit process
    // addresses.
    //
    uint32_t b (a + static_cast<uint32_t> (fde_begin) +
                load32 (p + fde_begin));

    if (b != begin)
      return false;

    range = load32 (p + fde_range);
    return true;
  }

  // Resolve the datarel search table value at offset o. Return false if the
  // result is outside the 32-bit address space.
  //
  static bool
  entry_address (const elf_image& x, size_t o, uint32_t& a) noexcept
  {
    int64_t v (int64_t (x.header) +
               static_cast<int32_t> (load32 (x.data.data () + o)));

    if (v < 0 || v > int64_t (numeric_limits<uint32_t>::max ()))
      return false;

    a = static_cast<uint32_t> (v);
    return true;
  }

  elf_function
  find_function (const elf_image& x, uint32_t a) noexcept
  {
    elf_function r {0, 0};

    LIBCOMPANION_POST (r.address == 0 ||
                       (a >= r.address && a - r.address < r.size));

    LIBCOMPANION_PRE (x.function_count == 0 ||
                      readable (x,
                                    x.functions,
                                    size_t (x.function_count) * entry_size));

    // Binary search for the last entry that starts at or before the
    // address. The table is sorted by the signed header-relative offsets, so
    // compare these offsets as 64-bit values, which cannot wrap.
    //
    const uint8_t* t (x.data.data () + x.functions);
    int64_t        d (int64_t (a) - int64_t (x.header));

    auto location_of = [t] (uint32_t i) -> int64_t
    {
      return static_cast<int32_t> (
        load32 (t + size_t (i) * entry_size + entry_location));
    };

    uint32_t l (0), h (x.function_count);

    while (l != h)
    {
      uint32_t m (l + (h - l) / 2);

      if (location_of (m) <= d)
        l = m + 1;
      else
        h = m;
    }

    if (l == 0)
      return r;

    size_t   o (x.functions + size_t (l - 1) * entry_size);
    uint32_t b, f, n;

    if (!entry_address (x, o + entry_location, b) ||
        !entry_address (x, o + entry_fde, f) ||
        !function_range (x, f, b, n))
      return r;

    // The search guarantees that the address is at or past the start.
    //
    LIBCOMPANION_ASSERT (a >= b);

    if (a - b < n)
      r = elf_function {b, n};

    return r;
  }

  // PC thunk encoding: mov r32, [esp] (ModRM with mod 00 and r/m 100
  // followed by the [esp] SIB byte) and ret.
  //
  static constexpr uint8_t thunk_mov       (0x8B);
  static constexpr uint8_t thunk_modrm     (0x04);
  static constexpr uint8_t thunk_sib       (0x24);
  static constexpr uint8_t thunk_ret       (0xC3);
  static constexpr size_t  thunk_size      (4);
  static constexpr uint8_t modrm_reg_mask  (0x38);
  static constexpr uint8_t modrm_reg_shift (3);
  static constexpr uint8_t modrm_mod_rm    (0xC7); // Mod and r/m fields.

  bool
  pc_thunk (const elf_image& x, uint32_t a, i386_register& r) noexcept
  {
    const elf_segment* s (find_segment (x, a, thunk_size));

    if (s == nullptr || !s->executable)
      return false;

    const uint8_t* p (x.data.data () + a);

    if (p[0] != thunk_mov ||
        (p[1] & modrm_mod_rm) != thunk_modrm ||
        p[2] != thunk_sib ||
        p[3] != thunk_ret)
      return false;

    r = static_cast<i386_register> ((p[1] & modrm_reg_mask) >>
                                    modrm_reg_shift);

    return r != i386_register::esp;
  }
}
