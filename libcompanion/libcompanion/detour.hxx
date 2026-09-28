// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <cstddef> // size_t

#include <libcompanion/elf.hxx>

namespace companion
{
  // i386 function detours for the Linux Steam client (on Windows MinHook is
  // used; see hook-win32.cxx).
  //
  // A detour overwrites the start of a function with a jump to the hook.
  // The hook calls the original function through a trampoline, which
  // contains the overwritten instructions followed by a jump to the first
  // instruction after them.
  //
  // This code builds the trampoline and encodes the jump. It is a pure
  // function of the image bytes and the image and trampoline addresses, so
  // it runs and is tested on any host. The platform layer allocates the
  // trampoline memory and writes the jump (see hook-linux.cxx).
  //
  // On i386 only control transfer instructions are position-dependent (there
  // is no PC-relative data addressing), so other instructions are copied
  // unchanged. The only control transfer that we relocate is the call that
  // position-independent code uses to obtain its own address (see
  // pc_thunk()), which GCC normally places in the function prologue. It is
  // replaced with an instruction of the same size that stores the same
  // value in the same place:
  //
  //   call <pc thunk>     ->  mov <r>, imm32
  //   call <next insn>    ->  push imm32        (Clang)
  //
  // As a result, the trampoline code size is always the overwritten size
  // plus the jump size.
  //
  // We refuse to detour a function if the overwritten instructions contain
  // any other control transfer or if a direct branch in the function targets
  // an overwritten instruction other than the first (it would land inside
  // the jump). Indirect branches and branches from other functions (including
  // the cold part of this one) are not checked since we assume compilers
  // don't generate such branches into a prologue.
  //
  // Instructions are decoded with HDE (see minhook/hde32.c). Anything
  // that HDE or this code does not fully understand is refused.
  //

  // Address in the i386 process. Note that an address in the image is a
  // std::uint32_t offset from the image base (see elf.hxx). Arithmetic wraps
  // modulo 2^32, the same as on the CPU.
  //
  enum class i386_address: std::uint32_t {};

  // Size of jmp rel32, which is used both for the detour and for the jump
  // back from the trampoline.
  //
  inline constexpr std::size_t jump_size (5);

  // Maximum x86 instruction size.
  //
  inline constexpr std::size_t max_instruction_size (15);

  // Maximum number of overwritten bytes. All the overwritten instructions
  // except the last start within the jump and the last one can be of the
  // maximum size.
  //
  inline constexpr std::size_t max_replaced_size (jump_size - 1 +
                                                  max_instruction_size);

  // Trampoline code capacity. This is the maximum code size rounded up to a
  // power of two so that trampolines can be packed into aligned slots.
  //
  inline constexpr std::size_t trampoline_capacity (32);

  static_assert (max_replaced_size + jump_size <= trampoline_capacity);

  struct trampoline
  {
    std::uint8_t code[trampoline_capacity];
    std::uint8_t size;     // Code size (replaced plus jump_size).
    std::uint8_t replaced; // Number of overwritten function bytes.
  };

  // Trampoline building outcome. The failures are listed in the order of
  // the checks.
  //
  enum class detour_outcome: std::uint8_t
  {
    valid,
    size,   // Function is smaller than the jump.
    decode, // Undecodable instruction or instruction past the function end.
    branch, // Control transfer among the overwritten instructions.
    entry   // Branch into the overwritten instructions.
  };

  // Return the outcome name (for example, branch).
  //
  const char*
  detour_outcome_name (detour_outcome) noexcept;

  // Build the trampoline for the function given the image load address and
  // the trampoline address. If the outcome is valid, then the caller should
  // overwrite the first replaced bytes of the function with the jump to the
  // hook (see encode_jump()).
  //
  // The function must be as returned by find_function() for its start
  // address and must be in an executable segment.
  //
  detour_outcome
  build_trampoline (const elf_image&,
                    const elf_function&,
                    i386_address base,
                    i386_address t,
                    trampoline&) noexcept;

  // Encode jmp rel32 located at the from address and targeting the to
  // address.
  //
  void
  encode_jump (i386_address from,
               i386_address to,
               std::uint8_t (&)[jump_size]) noexcept;
}
