// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/detour.hxx>

#include <cstring> // memcpy(), memset()

#include <libcompanion/minhook/hde32.h>

#include <libcompanion/endian.hxx>
#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  static constexpr uint8_t jump_opcode (0xE9); // jmp rel32
  static constexpr uint8_t call_opcode (0xE8); // call rel32
  static constexpr size_t  call_size   (5);
  static constexpr uint8_t mov_opcode  (0xB8); // mov r32, imm32 (+r)
  static constexpr uint8_t push_opcode (0x68); // push imm32

  static_assert (call_size == jump_size);

  // Minimum number of readable bytes to pass to HDE.
  //
  // HDE takes no input size and may read up to 34 bytes for a single
  // instruction (16 bytes of prefixes including the opcode, then the second
  // opcode byte, ModRM, SIB, disp32, and 11 bytes of immediates) before
  // reporting a length of at most 15.
  //
  static constexpr size_t decode_window (64);

  static constexpr uint32_t
  value (i386_address a) noexcept
  {
    return static_cast<uint32_t> (a);
  }

  // Return a plus n modulo 2^32.
  //
  static constexpr i386_address
  offset (i386_address a, uint32_t n) noexcept
  {
    return i386_address (value (a) + n);
  }

  // Return true if the instruction is ud2 without prefixes and that is the
  // only error HDE reports.
  //
  // HDE's opcode table marks ud2 as invalid. GCC emits ud2 for
  // __builtin_trap() and after calls to noreturn functions, anywhere in a
  // function. In the Steam builds we have examined, accepting it makes most
  // of the otherwise undecodable functions decodable.
  //
  static bool
  ud2 (const hde32_instruction& h) noexcept
  {
    return h.flags == (HDE32_F_ERROR | HDE32_F_ERROR_OPCODE) &&
           h.opcode == 0x0F &&
           h.opcode2 == 0x0B;
  }

  // Decode the instruction at the image address a in a function that ends
  // at e within the segment. Return false if HDE reports an error other than
  // ud2 (see above) or if the instruction extends past e.
  //
  static bool
  decode (const elf_image& x,
          const elf_segment& s,
          uint32_t a,
          uint32_t e,
          hde32_instruction& h) noexcept
  {
    LIBCOMPANION_PRE (a >= s.address && a < e && e - s.address <= s.size);

    const uint8_t* p (x.data.data () + a);
    size_t         n (s.address + s.size - a); // Bytes left in segment.

    // Near the end of the segment, decode a zero-padded copy. The function
    // ends within the segment, so any instruction that includes the padding
    // extends past e and is refused below.
    //
    uint8_t w[decode_window];

    if (n < decode_window)
    {
      memcpy (w, p, n);
      memset (w + n, 0, decode_window - n);
      p = w;
    }

    hde32_disasm (p, &h);

    return ((h.flags & HDE32_F_ERROR) == 0 || ud2 (h)) && h.len <= e - a;
  }

  // Return true if the instruction may transfer control to somewhere other
  // than the next instruction. This includes direct, indirect, and far
  // branches, returns, interrupts, system calls, and ud2 (which raises an
  // exception).
  //
  static bool
  transfer (const hde32_instruction& h) noexcept
  {
    if ((h.flags & HDE32_F_RELATIVE) != 0)
      return true;

    if (h.opcode == 0x0F)
    {
      switch (h.opcode2)
      {
      case 0x05: // syscall
      case 0x07: // sysret
      case 0x0B: // ud2
      case 0x34: // sysenter
      case 0x35: // sysexit
        return true;
      }

      return false;
    }

    switch (h.opcode)
    {
    case 0x9A: // call ptr16:32
    case 0xEA: // jmp ptr16:32
    case 0xC2: // ret imm16
    case 0xC3: // ret
    case 0xCA: // retf imm16
    case 0xCB: // retf
    case 0xCC: // int3
    case 0xCD: // int imm8
    case 0xCE: // into
    case 0xCF: // iret
    case 0xF1: // int1
      return true;
    case 0xFF: // call, callf, jmp, jmpf r/m (/2 to /5)
      return h.modrm_reg >= 2 && h.modrm_reg <= 5;
    }

    return false;
  }

  // Calculate the process address that the direct branch at a jumps to.
  //
  static i386_address
  branch_target (const hde32_instruction& h, i386_address a) noexcept
  {
    LIBCOMPANION_PRE ((h.flags & HDE32_F_RELATIVE) != 0);

    // Displacement sign-extended to 32 bits.
    //
    uint32_t d;

    if ((h.flags & HDE32_F_IMM32) != 0)
      d = h.imm.imm32;
    else if ((h.flags & HDE32_F_IMM16) != 0)
      d = static_cast<uint32_t> (static_cast<int16_t> (h.imm.imm16));
    else
      d = static_cast<uint32_t> (static_cast<int8_t> (h.imm.imm8));

    uint32_t t (value (a) + h.len + d);

    // With the operand size prefix the CPU truncates the target to 16 bits.
    //
    if (h.p_66 != 0)
      t &= 0xFFFF;

    return i386_address (t);
  }

  // If the instruction at the image address a is a call that obtains the
  // program counter, then write its replacement to p and return true.
  //
  // A call to the next instruction leaves the return address on the stack,
  // which push imm32 reproduces. A call to a PC thunk leaves the return
  // address in the thunk's register, which mov r32, imm32 reproduces. Both
  // replacements are 5 bytes long, the same as the call.
  //
  static bool
  relocate_call (const elf_image& x,
                 const hde32_instruction& h,
                 uint32_t a,
                 i386_address base,
                 uint8_t* p) noexcept
  {
    // A prefixed call is longer than call_size and is not relocated.
    //
    if (h.opcode != call_opcode || h.len != call_size)
      return false;

    uint32_t      ra (a + static_cast<uint32_t> (call_size));
    i386_register r;

    if (h.imm.imm32 == 0)
      p[0] = push_opcode;
    else if (pc_thunk (x, ra + h.imm.imm32, r)) // Modulo 2^32.
      p[0] = static_cast<uint8_t> (mov_opcode + static_cast<uint8_t> (r));
    else
      return false;

    store32 (p + 1, value (offset (base, ra)));
    return true;
  }

  // Copy the instructions overwritten by the jump into the trampoline,
  // relocating a program counter call, and set the replaced size.
  //
  static detour_outcome
  copy_replaced (const elf_image& x,
                 const elf_segment& s,
                 const elf_function& f,
                 i386_address base,
                 trampoline& t) noexcept
  {
    uint32_t e (f.address + f.size);
    uint32_t n (0);

    while (n < jump_size)
    {
      uint32_t          a (f.address + n);
      hde32_instruction h;

      if (!decode (x, s, a, e, h))
        return detour_outcome::decode;

      // Every overwritten instruction starts before the end of the jump,
      // which bounds the total.
      //
      LIBCOMPANION_ASSERT (n + h.len <= max_replaced_size);

      uint8_t* p (t.code + n);

      if (!relocate_call (x, h, a, base, p))
      {
        if (transfer (h))
          return detour_outcome::branch;

        memcpy (p, x.data.data () + a, h.len);
      }

      n += h.len;
    }

    t.replaced = static_cast<uint8_t> (n);
    return detour_outcome::valid;
  }

  // Verify that no direct branch in the rest of the function targets the
  // overwritten bytes past the function start. A branch to the function
  // start lands on the jump and enters the hook, the same as a call.
  //
  static detour_outcome
  check_entries (const elf_image& x,
                 const elf_segment& s,
                 const elf_function& f,
                 i386_address base,
                 uint32_t n) noexcept
  {
    LIBCOMPANION_PRE (n >= jump_size && n <= f.size);

    uint32_t e (f.address + f.size);

    // HDE always reports a non-zero length, so the loop terminates.
    //
    for (uint32_t a (f.address + n); a != e; )
    {
      hde32_instruction h;

      if (!decode (x, s, a, e, h))
        return detour_outcome::decode;

      if ((h.flags & HDE32_F_RELATIVE) != 0)
      {
        // Offset from the function start modulo 2^32. A target before the
        // start wraps to a large value.
        //
        uint32_t d (value (branch_target (h, offset (base, a))) -
                    value (offset (base, f.address)));

        if (d != 0 && d < n)
          return detour_outcome::entry;
      }

      a += h.len;
    }

    return detour_outcome::valid;
  }

  // Return true if find_function() yields exactly this function for its
  // start address (precondition helper).
  //
  [[maybe_unused]] static bool
  known_function (const elf_image& x, const elf_function& f) noexcept
  {
    elf_function r (find_function (x, f.address));
    return r.address == f.address && r.size == f.size;
  }

  // Store jmp rel32 at p, which is at the process address from.
  //
  static void
  store_jump (uint8_t* p, i386_address from, i386_address to) noexcept
  {
    p[0] = jump_opcode;
    store32 (p + 1, value (to) - value (offset (from, jump_size)));
  }

  const char*
  detour_outcome_name (detour_outcome o) noexcept
  {
    switch (o)
    {
    case detour_outcome::valid:  return "valid";
    case detour_outcome::size:   return "size";
    case detour_outcome::decode: return "decode";
    case detour_outcome::branch: return "branch";
    case detour_outcome::entry:  return "entry";
    }

    LIBCOMPANION_UNREACHABLE ();
  }

  detour_outcome
  build_trampoline (const elf_image& x,
                    const elf_function& f,
                    i386_address base,
                    i386_address t,
                    trampoline& r) noexcept
  {
    detour_outcome o (detour_outcome::size);

    LIBCOMPANION_POST (o != detour_outcome::valid ||
                       (r.replaced >= jump_size &&
                        r.replaced <= max_replaced_size &&
                        r.size == r.replaced + jump_size));

    const elf_segment* s (find_segment (x, f.address, f.size));

    LIBCOMPANION_PRE (f.address != 0 && s != nullptr && s->executable);
    LIBCOMPANION_PRE (known_function (x, f));

    if (f.size < jump_size)
      return o;

    o = copy_replaced (x, *s, f, base, r);

    if (o == detour_outcome::valid)
      o = check_entries (x, *s, f, base, r.replaced);

    if (o != detour_outcome::valid)
      return o;

    store_jump (r.code + r.replaced,
                offset (t, r.replaced),
                offset (base, f.address + r.replaced));

    r.size = static_cast<uint8_t> (r.replaced + jump_size);
    return o;
  }

  void
  encode_jump (i386_address from,
               i386_address to,
               uint8_t (&r)[jump_size]) noexcept
  {
    store_jump (r, from, to);
  }
}
