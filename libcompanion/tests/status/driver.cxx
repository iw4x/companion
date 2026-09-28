// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <string>
#include <vector>
#include <cstring>   // memcpy(), memset(), memcmp()
#include <sstream>
#include <concepts>
#include <iostream>
#include <stdexcept> // runtime_error

#include <libcompanion/status.hxx>

#undef NDEBUG
#include <cassert>

// Usages:
//
// argv[0] status <steam pid> <state> <hook>
// argv[0] unix-status <steam pid> <state> <hook>
//
// Create the Windows or Linux status with the specified fields, modify it
// according to the lines read from stdin, decode it, and print the outcome.
// For a valid outcome also print the decoded state, build, hook, and Steam
// pid. The state is one of starting, active, unsupported, or failed. Each
// stdin line modifies one field:
//
// magic <n>        magic
// version <n>      version
// size <n>         size
// state <n>        state (numeric)
// build <n>        build
// hook <n>         hook
// steam <n>        steam_pid (Linux only)
// length <n>       size of the decoded bytes (zero-extended if needed)
//
// Numbers can be decimal or 0x-prefixed hex.
//
using namespace std;
using namespace companion;

using buffer = vector<uint8_t>;

static const char* outcomes[] = {
  "valid",
  "truncated",
  "magic",
  "version",
  "size",
  "state"
};

static const char* states[] = {
  "starting",
  "active",
  "unsupported",
  "failed"
};

// Parse the state name.
//
static companion_state
parse_state (const string& s)
{
  for (size_t i (0); i != size (states); ++i)
  {
    if (s == states[i])
      return static_cast<companion_state> (i);
  }

  throw runtime_error ("invalid state '" + s + '\'');
}

// Status structures that this driver can modify and lay out.
//
template <typename S>
concept status_layout = same_as<S, status> || same_as<S, unix_status>;

// Apply the modification line to the status.
//
template <status_layout S>
static void
change (S& x, size_t& length, const string& l)
{
  istringstream is (l);
  string        f, v;

  if (!(is >> f >> v))
    throw runtime_error ("invalid line '" + l + '\'');

  uint32_t n (static_cast<uint32_t> (stoul (v, nullptr, 0)));

  if      (f == "magic")   x.magic   = n;
  else if (f == "version") x.version = n;
  else if (f == "size")    x.size    = n;
  else if (f == "state")   x.state   = n;
  else if (f == "build")   x.build   = n;
  else if (f == "hook")    x.hook    = n;
  else if (f == "length")  length    = n;
  else if (f == "steam")
  {
    if constexpr (same_as<S, unix_status>)
      x.steam_pid = n;
    else
      throw runtime_error ("Steam pid in a Windows status");
  }
  else
    throw runtime_error ("invalid line '" + l + '\'');
}

// Apply the stdin modifications and return the status bytes of the
// requested length.
//
template <status_layout S>
static buffer
read_status (S x)
{
  size_t length (sizeof (S));

  for (string l; getline (cin, l); )
  {
    if (!l.empty ())
      change (x, length, l);
  }

  buffer r (length);
  memcpy (r.data (), &x, min (length, sizeof (S)));
  return r;
}

int
main (int argc, char* argv[])
try
{
  if (argc != 5)
    throw runtime_error ("invalid arguments");

  string          m (argv[1]);
  process_id      p (static_cast<process_id> (stoul (argv[2])));
  companion_state s (parse_state (argv[3]));
  uint32_t        h (static_cast<uint32_t> (stoul (argv[4], nullptr, 0)));

  // Initialize the result with a pattern that a failed decode must leave
  // unchanged (verified below).
  //
  companion_status r;
  memset (&r, 0xA5, sizeof (r));

  const companion_status sentinel (r);

  status_outcome o;

  if (m == "status")
    o = decode_status (read_status (make_status (s, h)), p, r);
  else if (m == "unix-status")
    o = decode_unix_status (read_status (make_unix_status (s, h, p)), r);
  else
    throw runtime_error ("invalid mode '" + m + '\'');

  cout << outcomes[static_cast<size_t> (o)];

  if (o == status_outcome::valid)
  {
    assert (valid (r.state));

    cout << ' ' << states[static_cast<size_t> (r.state)]
         << ' ' << r.build
         << ' ' << r.hook
         << ' ' << value (r.steam);
  }
  else
    assert (memcmp (&r, &sentinel, sizeof (r)) == 0);

  cout << '\n';
  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 2;
}
