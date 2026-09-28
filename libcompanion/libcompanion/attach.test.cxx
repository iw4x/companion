// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <string>
#include <iostream>
#include <stdexcept> // runtime_error

#include <libcompanion/attach.hxx>

#undef NDEBUG
#include <cassert>

// Usages:
//
// argv[0] arguments <command line>
// argv[0] result <state>
//
// In the first form parse the attach entry point command line and print the
// process id or 'invalid'. The command line is ASCII and is widened
// character by character.
//
// In the second form map the specified settled companion state to the
// attach result and print it.
//
using namespace std;
using namespace companion;

static const char* states[] = {
  "starting",
  "active",
  "unsupported",
  "failed"
};

static const char* results[] = {
  "active",
  "unsupported",
  "failed",
  "invalid-arguments",
  "not-steam",
  "access-denied",
  "injection-failed",
  "timeout"
};

// Verify that to_attach_result() is constexpr.
//
static_assert (to_attach_result (companion_state::active) ==
               attach_result::active);

int
main (int argc, char* argv[])
try
{
  if (argc != 3)
    throw runtime_error ("invalid arguments");

  string m (argv[1]);
  string a (argv[2]);

  if (m == "arguments")
  {
    wstring w (a.begin (), a.end ());
    process_id p;

    if (parse_attach_arguments (w, p))
      cout << value (p) << '\n';
    else
      cout << "invalid" << '\n';
  }
  else if (m == "result")
  {
    size_t i (0);
    for (; i != size (states) && a != states[i]; ++i) ;

    if (i == size (states))
      throw runtime_error ("invalid state '" + a + '\'');

    attach_result r (to_attach_result (static_cast<companion_state> (i)));
    cout << results[static_cast<size_t> (r)] << '\n';
  }
  else
    throw runtime_error ("invalid mode '" + m + '\'');

  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 2;
}
