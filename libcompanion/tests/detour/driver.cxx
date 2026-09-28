// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

#include <string>
#include <cstring>   // memcpy()
#include <iomanip>   // setw(), setfill()
#include <iostream>
#include <stdexcept> // runtime_error

#include <libcompanion/elf.hxx>
#include <libcompanion/detour.hxx>

#include <common/elf-builder.hxx>

#undef NDEBUG
#include <cassert>

// Usages:
//
// argv[0] <function> <base> <trampoline>
// argv[0] --jump <from> <to>
//
// In the first form read the image description from stdin (see
// common/elf-builder.hxx), build the mapped i386 ELF32 shared object, and
// build the trampoline for the function that starts at <function> given the
// image load address <base> and the trampoline address <trampoline>. Then
// print the result in the following format:
//
// <outcome> [<replaced> <code>...]
//
// If the outcome is valid, then <replaced> is the number of overwritten
// bytes and <code> is the trampoline code.
//
// The image is mapped with its non-readable ('-') segments inaccessible,
// the same as the dynamic linker would map it. As a result, reading past
// the end of a readable segment faults.
//
// In the second form encode jmp rel32 from <from> to <to> and print its
// bytes.
//
// All the numbers are in hex.
//
using namespace std;
using namespace companion;
using namespace elf_builder;

static void
print (const uint8_t* p, size_t n)
{
  cout << hex << setfill ('0');

  for (size_t i (0); i != n; ++i)
    cout << (i != 0 ? " " : "") << setw (2) << unsigned (p[i]);
}

// Page size used for segment protection. An inaccessible segment must start
// and end on a page boundary.
//
static const uint32_t page_size (0x1000);

// Copy the image into newly allocated memory and make its non-readable
// segments inaccessible. The memory is not released.
//
static bytes
map_image (const description& d, const buffer& b)
{
  size_t n (b.size ());

#ifdef _WIN32
  void* p (VirtualAlloc (nullptr, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));

  if (p == nullptr)
    throw runtime_error ("unable to allocate the image");
#else
  void* p (mmap (nullptr,
                 n,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS,
                 -1,
                 0));

  if (p == MAP_FAILED)
    throw runtime_error ("unable to map the image");
#endif

  uint8_t* m (static_cast<uint8_t*> (p));
  memcpy (m, b.data (), n);

  for (const segment_item& s: d.segments)
  {
    if (s.flags != 0)
      continue;

    if (s.address % page_size != 0 || s.size % page_size != 0)
      throw runtime_error ("unaligned inaccessible segment");

#ifdef _WIN32
    DWORD o;
    if (!VirtualProtect (m + s.address, s.size, PAGE_NOACCESS, &o))
#else
    if (mprotect (m + s.address, s.size, PROT_NONE) != 0)
#endif
      throw runtime_error ("unable to protect the image");
  }

  return bytes (m, n);
}

static void
jump (const char* from, const char* to)
{
  uint8_t b[jump_size];
  encode_jump (i386_address (hex_number (from)),
               i386_address (hex_number (to)),
               b);

  print (b, jump_size);
  cout << '\n';
}

static void
detour (const char* function, const char* base, const char* t)
{
  description d;

  for (string l; getline (cin, l); )
  {
    words w (split (l));

    if (!w.empty () && !parse (d, w))
      throw runtime_error ("invalid command '" + w[0] + '\'');
  }

  buffer b (build (d));

  elf_image   x;
  elf_outcome xo (parse_elf (map_image (d, b), x));
  assert (xo == elf_outcome::valid);

  uint32_t     a (hex_number (function));
  elf_function f (find_function (x, a));

  if (f.address != a)
    throw runtime_error ("no function at " + string (function));

  trampoline     r;
  detour_outcome o (build_trampoline (x,
                                      f,
                                      i386_address (hex_number (base)),
                                      i386_address (hex_number (t)),
                                      r));

  cout << detour_outcome_name (o);

  if (o == detour_outcome::valid)
  {
    assert (r.size == r.replaced + jump_size);

    cout << ' ' << hex << unsigned (r.replaced) << ' ';
    print (r.code, r.size);
  }

  cout << '\n';
}

int
main (int argc, char* argv[])
try
{
  if (argc == 4 && argv[1] == string ("--jump"))
    jump (argv[2], argv[3]);
  else if (argc == 4)
    detour (argv[1], argv[2], argv[3]);
  else
    throw runtime_error ("invalid arguments");

  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 1;
}
