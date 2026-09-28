// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <span>
#include <string>
#include <cstdint>
#include <iomanip>   // hex
#include <iostream>
#include <stdexcept> // runtime_error

#include <libcompanion/minhook/hde32.h>
#include <libcompanion/minhook/hde64.h>

#undef NDEBUG
#include <cassert>

// Usage: argv[0] 32|64 <byte>...
//
// Decode the instruction that starts with the bytes (in hex) with the i386
// (32) or x86-64 (64) decoder and print the result in the following
// format:
//
// <length> [<flag>...]
// [imm <value>]
// [disp <value>]
//
// The flags are named after the HDE*_F_* macros in lower case, for
// example, error-opcode for HDE32_F_ERROR_OPCODE. The immediate and the
// displacement are printed in hex if present.
//
// The bytes past those specified are zero.
//
using namespace std;

struct flag_name
{
  uint32_t    flag;
  const char* name;
};

static const flag_name flags32[] = {
  {HDE32_F_MODRM,         "modrm"},
  {HDE32_F_SIB,           "sib"},
  {HDE32_F_IMM8,          "imm8"},
  {HDE32_F_IMM16,         "imm16"},
  {HDE32_F_IMM32,         "imm32"},
  {HDE32_F_DISP8,         "disp8"},
  {HDE32_F_DISP16,        "disp16"},
  {HDE32_F_DISP32,        "disp32"},
  {HDE32_F_RELATIVE,      "relative"},
  {HDE32_F_2IMM16,        "2imm16"},
  {HDE32_F_ERROR,         "error"},
  {HDE32_F_ERROR_OPCODE,  "error-opcode"},
  {HDE32_F_ERROR_LENGTH,  "error-length"},
  {HDE32_F_ERROR_LOCK,    "error-lock"},
  {HDE32_F_ERROR_OPERAND, "error-operand"},
  {HDE32_F_PREFIX_REPNZ,  "prefix-repnz"},
  {HDE32_F_PREFIX_REPX,   "prefix-repx"},
  {HDE32_F_PREFIX_66,     "prefix-66"},
  {HDE32_F_PREFIX_67,     "prefix-67"},
  {HDE32_F_PREFIX_LOCK,   "prefix-lock"},
  {HDE32_F_PREFIX_SEG,    "prefix-seg"}};

static const flag_name flags64[] = {
  {HDE64_F_MODRM,         "modrm"},
  {HDE64_F_SIB,           "sib"},
  {HDE64_F_IMM8,          "imm8"},
  {HDE64_F_IMM16,         "imm16"},
  {HDE64_F_IMM32,         "imm32"},
  {HDE64_F_IMM64,         "imm64"},
  {HDE64_F_DISP8,         "disp8"},
  {HDE64_F_DISP16,        "disp16"},
  {HDE64_F_DISP32,        "disp32"},
  {HDE64_F_RELATIVE,      "relative"},
  {HDE64_F_ERROR,         "error"},
  {HDE64_F_ERROR_OPCODE,  "error-opcode"},
  {HDE64_F_ERROR_LENGTH,  "error-length"},
  {HDE64_F_ERROR_LOCK,    "error-lock"},
  {HDE64_F_ERROR_OPERAND, "error-operand"},
  {HDE64_F_PREFIX_REPNZ,  "prefix-repnz"},
  {HDE64_F_PREFIX_REPX,   "prefix-repx"},
  {HDE64_F_PREFIX_66,     "prefix-66"},
  {HDE64_F_PREFIX_67,     "prefix-67"},
  {HDE64_F_PREFIX_LOCK,   "prefix-lock"},
  {HDE64_F_PREFIX_SEG,    "prefix-seg"},
  {HDE64_F_PREFIX_REX,    "prefix-rex"}};

// Print the length and the flags. Note that each prefix flag is a single
// bit (the REP flag is REPNZ and REPX).
//
static void
print (unsigned int length, uint32_t flags, span<const flag_name> names)
{
  cout << length;

  for (const flag_name& n: names)
  {
    if ((flags & n.flag) == n.flag)
      cout << ' ' << n.name;
  }

  cout << '\n';
}

static void
print_value (const char* name, uint64_t v)
{
  cout << name << ' ' << hex << v << dec << '\n';
}

int
main (int argc, char* argv[])
try
{
  if (argc < 3)
    throw runtime_error ("invalid arguments");

  string m (argv[1]);

  if (m != "32" && m != "64")
    throw runtime_error ("invalid mode '" + m + '\'');

  // The decoder may read up to 34 bytes for a single instruction (see
  // decode_window in detour.cxx), so the buffer has room for that past the
  // bytes specified.
  //
  uint8_t b[64] {};
  size_t n (0);

  for (int i (2); i != argc; ++i)
  {
    if (n == 32)
      throw runtime_error ("too many bytes");

    size_t p;
    string a (argv[i]);
    unsigned long v (stoul (a, &p, 16));

    if (p != a.size () || v > 0xff)
      throw runtime_error ("invalid byte '" + a + '\'');

    b[n++] = static_cast<uint8_t> (v);
  }

  if (m == "32")
  {
    hde32_instruction i;
    unsigned int l (hde32_disasm (b, &i));
    assert (l == i.len);

    print (l, i.flags, flags32);

    if (i.flags & HDE32_F_IMM32)
      print_value ("imm", i.imm.imm32);
    else if (i.flags & HDE32_F_IMM16)
      print_value ("imm", i.imm.imm16);
    else if (i.flags & HDE32_F_IMM8)
      print_value ("imm", i.imm.imm8);

    if (i.flags & HDE32_F_DISP32)
      print_value ("disp", i.disp.disp32);
    else if (i.flags & HDE32_F_DISP16)
      print_value ("disp", i.disp.disp16);
    else if (i.flags & HDE32_F_DISP8)
      print_value ("disp", i.disp.disp8);
  }
  else
  {
    hde64_instruction i;
    unsigned int l (hde64_disasm (b, &i));
    assert (l == i.len);

    print (l, i.flags, flags64);

    if (i.flags & HDE64_F_IMM64)
      print_value ("imm", i.imm.imm64);
    else if (i.flags & HDE64_F_IMM32)
      print_value ("imm", i.imm.imm32);
    else if (i.flags & HDE64_F_IMM16)
      print_value ("imm", i.imm.imm16);
    else if (i.flags & HDE64_F_IMM8)
      print_value ("imm", i.imm.imm8);

    if (i.flags & HDE64_F_DISP32)
      print_value ("disp", i.disp.disp32);
    else if (i.flags & HDE64_F_DISP16)
      print_value ("disp", i.disp.disp16);
    else if (i.flags & HDE64_F_DISP8)
      print_value ("disp", i.disp.disp8);
  }

  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 2;
}
