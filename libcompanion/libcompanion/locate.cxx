// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/locate.hxx>

#include <limits>  // numeric_limits
#include <cstring> // memchr()

#include <libcompanion/endian.hxx>
#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  // Fixed-capacity set of RVAs (anchors or candidate functions). An insert
  // past the capacity only sets the overflow flag.
  //
  template <size_t N>
  struct rva_set
  {
    uint32_t items[N];
    uint32_t count    = 0;
    bool     overflow = false;
  };

  template <size_t N>
  static bool
  contains (const rva_set<N>& s, uint32_t v) noexcept
  {
    for (uint32_t i (0); i != s.count; ++i)
    {
      if (s.items[i] == v)
        return true;
    }

    return false;
  }

  template <size_t N>
  static void
  insert (rva_set<N>& s, uint32_t v) noexcept
  {
    if (contains (s, v))
      return;

    if (s.count == N)
    {
      s.overflow = true;
      return;
    }

    s.items[s.count++] = v;
  }

  using anchor_set    = rva_set<anchor_capacity>;
  using candidate_set = rva_set<candidate_capacity>;

  // Add the address of every occurrence of the anchor, including its
  // terminating NUL, in [a, a + n) of the image bytes.
  //
  static void
  find_anchors (bytes b, uint32_t a, uint32_t n, anchor_set& r) noexcept
  {
    LIBCOMPANION_PRE (a <= b.size () && b.size () - a >= n);

    // Matching the NUL excludes longer names that start with the anchor.
    // The view can include it since the literal is NUL-terminated.
    //
    string_view s (send_frame_anchor.data (), send_frame_anchor.size () + 1);
    string_view d (reinterpret_cast<const char*> (b.data () + a), n);

    for (size_t p (d.find (s)); p != string_view::npos; p = d.find (s, p + 1))
      insert (r, a + static_cast<uint32_t> (p));
  }

  // Find the anchors in the non-executable sections.
  //
  static void
  find_anchors (const pe_image& x, anchor_set& r) noexcept
  {
    for (uint16_t i (0); i != x.section_count; ++i)
    {
      const pe_section& s (x.sections[i]);

      if (!s.executable)
        find_anchors (x.data, s.rva, s.size, r);
    }
  }

  // x64 anchor reference: lea r64, [rip + disp32]. That is, a REX.W prefix,
  // the opcode, ModRM with mod 00 and r/m 101, and the 32-bit displacement.
  //
  static constexpr uint8_t lea_opcode (0x8D);
  static constexpr size_t  lea_size   (7);

  static constexpr bool
  lea_rex (uint8_t b) noexcept
  {
    return (b & 0xF8) == 0x48;
  }

  static constexpr bool
  lea_rip (uint8_t modrm) noexcept
  {
    return (modrm & 0xC7) == 0x05;
  }

  // If p (at the specified RVA) points to the lea reference, then set t to
  // the RVA that it loads and return true.
  //
  static bool
  lea_target (const uint8_t* p, uint32_t rva, uint32_t& t) noexcept
  {
    if (!lea_rex (p[0]) || p[1] != lea_opcode || !lea_rip (p[2]))
      return false;

    // The displacement is relative to the end of the instruction. Calculate
    // in 64 bits so that a target outside [0, 4G) is rejected and cannot
    // wrap into a valid RVA.
    //
    int64_t v (int64_t (rva) +
               int64_t (lea_size) +
               static_cast<int32_t> (load32 (p + 3)));

    if (v < 0 || v > int64_t (numeric_limits<uint32_t>::max ()))
      return false;

    t = static_cast<uint32_t> (v);
    return true;
  }

  // Scan the code section for anchor references and add each function that
  // has a reference close to its entry to the candidates.
  //
  static void
  scan_section (const pe_image& x,
                const pe_section& s,
                const anchor_set& a,
                candidate_set& c,
                uint32_t& references) noexcept
  {
    LIBCOMPANION_PRE (s.executable);
    LIBCOMPANION_PRE (s.rva <= x.data.size () &&
                      x.data.size () - s.rva >= s.size);

    if (s.size < lea_size)
      return;

    const uint8_t* b (x.data.data () + s.rva);

    // Use memchr() to skip to the next opcode byte, which is much faster
    // than checking every offset. The opcode is at offset 1 of the
    // instruction, so the instruction is [i - 1, i - 1 + lea_size) and n is
    // one past the last possible opcode offset.
    //
    size_t n (s.size - lea_size + 2);

    for (size_t i (1); i < n; ++i)
    {
      const void* p (memchr (b + i, lea_opcode, n - i));

      if (p == nullptr)
        break;

      i = static_cast<size_t> (static_cast<const uint8_t*> (p) - b);

      uint32_t r (s.rva + static_cast<uint32_t> (i - 1)), t;

      if (!lea_target (b + i - 1, r, t) || !contains (a, t))
        continue;

      ++references;

      uint32_t f (find_function (x, r));

      // Skip a reference outside any function (no unwind information) and
      // one before its function's entry (a cold block placed ahead of it).
      //
      if (f == 0 || r < f || r - f > max_scope_distance)
        continue;

      insert (c, f);
    }
  }

  // i386 GOT loads.
  //
  // Position-independent code obtains the return address of a call and
  // adds the distance from it to the GOT. GCC and Clang generate the
  // following sequences, respectively:
  //
  // call __x86.get_pc_thunk.<r>    (the thunk is mov <r>, [esp]; ret)
  // add  <r>, GOT - <return>
  //
  // call <return>
  // pop  <r>
  // add  <r>, GOT - <return>
  //
  // We match these sequences exactly (including the thunk) at any offset in
  // the function. With shrink-wrapping the load can be anywhere in the
  // function (in the Steam builds we have examined, about a fifth of the
  // functions load the GOT more than 64 bytes from the entry), so we scan
  // for the byte pattern, which requires no instruction decoding. In these
  // builds all the loads yield the same GOT.
  //
  static constexpr uint8_t call_opcode  (0xE8); // call rel32
  static constexpr size_t  call_size    (5);
  static constexpr uint8_t pop_opcode   (0x58); // pop r32 (+r)
  static constexpr uint8_t add_opcode   (0x81); // add r/m32, imm32 (/0)
  static constexpr uint8_t add_modrm    (0xC0); // mod 11, /0 (+r)
  static constexpr size_t  add_size     (6);

  // i386 anchor reference: lea r32, [r32 + disp32]. That is, ModRM with mod
  // 10 and any r/m except 100 (which would add a SIB byte).
  //
  static constexpr size_t  lea32_size (6);

  static constexpr uint8_t modrm_mod_mask (0xC0);
  static constexpr uint8_t modrm_mod_10   (0x80);
  static constexpr uint8_t modrm_rm_mask  (0x07);
  static constexpr uint8_t modrm_rm_sib   (0x04);

  // Return true if an executable segment covers [a, a + n).
  //
  static bool
  executable (const elf_image& x, uint32_t a, size_t n) noexcept
  {
    const elf_segment* s (find_segment (x, a, n));
    return s != nullptr && s->executable;
  }

  // Return true if a writable segment covers [a, a + n).
  //
  static bool
  writable (const elf_image& x, uint32_t a, size_t n) noexcept
  {
    const elf_segment* s (find_segment (x, a, n));
    return s != nullptr && s->writable;
  }

  // If a GOT load sequence starts at the specified address, then set g to
  // the GOT address and return true.
  //
  static bool
  got_load (const elf_image& x, uint32_t a, uint32_t& g) noexcept
  {
    if (!executable (x, a, call_size + add_size))
      return false;

    const uint8_t* p (x.data.data () + a);

    if (p[0] != call_opcode)
      return false;

    // Return address. It and the call target are calculated modulo 2^32,
    // the same as on the CPU.
    //
    uint32_t       ra (a + static_cast<uint32_t> (call_size));
    uint32_t       d  (load32 (p + 1));
    const uint8_t* q  (p + call_size);
    i386_register  r;

    if (d == 0)
    {
      if (!executable (x, a, call_size + 1 + add_size) ||
          q[0] < pop_opcode ||
          q[0] - pop_opcode > 7)
        return false;

      r = static_cast<i386_register> (q[0] - pop_opcode);

      if (r == i386_register::esp)
        return false;

      ++q;
    }
    else if (!pc_thunk (x, ra + d, r))
      return false;

    if (q[0] != add_opcode ||
        q[1] != (add_modrm | static_cast<uint8_t> (r)))
      return false;

    g = ra + load32 (q + 2);
    return true;
  }

  // Scan [a, e) for a GOT load. If one is found, then advance a to it, set g
  // to the GOT address, and return true.
  //
  static bool
  next_got_load (const elf_image& x,
                 uint32_t& a,
                 uint32_t e,
                 uint32_t& g) noexcept
  {
    LIBCOMPANION_PRE (a <= e && executable (x, a, e - a));

    const uint8_t* b (x.data.data ());

    while (a != e)
    {
      // Skip to the next call opcode (see scan_section()).
      //
      const void* p (memchr (b + a, call_opcode, e - a));

      if (p == nullptr)
        break;

      a = static_cast<uint32_t> (static_cast<const uint8_t*> (p) - b);

      if (got_load (x, a, g))
        return true;

      ++a;
    }

    return false;
  }

  // Determine the GOT address from the first GOT load that is inside a
  // function. Return false if there is no such load or if the GOT is not in
  // a writable segment.
  //
  static bool
  find_got (const elf_image& x, uint32_t& g) noexcept
  {
    for (uint16_t i (0); i != x.segment_count; ++i)
    {
      const elf_segment& s (x.segments[i]);

      if (!s.executable)
        continue;

      uint32_t e (s.address + s.size);

      for (uint32_t a (s.address); next_got_load (x, a, e, g); ++a)
      {
        if (find_function (x, a).address != 0)
          return writable (x, g, 4);
      }
    }

    return false;
  }

  // Scan the code segment for anchor references through the GOT and add
  // each referencing function that loads the GOT itself to the candidates.
  //
  static void
  scan_segment (const elf_image& x,
                const elf_segment& s,
                const anchor_set& a,
                uint32_t got,
                candidate_set& c,
                uint32_t& references) noexcept
  {
    LIBCOMPANION_PRE (s.executable);

    if (s.size < lea32_size)
      return;

    const uint8_t* b (x.data.data () + s.address);

    // The instruction is [i, i + lea32_size) and n is one past the last
    // possible opcode offset.
    //
    size_t n (s.size - lea32_size + 1);

    for (size_t i (0); i < n; ++i)
    {
      const void* p (memchr (b + i, lea_opcode, n - i));

      if (p == nullptr)
        break;

      i = static_cast<size_t> (static_cast<const uint8_t*> (p) - b);

      uint8_t m (b[i + 1]);

      if ((m & modrm_mod_mask) != modrm_mod_10 ||
          (m & modrm_rm_mask) == modrm_rm_sib)
        continue;

      // The target is calculated modulo 2^32, the same as on the CPU.
      //
      if (!contains (a, got + load32 (b + i + 2)))
        continue;

      ++references;

      uint32_t     r (s.address + static_cast<uint32_t> (i));
      elf_function f (find_function (x, r));

      if (f.address == 0 || !executable (x, f.address, f.size))
        continue;

      // Make sure the function loads the GOT itself. Note that the load can
      // be anywhere in the function, before or after the reference.
      //
      uint32_t fa (f.address), g;

      if (!next_got_load (x, fa, f.address + f.size, g) || g != got)
        continue;

      insert (c, f.address);
    }
  }

  // Classify the search given the reference count and the candidates.
  //
  static void
  decide (locate_result& r, const candidate_set& c) noexcept
  {
    r.candidates = c.count;

    if (r.references == 0)
      r.outcome = locate_outcome::no_reference;
    else if (c.count == 0)
      r.outcome = locate_outcome::no_candidate;
    else if (c.count != 1 || c.overflow)
      r.outcome = locate_outcome::ambiguous;
    else
    {
      r.outcome = locate_outcome::found;
      r.rva = c.items[0];
    }
  }

  const char*
  locate_outcome_name (locate_outcome o) noexcept
  {
    switch (o)
    {
    case locate_outcome::found:        return "found";
    case locate_outcome::no_anchor:    return "no-anchor";
    case locate_outcome::no_got:       return "no-got";
    case locate_outcome::no_reference: return "no-reference";
    case locate_outcome::no_candidate: return "no-candidate";
    case locate_outcome::ambiguous:    return "ambiguous";
    }

    LIBCOMPANION_UNREACHABLE ();
  }

  locate_result
  locate_send_frame (const pe_image& x) noexcept
  {
    locate_result r {locate_outcome::no_anchor, 0, 0, 0};

    LIBCOMPANION_POST (r.outcome != locate_outcome::found ||
                       (r.rva != 0 && r.candidates == 1));

    anchor_set a;
    find_anchors (x, a);

    if (a.count == 0)
      return r;

    // Duplicate anchors come from object files whose strings the linker did
    // not merge. More than anchor_capacity of them make the result
    // ambiguous.
    //
    if (a.overflow)
    {
      r.outcome = locate_outcome::ambiguous;
      return r;
    }

    candidate_set c;

    for (uint16_t i (0); i != x.section_count; ++i)
    {
      const pe_section& s (x.sections[i]);

      if (s.executable)
        scan_section (x, s, a, c, r.references);
    }

    decide (r, c);
    return r;
  }

  locate_result
  locate_send_frame (const elf_image& x) noexcept
  {
    locate_result r {locate_outcome::no_anchor, 0, 0, 0};

    LIBCOMPANION_POST (r.outcome != locate_outcome::found ||
                       (r.rva != 0 && r.candidates == 1));

    anchor_set a;

    for (uint16_t i (0); i != x.segment_count; ++i)
    {
      const elf_segment& s (x.segments[i]);

      if (!s.executable)
        find_anchors (x.data, s.address, s.size, a);
    }

    if (a.count == 0)
      return r;

    if (a.overflow)
    {
      r.outcome = locate_outcome::ambiguous;
      return r;
    }

    uint32_t got;

    if (!find_got (x, got))
    {
      r.outcome = locate_outcome::no_got;
      return r;
    }

    candidate_set c;

    for (uint16_t i (0); i != x.segment_count; ++i)
    {
      const elf_segment& s (x.segments[i]);

      if (s.executable)
        scan_segment (x, s, a, got, c, r.references);
    }

    decide (r, c);
    return r;
  }
}
