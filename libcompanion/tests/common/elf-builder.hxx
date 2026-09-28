// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>
#include <vector>
#include <cstring>   // memcpy()
#include <sstream>
#include <algorithm> // sort(), max()
#include <stdexcept> // runtime_error

#include <libcompanion/endian.hxx>

#undef NDEBUG
#include <cassert>

// Mapped i386 ELF32 shared object builder for the elf, locate, and detour
// tests.
//
// The image is described with one command per line in any order. All the
// numbers are in hex. The commands are:
//
// segment r|rw|rx|- <address> <size>
//
//   Add a loadable segment. The '-' permissions make it non-readable.
//
// function <begin> <end> [personality]
//
//   Add an FDE and its search table entry. The FDE refers to the "zPLR" CIE
//   if personality is specified and to the "zR" CIE otherwise.
//
// byte <address> <byte>...
//
//   Overwrite the laid out image bytes at the address.
//
// truncate <size>
//
//   Truncate the image to the size.
//
namespace elf_builder
{
  using namespace std;
  using namespace companion;

  // Image layout.
  //
  // The layout is fixed so that the tests can patch specific bytes. The ELF
  // header is at 0 followed by the 32-byte program headers starting at 34:
  //
  // 0      headers segment (r, [0, 1000))
  // 1...   described segments in the description order
  // n + 1  unwind segment (r, after the last described segment)
  // n + 2  PT_GNU_EH_FRAME for .eh_frame_hdr
  //
  // The unwind segment starts at the first page boundary U past the
  // described segments and contains:
  //
  // U       .eh_frame_hdr version and the three encodings
  // U + 4   .eh_frame pointer
  // U + 8   search table entry count
  // U + c   search table
  // F       .eh_frame (the next 4-byte boundary after the table)
  // F       "zR" CIE (20 bytes)
  // F + 14  "zPLR" CIE (28 bytes)
  // ...     FDEs sorted by function start and the terminator
  //

  using buffer = vector<uint8_t>;

  struct segment_item
  {
    uint32_t flags; // PF_*
    uint32_t address;
    uint32_t size;
  };

  struct function_item
  {
    uint32_t begin;
    uint32_t end;
    bool     personality;
  };

  struct write_item
  {
    uint32_t address;
    buffer   data;
  };

  // Image description.
  //
  struct description
  {
    vector<segment_item>  segments;
    vector<function_item> functions;
    vector<write_item>    writes;
    size_t                truncation = 0;
    bool                  truncated  = false;
  };

  inline const uint32_t header_end   (0x1000); // Lowest segment address.
  inline const uint32_t program_base (0x34);
  inline const uint32_t program_size (0x20);

  inline const uint32_t flag_read (4);

  // .eh_frame entry sizes (see the layout above).
  //
  inline const uint32_t cie_size       (20);
  inline const uint32_t cie_plr_size   (28);
  inline const uint32_t fde_size       (20);
  inline const uint32_t fde_plr_size   (24);

  inline void
  store16 (uint8_t* p, uint16_t v)
  {
    p[0] = static_cast<uint8_t> (v);
    p[1] = static_cast<uint8_t> (v >> 8);
  }

  inline uint32_t
  align (uint32_t v, uint32_t a)
  {
    return (v + a - 1) & ~(a - 1);
  }

  inline void
  write (buffer& b, uint32_t a, const buffer& d)
  {
    if (a > b.size () || b.size () - a < d.size ())
      throw runtime_error ("write outside of the image at " + to_string (a));

    memcpy (b.data () + a, d.data (), d.size ());
  }

  inline void
  write_program (uint8_t* p,
                 uint32_t type,
                 uint32_t address,
                 uint32_t size,
                 uint32_t flags)
  {
    store32 (p, type);
    store32 (p + 4, address);  // p_offset (equals p_vaddr).
    store32 (p + 8, address);  // p_vaddr
    store32 (p + 12, address); // p_paddr
    store32 (p + 16, size);    // p_filesz
    store32 (p + 20, size);    // p_memsz
    store32 (p + 24, flags);
    store32 (p + 28, 0x1000);  // p_align
  }

  // Write the "zR" and "zPLR" CIEs. Both use code alignment 1, data
  // alignment -4, and return address register 8 (the GCC values for i386)
  // and encode FDE pointers as PC-relative sdata4.
  //
  inline void
  write_cies (uint8_t* p)
  {
    const uint8_t zr[] = {
      0x10, 0, 0, 0,     // Length.
      0, 0, 0, 0,        // CIE id.
      1, 'z', 'R', 0,    // Version and augmentation.
      1, 0x7C, 8,        // Alignment factors and return address register.
      1, 0x1B,           // Augmentation data: R.
      0, 0, 0};          // Padding (DW_CFA_nop).

    const uint8_t zplr[] = {
      0x18, 0, 0, 0,
      0, 0, 0, 0,
      1, 'z', 'P', 'L', 'R', 0,
      1, 0x7C, 8,
      7, 0x9B, 0, 0, 0, 0, 0x1B, 0x1B, // P (indirect pointer), L, and R.
      0, 0, 0};

    static_assert (sizeof (zr) == cie_size && sizeof (zplr) == cie_plr_size);

    memcpy (p, zr, sizeof (zr));
    memcpy (p + cie_size, zplr, sizeof (zplr));
  }

  // Write the FDE for the function at the image address a (p points to it)
  // referring to the CIE at c. Return the FDE size.
  //
  inline uint32_t
  write_fde (uint8_t* p, uint32_t a, uint32_t c, const function_item& f)
  {
    uint32_t n (f.personality ? fde_plr_size : fde_size);

    store32 (p, n - 4);
    store32 (p + 4, a + 4 - c);
    store32 (p + 8, f.begin - (a + 8)); // PC-relative, modulo 2^32.
    store32 (p + 12, f.end - f.begin);
    p[16] = f.personality ? 4 : 0;      // Augmentation data (NULL LSDA).

    return n;
  }

  // Build the image (headers, described segments, and unwind segment) and
  // apply the patches and truncation. Note that the described functions are
  // sorted by the start address as a side effect.
  //
  inline buffer
  build (description& d)
  {
    uint32_t e (header_end);

    for (const segment_item& s: d.segments)
    {
      if (s.address < header_end)
        throw runtime_error ("segment overlaps the headers");

      e = max (e, align (s.address + s.size, 0x1000));
    }

    sort (d.functions.begin (), d.functions.end (),
          [] (const function_item& x, const function_item& y)
          {
            return x.begin < y.begin;
          });

    uint32_t fn (static_cast<uint32_t> (d.functions.size ()));
    uint32_t u  (e);                             // .eh_frame_hdr
    uint32_t hn (12 + fn * 8);
    uint32_t f  (align (u + hn, 4));             // .eh_frame
    uint32_t fe (f + cie_size + cie_plr_size);   // First FDE.

    for (const function_item& x: d.functions)
      fe += x.personality ? fde_plr_size : fde_size;

    uint32_t n (fe + 4); // Terminator.

    buffer b (n, 0);
    uint8_t* p (b.data ());
    assert (p != nullptr);

    // ELF header.
    //
    uint32_t pn (static_cast<uint32_t> (d.segments.size ()) + 3);

    store32 (p, 0x464C457F);                    // "\x7F" "ELF"
    p[4] = 1;                                   // ELFCLASS32
    p[5] = 1;                                   // ELFDATA2LSB
    p[6] = 1;                                   // EV_CURRENT
    store16 (p + 16, 3);                        // ET_DYN
    store16 (p + 18, 3);                        // EM_386
    store32 (p + 20, 1);                        // EV_CURRENT
    store32 (p + 28, program_base);             // e_phoff
    store16 (p + 40, 0x34);                     // e_ehsize
    store16 (p + 42, uint16_t (program_size));  // e_phentsize
    store16 (p + 44, static_cast<uint16_t> (pn));

    if (program_base + pn * program_size > header_end)
      throw runtime_error ("too many segments");

    // Program headers.
    //
    uint8_t* h (p + program_base);

    write_program (h, 1, 0, header_end, flag_read);
    h += program_size;

    for (const segment_item& s: d.segments)
    {
      write_program (h, 1, s.address, s.size, s.flags);
      h += program_size;
    }

    write_program (h, 1, u, n - u, flag_read);
    write_program (h + program_size, 0x6474E550, u, hn, flag_read);

    // .eh_frame_hdr.
    //
    p[u] = 1;                               // Version.
    p[u + 1] = 0x1B;                        // .eh_frame pointer: pcrel sdata4.
    p[u + 2] = 0x03;                        // Count: udata4.
    p[u + 3] = 0x3B;                        // Table: datarel sdata4.
    store32 (p + u + 4, f - (u + 4));
    store32 (p + u + 8, fn);

    // .eh_frame.
    //
    write_cies (p + f);

    uint32_t a (f + cie_size + cie_plr_size);

    for (uint32_t i (0); i != fn; ++i)
    {
      const function_item& x (d.functions[i]);

      store32 (p + u + 12 + i * 8, x.begin - u);
      store32 (p + u + 16 + i * 8, a - u);

      a += write_fde (p + a, a, x.personality ? f + cie_size : f, x);
    }

    // Patches.
    //
    for (const write_item& w: d.writes)
      write (b, w.address, w.data);

    if (d.truncated)
    {
      if (d.truncation > b.size ())
        throw runtime_error ("truncation past the end of the image");

      b.resize (d.truncation);
    }

    return b;
  }

  // Description line split into words.
  //
  using words = vector<string>;

  inline words
  split (const string& l)
  {
    words w;
    istringstream is (l);

    for (string s; is >> s; )
      w.push_back (s);

    return w;
  }

  // Parse a 32-bit hex number.
  //
  inline uint32_t
  hex_number (const string& s)
  {
    size_t n (0);
    unsigned long v (0);

    try
    {
      v = stoul (s, &n, 16);
    }
    catch (const logic_error&) // invalid_argument, out_of_range
    {
      n = 0;
    }

    if (n == 0 || n != s.size () || v > 0xFFFFFFFF)
      throw runtime_error ("invalid number '" + s + '\'');

    return static_cast<uint32_t> (v);
  }

  // Verify the command argument count.
  //
  inline void
  arity (const words& w, size_t min, size_t max)
  {
    if (w.size () - 1 < min || w.size () - 1 > max)
      throw runtime_error ("invalid argument count for '" + w[0] + '\'');
  }

  inline uint32_t
  segment_flags (const string& s)
  {
    if (s == "r")  return 4;
    if (s == "rw") return 6;
    if (s == "rx") return 5;
    if (s == "-")  return 0;

    throw runtime_error ("invalid segment permissions '" + s + '\'');
  }

  // Parse the description line into the description. Return false if the
  // command is not recognized (the caller may handle its own commands).
  //
  inline bool
  parse (description& d, const words& w)
  {
    const string& c (w[0]);

    if (c == "segment")
    {
      arity (w, 3, 3);
      d.segments.push_back (segment_item {segment_flags (w[1]),
                                          hex_number (w[2]),
                                          hex_number (w[3])});
    }
    else if (c == "function")
    {
      arity (w, 2, 3);

      if (w.size () > 3 && w[3] != "personality")
        throw runtime_error ("invalid function option '" + w[3] + '\'');

      function_item f {hex_number (w[1]), hex_number (w[2]), w.size () > 3};

      if (f.end <= f.begin)
        throw runtime_error ("empty function at " + w[1]);

      d.functions.push_back (f);
    }
    else if (c == "byte")
    {
      arity (w, 2, 16);

      d.writes.push_back (write_item {hex_number (w[1]), buffer ()});

      for (size_t i (2); i != w.size (); ++i)
      {
        uint32_t v (hex_number (w[i]));

        if (v > 0xFF)
          throw runtime_error ("invalid byte '" + w[i] + '\'');

        d.writes.back ().data.push_back (static_cast<uint8_t> (v));
      }
    }
    else if (c == "truncate")
    {
      arity (w, 1, 1);
      d.truncation = hex_number (w[1]);
      d.truncated = true;
    }
    else
      return false;

    return true;
  }
}
