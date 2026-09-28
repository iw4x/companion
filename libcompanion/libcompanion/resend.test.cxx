// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <string>
#include <sstream>
#include <cstring>   // memcpy()
#include <iostream>
#include <stdexcept> // runtime_error

#include <libcompanion/resend.hxx>

#undef NDEBUG
#include <cassert>

// Usage: argv[0]
//
// Read events from stdin, one per line, and apply them to a resend state.
// Print the result of each resend() call and each frame it sends. Frames are
// strings, connections and threads are integers, and times are in
// milliseconds. The events are:
//
// rewrite <report> <frame>             <report> is rewritten into <frame>
// rewrite <report> -                   <report> has no registered processes
// report <c> <t> <ms> <report> <sent>  call note_report()
// change                               call note_change() and print
//                                      signalled or pending
// frame <c> <t> <ms>                   call resend() and print the outcome
//
using namespace std;
using namespace companion;

// Report and its rewrite as set by the rewrite event.
//
static string rewrite_report;
static string rewrite_result;

static string
text (bytes b)
{
  return string (b.begin (), b.end ());
}

static bytes
to_bytes (const string& s)
{
  return bytes (reinterpret_cast<const uint8_t*> (s.data ()), s.size ());
}

// Rewrite and send callbacks.
//
static size_t
rewrite (bytes r, mutable_bytes o) noexcept
{
  if (text (r) != rewrite_report || rewrite_result == "-")
    return 0;

  assert (rewrite_result.size () <= o.size ());
  memcpy (o.data (), rewrite_result.data (), rewrite_result.size ());
  return rewrite_result.size ();
}

static void
send (connection c, bytes f) noexcept
{
  cout << "send " << static_cast<uintptr_t> (c) << ' ' << text (f) << '\n';
}

static const char* outcomes[] = {
  "idle",
  "no-report",
  "other-thread",
  "stale",
  "no-match",
  "unchanged",
  "resent"
};

static resend_state state;

// Apply the event line.
//
static void
run (const string& l)
{
  istringstream is (l);
  string    command;
  uintptr_t c;
  uint64_t  t;
  int64_t   ms;

  is >> command;

  if (command == "rewrite")
    is >> rewrite_report >> rewrite_result;
  else if (command == "change")
    cout << (note_change (state) ? "signalled" : "pending") << '\n';
  else if (command == "report")
  {
    string r, x;
    is >> c >> t >> ms >> r >> x;
    note_report (state,
                 connection (c), to_bytes (r), to_bytes (x),
                 thread_id (t),
                 timestamp (ms));
  }
  else if (command == "frame")
  {
    is >> c >> t >> ms;
    resend_result r (resend (state,
                             connection (c),
                             thread_id (t),
                             timestamp (ms),
                             &rewrite,
                             &send));

    cout << outcomes[static_cast<size_t> (r.outcome)];

    if (r.outcome == resend_outcome::stale)
      cout << ' ' << r.idle.count ();

    cout << '\n';
  }
  else
    throw runtime_error ("invalid command '" + command + '\'');

  if (!is)
    throw runtime_error ("invalid line '" + l + '\'');
}

int
main ()
try
{
  for (string l; getline (cin, l); )
  {
    if (!l.empty ())
      run (l);
  }

  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 1;
}
