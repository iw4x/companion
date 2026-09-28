// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <iostream>

#include <libcompanion/elf.hxx>

#include <common/elf-builder.hxx>

// Usage: argv[0] [<address>...]
//
// Read the image description from stdin (see common/elf-builder.hxx),
// build the mapped i386 ELF32 shared object, parse it, and print the result
// in the following format:
//
// <outcome> <size>
//
// Here <size> is the elf_image_size() result. If the image is valid, then
// for each address also print the containing function start and size on a
// separate line (0 0 if there is none).
//
using namespace std;
using namespace companion;
using namespace elf_builder;

int
main (int argc, char* argv[])
try
{
  description d;

  for (string l; getline (cin, l); )
  {
    words w (split (l));

    if (!w.empty () && !parse (d, w))
      throw runtime_error ("invalid command '" + w[0] + '\'');
  }

  buffer b (build (d));

  elf_image x;
  elf_outcome o (parse_elf (bytes (b), x));

  cout << elf_outcome_name (o) << ' '
       << hex << elf_image_size (bytes (b)) << '\n';

  if (o == elf_outcome::valid)
  {
    for (int i (1); i != argc; ++i)
    {
      elf_function f (find_function (x, hex_number (argv[i])));
      cout << f.address << ' ' << f.size << '\n';
    }
  }

  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 1;
}
