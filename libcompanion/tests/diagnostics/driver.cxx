// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <string>
#include <iostream>
#include <stdexcept> // invalid_argument

#include <libcompanion/diagnostics.hxx>

#undef NDEBUG
#include <cassert>

// Usage: argv[0] <prefix> <message> [<format>]
//
// Format the diagnostics line with the prefix of <prefix> 'p' characters and
// the message of <message> 'x' characters and print it. If <format> is
// specified, then use it as a dynamic format string for the message, which
// allows testing runtime formatting failures.
//
using namespace std;
using namespace companion;

int
main (int argc, char* argv[])
try
{
  if (argc < 3 || argc > 4)
    throw invalid_argument ("invalid argument count");

  string p (stoul (argv[1]), 'p');
  string m (stoul (argv[2]), 'x');

  diag_line l (argc == 3
              ? format_diag (p, "{}", m)
              : format_diag (p, dynamic_format (argv[3]), m));

  assert (l.size < diag_capacity);
  assert (l.data[l.size] == '\0');
  assert (l.data[l.size - 1] == '\n');

  cout << text (l);
  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 1;
}
