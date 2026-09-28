// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <map>
#include <string>
#include <vector>
#include <cstring>   // memcpy()
#include <sstream>
#include <iostream>
#include <algorithm> // min()
#include <stdexcept> // runtime_error

#include <libcompanion/types.hxx>
#include <libcompanion/process.hxx>
#include <libcompanion/protocol.hxx>
#include <libcompanion/registration.hxx>

// Usage: argv[0] <self> <pid>
//
// Read the process tree description from stdin, search the tree of <pid>
// with <self> as the Steam client, and print the result in the following
// format:
//
// <outcome> [<app> <extra info>]
//
// The processes are described with one command per line in any order. All
// the numbers are decimal. The commands are:
//
// process <pid> <parent> <start> [<nspid>...]
//
//   Add the stat and status files of the process. The status file contains
//   the NSpid line if the namespace pids are specified.
//
// stat <pid> <word>...
// status <pid> <word>...
//
//   Set the stat or status file content to the space-separated words. The
//   \t and \n escapes are replaced with the TAB and newline characters.
//
// children <pid> <child>...
//
//   Append the children list (of one task) to the process's children.
//
// fan <parent> <first> <count>
//
//   Add <count> processes with consecutive pids starting from <first> as
//   the parent's children (all with the start time 1).
//
// pad stat|status|children <pid> <count>
//
//   Append <count> spaces to the file.
//
// record <pid> <start> <app> <extra info>
//
//   Add the record file of the wineserver process.
//
using namespace std;
using namespace companion;

// File contents keyed by pid.
//
static map<uint32_t, string> stats;
static map<uint32_t, string> statuses;
static map<uint32_t, string> children;
static map<uint32_t, string> records;

// Process source functions over the file maps.
//
static size_t
read_file (const map<uint32_t, string>& m, process_id p, mutable_bytes b)
  noexcept
{
  auto i (m.find (value (p)));

  if (i == m.end ())
    return 0;

  const string& s (i->second);
  memcpy (b.data (), s.data (), min (s.size (), b.size ()));
  return s.size ();
}

static size_t
read_stat (process_id p, mutable_bytes b) noexcept
{
  return read_file (stats, p, b);
}

static size_t
read_status (process_id p, mutable_bytes b) noexcept
{
  return read_file (statuses, p, b);
}

static size_t
read_children (process_id p, mutable_bytes b) noexcept
{
  return read_file (children, p, b);
}

static size_t
read_record (process_id p, mutable_bytes b) noexcept
{
  return read_file (records, p, b);
}

// Parse a decimal number.
//
static uint64_t
number64 (const string& s)
{
  size_t n (0);
  unsigned long long v (0);

  try
  {
    v = stoull (s, &n, 10);
  }
  catch (const logic_error&) // invalid_argument, out_of_range
  {
    n = 0;
  }

  if (n == 0 || n != s.size () || s[0] == '-')
    throw runtime_error ("invalid number '" + s + '\'');

  return v;
}

static uint32_t
number (const string& s)
{
  uint64_t v (number64 (s));

  if (v > 0xFFFFFFFF)
    throw runtime_error ("invalid number '" + s + '\'');

  return static_cast<uint32_t> (v);
}

// Return the stat file content formatted as by the kernel (see proc(5)).
//
static string
stat_content (uint32_t p, uint32_t parent, uint64_t start)
{
  ostringstream os;
  os << p << " (wine) S " << parent << ' ' << p << ' ' << p
     << " 0 -1 4194560 120 0 0 0 3 1 0 0 20 0 1 0 " << start
     << " 3145728 256 4294967295 1 1 0 0 0 0 0 0 0 0 0 0 0 17 2 0 0 0 0 0"
     << " 0 0 0 0 0 0 0 0\n";
  return os.str ();
}

// Return the status file content with the NStgid and NSpid lines if the
// namespace pids are specified.
//
static string
status_content (uint32_t p, uint32_t parent, const vector<uint32_t>& ns)
{
  ostringstream os;
  os << "Name:\twine\nUmask:\t0022\nState:\tS (sleeping)\nTgid:\t" << p
     << "\nNgid:\t0\nPid:\t" << p << "\nPPid:\t" << parent
     << "\nTracerPid:\t0\nUid:\t1000\t1000\t1000\t1000\n";

  if (!ns.empty ())
  {
    for (const char* k: {"NStgid:", "NSpid:"})
    {
      os << k;

      for (uint32_t n: ns)
        os << '\t' << n;

      os << '\n';
    }
  }

  os << "VmPeak:\t   10000 kB\nThreads:\t1\n";
  return os.str ();
}

// Join the words starting from first with spaces and replace the \t and
// \n escapes.
//
static string
join (const vector<string>& w, size_t first)
{
  string r;

  for (size_t i (first); i < w.size (); ++i)
  {
    if (i != first)
      r += ' ';

    r += w[i];
  }

  for (size_t p (0); (p = r.find ('\\', p)) != string::npos; ++p)
  {
    if (p + 1 == r.size () || (r[p + 1] != 't' && r[p + 1] != 'n'))
      throw runtime_error ("invalid escape in '" + r + '\'');

    r.replace (p, 2, 1, r[p + 1] == 't' ? '\t' : '\n');
  }

  return r;
}

// Return the record file content.
//
static string
record_content (uint32_t p, uint64_t start, uint32_t app, const string& i)
{
  if (i.size () >= extra_info_capacity)
    throw runtime_error ("extra info too long");

  unix_record x {};
  x.magic           = unix_record_magic;
  x.version         = unix_record_version;
  x.size            = sizeof (unix_record);
  x.process_id      = p;
  x.start_time_low  = static_cast<uint32_t> (start);
  x.start_time_high = static_cast<uint32_t> (start >> 32);
  x.app_id          = app;
  memcpy (x.extra_info, i.data (), i.size ());

  return string (reinterpret_cast<const char*> (&x), sizeof (x));
}

// Verify the command argument count.
//
static void
arity (const vector<string>& w, size_t min, size_t max)
{
  if (w.size () - 1 < min || w.size () - 1 > max)
    throw runtime_error ("invalid argument count for '" + w[0] + '\'');
}

// Return the file map by the file name.
//
static map<uint32_t, string>&
file (const string& s)
{
  if (s == "stat")     return stats;
  if (s == "status")   return statuses;
  if (s == "children") return children;

  throw runtime_error ("invalid file '" + s + '\'');
}

// Parse the description line split into words.
//
static void
parse (const vector<string>& w)
{
  const string& c (w[0]);

  if (c == "process")
  {
    arity (w, 3, 3 + namespace_capacity);

    uint32_t p (number (w[1])), a (number (w[2]));
    uint64_t t (number64 (w[3]));
    vector<uint32_t> ns;

    for (size_t i (4); i != w.size (); ++i)
      ns.push_back (number (w[i]));

    stats[p] = stat_content (p, a, t);
    statuses[p] = status_content (p, a, ns);
  }
  else if (c == "stat" || c == "status")
  {
    arity (w, 2, 64);
    file (c)[number (w[1])] = join (w, 2) + (c == "stat" ? "\n" : "");
  }
  else if (c == "children")
  {
    arity (w, 1, 64);

    string& s (children[number (w[1])]);

    for (size_t i (2); i != w.size (); ++i)
      s += to_string (number (w[i])) + ' ';
  }
  else if (c == "fan")
  {
    arity (w, 3, 3);

    uint32_t a (number (w[1])), f (number (w[2])), n (number (w[3]));

    for (uint32_t p (f); p != f + n; ++p)
    {
      stats[p] = stat_content (p, a, 1);
      statuses[p] = status_content (p, a, {});
      children[a] += to_string (p) + ' ';
    }
  }
  else if (c == "pad")
  {
    arity (w, 3, 3);
    file (w[1])[number (w[2])].append (number (w[3]), ' ');
  }
  else if (c == "record")
  {
    arity (w, 4, 4);

    uint32_t p (number (w[1]));
    records[p] = record_content (p, number64 (w[2]), number (w[3]), w[4]);
  }
  else
    throw runtime_error ("invalid command '" + c + '\'');
}

int
main (int argc, char* argv[])
try
{
  if (argc != 3)
    throw runtime_error ("invalid arguments");

  for (string l; getline (cin, l); )
  {
    istringstream    is (l);
    vector<string>   w;

    for (string s; is >> s; )
      w.push_back (s);

    if (!w.empty ())
      parse (w);
  }

  const process_source s {&read_stat, &read_status, &read_children,
                          &read_record};

  registration   r;
  search_outcome o (find_registration (s,
                                       process_id (number (argv[1])),
                                       process_id (number (argv[2])),
                                       r));

  cout << search_outcome_name (o);

  if (o == search_outcome::found)
    cout << ' ' << value (r.app) << ' ' << extra_info (r);

  cout << '\n';
  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 1;
}
