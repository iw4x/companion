// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <string>
#include <vector>
#include <cstring>   // memcpy()
#include <sstream>
#include <iostream>
#include <algorithm> // sort(), max(), fill_n(), find_if()
#include <stdexcept> // runtime_error

#include <libcompanion/pe.hxx>
#include <libcompanion/elf.hxx>
#include <libcompanion/endian.hxx>
#include <libcompanion/locate.hxx>

#include <libcompanion/elf.test.hxx>

#undef NDEBUG
#include <cassert>

// Usage: argv[0] [--elf]
//
// Read the image description from stdin, build an x64 PE32+ image (or an
// i386 ELF32 shared object with --elf), locate the send function in it, and
// print the result in the following format:
//
// <outcome> <rva> <references> <candidates>
//
// Here <rva> is in hex and the counts are decimal.
//
// The image is described with one command per line in any order. All the
// numbers in the description are in hex. The PE image commands are:
//
// section code|data <rva> <size>
//
//   Add a code or data section. A code section is filled with int3 and a
//   data section with zeros.
//
// anchor <rva>
//
//   Write the anchor string including its terminating NUL.
//
// string <rva> <text>
//
//   Write the text without a terminating NUL.
//
// lea <rva> <target> [<rex> [<modrm>]]
//
//   Write lea r64, [rip + disp32] that loads <target>. The REX prefix and
//   ModRM default to 48 and 05.
//
// function <begin> <end> [<primary>]
//
//   Add a function table entry. If <primary> is specified, then the entry's
//   unwind information is chained to the entry that starts at <primary>.
//
// The function table and unwind information are placed into an additional
// data section after the last described one.
//
// The ELF image commands are those of elf.test.hxx plus the following:
//
// anchor <address>
// string <address> <text>
//
//   As for the PE image.
//
// thunk <address> <register>
//
//   Write the PC thunk mov <register>, [esp]; ret.
//
// load <address> <register> <got> [<thunk>]
//
//   Write the GOT load. With <thunk> it is call <thunk> and otherwise it is
//   call to the next instruction followed by pop <register>. In both cases
//   it ends with add <register>, <got> - <return address>.
//
// lea <address> <target> <got> [<modrm>]
//
//   Write lea r32, [r32 + <target> - <got>]. The ModRM defaults to 83,
//   which is eax, [ebx + disp32].
//
// Registers are specified by name (eax to edi).
//
using namespace std;
using namespace companion;

using buffer = vector<uint8_t>;

struct section_item
{
  bool     code;
  uint32_t rva;
  uint32_t size;
};

struct write_item
{
  uint32_t rva;
  buffer   data;
};

struct function_item
{
  uint32_t begin;
  uint32_t end;
  uint32_t primary; // 0 if not chained.
};

static vector<section_item>  sections;
static vector<write_item>    writes;
static vector<function_item> functions;

// PE32+ header layout (see the PE/COFF specification).
//
static const size_t nt_header       (0x40);
static const size_t file_header     (nt_header + 4);
static const size_t optional_header (file_header + 20);
static const size_t optional_size   (240); // Including 16 data directories.
static const size_t section_table   (optional_header + optional_size);
static const size_t section_size    (40);

static const uint32_t header_end (0x1000); // Lowest section RVA.

static const uint32_t code_characteristics (0x60000020); // Code, exec, read.
static const uint32_t data_characteristics (0x40000040); // Data, read.

// RUNTIME_FUNCTION and UNWIND_INFO. Each function gets an unwind slot of
// the same size, which fits either the plain or the chained form.
//
static const size_t   function_size (12);
static const uint32_t unwind_size   (16);
static const uint8_t  unwind_chain  (0x04 << 3 | 1);
static const uint8_t  unwind_plain  (1);

static void
store16 (uint8_t* p, uint16_t v)
{
  p[0] = static_cast<uint8_t> (v);
  p[1] = static_cast<uint8_t> (v >> 8);
}

static void
write (buffer& b, uint32_t rva, const buffer& d)
{
  if (rva < header_end || rva > b.size () || b.size () - rva < d.size ())
    throw runtime_error ("write outside of sections at " + to_string (rva));

  memcpy (b.data () + rva, d.data (), d.size ());
}

static void
write_function (uint8_t* p, uint32_t begin, uint32_t end, uint32_t unwind)
{
  store32 (p, begin);
  store32 (p + 4, end);
  store32 (p + 8, unwind);
}

static uint32_t
align (uint32_t v, uint32_t a)
{
  return (v + a - 1) & ~(a - 1);
}

// Build the image: the headers, the described sections with their content,
// and the exception data section. Note that the described functions are
// sorted by the start address as a side effect.
//
static buffer
build ()
{
  uint32_t e (header_end);

  for (const section_item& s: sections)
    e = max (e, align (s.rva + s.size, 0x1000));

  sort (functions.begin (), functions.end (),
        [] (const function_item& x, const function_item& y)
        {
          return x.begin < y.begin;
        });

  uint32_t fn (static_cast<uint32_t> (functions.size ()));
  uint32_t ft (e);                                       // Function table.
  uint32_t ui (align (ft + fn * uint32_t (function_size), 4)); // Unwind info.
  uint32_t xe (ui + fn * unwind_size);                   // Exception data end.
  uint32_t n  (align (max (xe, ft + 1), 0x1000));        // Image size.

  buffer b (n, 0);

  uint8_t* h (b.data ());
  assert (h != nullptr);

  // Headers.
  //
  store16 (h, 0x5A4D);                                // "MZ"
  store32 (h + 0x3C, uint32_t (nt_header));           // e_lfanew
  store32 (h + nt_header, 0x00004550);                // "PE\0\0"
  store16 (h + file_header, 0x8664);                  // AMD64
  store16 (h + file_header + 2, static_cast<uint16_t> (sections.size () + 1));
  store16 (h + file_header + 16, uint16_t (optional_size));
  store16 (h + optional_header, 0x20B);               // PE32+
  store32 (h + optional_header + 56, n);
  store32 (h + optional_header + 108, 16);
  store32 (h + optional_header + 112 + 3 * 8, ft);
  store32 (h + optional_header + 112 + 3 * 8 + 4,
           fn * uint32_t (function_size));

  // Section table with the exception data section last.
  //
  size_t t (section_table);

  auto add = [h, &t] (uint32_t rva, uint32_t size, uint32_t c)
  {
    if (t + section_size > header_end)
      throw runtime_error ("too many sections");

    store32 (h + t + 8, size);
    store32 (h + t + 12, rva);
    store32 (h + t + 36, c);
    t += section_size;
  };

  for (const section_item& s: sections)
  {
    if (s.rva < header_end)
      throw runtime_error ("section overlaps the headers");

    add (s.rva, s.size, s.code ? code_characteristics : data_characteristics);

    if (s.code)
      fill_n (h + s.rva, s.size, uint8_t (0xCC));
  }

  add (ft, xe - ft, data_characteristics);

  // Section content.
  //
  for (const write_item& w: writes)
    write (b, w.rva, w.data);

  // Function table and the unwind slot of each entry.
  //
  for (uint32_t i (0); i != fn; ++i)
  {
    const function_item& f (functions[i]);
    uint32_t u (ui + i * unwind_size);

    write_function (h + ft + i * function_size, f.begin, f.end, u);

    if (f.primary == 0)
    {
      b[u] = unwind_plain;
      continue;
    }

    auto p (find_if (functions.begin (), functions.end (),
                     [&f] (const function_item& x)
                     {
                       return x.begin == f.primary;
                     }));

    if (p == functions.end ())
      throw runtime_error ("no function at " + to_string (f.primary));

    size_t pi (static_cast<size_t> (p - functions.begin ()));

    // The chained RUNTIME_FUNCTION follows the (empty) unwind codes.
    //
    b[u] = unwind_chain;
    write_function (h + u + 4,
                    p->begin,
                    p->end,
                    ui + static_cast<uint32_t> (pi) * unwind_size);
  }

  return b;
}

// Description line split into words.
//
using words = vector<string>;

// Parse the word i as a 32-bit hex number.
//
static uint32_t
hex_word (const words& w, size_t i)
{
  const string& s (w[i]);
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
static void
arity (const words& w, size_t min, size_t max)
{
  if (w.size () - 1 < min || w.size () - 1 > max)
    throw runtime_error ("invalid argument count for '" + w[0] + '\'');
}

// Parse the PE image description line.
//
static void
parse (const string& l)
{
  words w;
  {
    istringstream is (l);
    for (string s; is >> s; )
      w.push_back (s);
  }

  const string& c (w[0]);

  if (c == "section")
  {
    arity (w, 3, 3);

    if (w[1] != "code" && w[1] != "data")
      throw runtime_error ("invalid section kind '" + w[1] + '\'');

    sections.push_back (
      section_item {w[1] == "code", hex_word (w, 2), hex_word (w, 3)});
  }
  else if (c == "anchor")
  {
    arity (w, 1, 1);

    // The literal is NUL-terminated, so we can copy the NUL with it.
    //
    const char* a (send_frame_anchor.data ());
    writes.push_back (
      write_item {hex_word (w, 1),
                  buffer (a, a + send_frame_anchor.size () + 1)});
  }
  else if (c == "string")
  {
    arity (w, 2, 2);
    writes.push_back (
      write_item {hex_word (w, 1), buffer (w[2].begin (), w[2].end ())});
  }
  else if (c == "lea")
  {
    arity (w, 2, 4);

    uint32_t r     (hex_word (w, 1));
    uint32_t t     (hex_word (w, 2));
    uint32_t rex   (w.size () > 3 ? hex_word (w, 3) : 0x48);
    uint32_t modrm (w.size () > 4 ? hex_word (w, 4) : 0x05);

    if (rex > 0xFF || modrm > 0xFF)
      throw runtime_error ("invalid byte in '" + l + '\'');

    writes.push_back (
      write_item {r, buffer {static_cast<uint8_t> (rex),
                             0x8D,
                             static_cast<uint8_t> (modrm),
                             0, 0, 0, 0}});

    // RIP-relative displacement (modulo 2^32).
    //
    store32 (writes.back ().data.data () + 3, t - (r + 7));
  }
  else if (c == "function")
  {
    arity (w, 2, 3);
    functions.push_back (
      function_item {hex_word (w, 1),
                     hex_word (w, 2),
                     w.size () > 3 ? hex_word (w, 3) : 0});
  }
  else
    throw runtime_error ("invalid command '" + c + '\'');
}

// Read the PE image description, build the image, and locate the send
// function.
//
static locate_result
locate_pe ()
{
  for (string l; getline (cin, l); )
  {
    if (l.find_first_not_of (' ') != string::npos)
      parse (l);
  }

  buffer b (build ());

  pe_image x;
  pe_outcome o (parse_pe (bytes (b), x));
  assert (o == pe_outcome::valid);
  assert (pe_image_size (bytes (b)) == b.size ());

  return locate_send_frame (x);
}

// Return the register encoding for the register name.
//
static uint8_t
register_number (const string& s)
{
  static const char* const names[] = {
    "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};

  for (uint8_t i (0); i != 8; ++i)
  {
    if (s == names[i])
      return i;
  }

  throw runtime_error ("invalid register '" + s + '\'');
}

static void
add_write (elf_test::description& d, uint32_t a, const buffer& b)
{
  d.writes.push_back (elf_test::write_item {a, b});
}

// Parse the ELF image description line if it is one of the commands
// specific to this test. Return false otherwise.
//
static bool
parse_i386 (elf_test::description& d, const words& w)
{
  using elf_test::arity;
  using elf_test::hex_number;

  const string& c (w[0]);

  if (c == "anchor")
  {
    arity (w, 1, 1);

    const char* a (send_frame_anchor.data ());
    add_write (d, hex_number (w[1]),
               buffer (a, a + send_frame_anchor.size () + 1));
  }
  else if (c == "string")
  {
    arity (w, 2, 2);
    add_write (d, hex_number (w[1]), buffer (w[2].begin (), w[2].end ()));
  }
  else if (c == "thunk")
  {
    arity (w, 2, 2);

    uint8_t r (register_number (w[2]));
    add_write (d, hex_number (w[1]),
               buffer {0x8B, static_cast<uint8_t> (r << 3 | 0x04), 0x24, 0xC3});
  }
  else if (c == "load")
  {
    arity (w, 3, 4);

    uint32_t a  (hex_number (w[1]));
    uint8_t  r  (register_number (w[2]));
    uint32_t g  (hex_number (w[3]));
    uint32_t ra (a + 5);

    buffer b {0xE8, 0, 0, 0, 0};

    if (w.size () > 4)
      store32 (b.data () + 1, hex_number (w[4]) - ra);
    else
      b.push_back (static_cast<uint8_t> (0x58 + r));

    b.insert (b.end (), {0x81, static_cast<uint8_t> (0xC0 | r), 0, 0, 0, 0});
    store32 (b.data () + b.size () - 4, g - ra); // Modulo 2^32.

    add_write (d, a, b);
  }
  else if (c == "lea")
  {
    arity (w, 3, 4);

    uint32_t m (w.size () > 4 ? hex_number (w[4]) : 0x83);

    if (m > 0xFF)
      throw runtime_error ("invalid ModRM '" + w[4] + '\'');

    buffer b {0x8D, static_cast<uint8_t> (m), 0, 0, 0, 0};
    store32 (b.data () + 2, hex_number (w[2]) - hex_number (w[3]));

    add_write (d, hex_number (w[1]), b);
  }
  else
    return false;

  return true;
}

// Read the ELF image description, build the image, and locate the send
// function.
//
static locate_result
locate_elf ()
{
  elf_test::description d;

  for (string l; getline (cin, l); )
  {
    words w (elf_test::split (l));

    if (!w.empty () && !parse_i386 (d, w) && !elf_test::parse (d, w))
      throw runtime_error ("invalid command '" + w[0] + '\'');
  }

  buffer b (elf_test::build (d));

  elf_image x;
  elf_outcome o (parse_elf (bytes (b), x));
  assert (o == elf_outcome::valid);
  assert (elf_image_size (bytes (b)) == b.size ());

  return locate_send_frame (x);
}

int
main (int argc, char* argv[])
try
{
  bool elf (argc > 1 && argv[1] == string ("--elf"));

  if (argc > (elf ? 2 : 1))
    throw runtime_error ("invalid arguments");

  locate_result r (elf ? locate_elf () : locate_pe ());

  cout << locate_outcome_name (r.outcome) << ' '
       << hex << r.rva << dec << ' '
       << r.references << ' '
       << r.candidates << '\n';

  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 1;
}
