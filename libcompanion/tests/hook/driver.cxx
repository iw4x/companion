// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <string>
#include <vector>
#include <cstdio>
#include <sstream>
#include <iostream>
#include <stdexcept> // runtime_error
#include <string_view>

#include <libcompanion/hook.hxx>
#include <libcompanion/types.hxx>
#include <libcompanion/resend.hxx>
#include <libcompanion/registration.hxx>

#undef NDEBUG
#include <cassert>

// Usage: argv[0]
//
// Read commands from stdin, one per line, and run them against send_frame()
// instantiated with a test platform. The commands are:
//
// register <pid> <app> <name>   add a registration
// clock <thread> <ms>           set the current thread id and time
// change                        signal a registration change
// result true|false             set what the original function returns
// send <conn> <opcode> <hex>|-  call the hook with the frame ('-' for NULL)
//
// Each call of the original function and each diagnostics line are printed
// as they happen, followed by the hook result:
//
// original <conn> <opcode> <hex>|- -> <result>
// diag <text>
// sent <result>
//
using namespace std;
using namespace companion;

static vector<pair<process_id, registration>> registrations;

// Current thread, time, and original function result as set by commands.
//
static thread_id thread_now {};
static timestamp time_now {0};
static bool      result_now (true);

// Return the bytes as lower-case hex or '-' if the pointer is NULL.
//
static string
to_hex (const uint8_t* d, size_t n)
{
  if (d == nullptr)
    return "-";

  static const char digits[] = "0123456789abcdef";

  string r;
  for (size_t i (0); i != n; ++i)
  {
    r += digits[d[i] >> 4];
    r += digits[d[i] & 0xF];
  }

  return r;
}

static vector<uint8_t>
from_hex (const string& s)
{
  if (s.size () % 2 != 0)
    throw runtime_error ("odd number of hex digits in '" + s + '\'');

  vector<uint8_t> r;
  for (size_t i (0); i != s.size (); i += 2)
    r.push_back (static_cast<uint8_t> (stoul (s.substr (i, 2), nullptr, 16)));

  return r;
}

// Platform layer that prints the calls of the original function and the
// diagnostics to stdout.
//
struct test_platform
{
  static constexpr string_view diag_prefix {""};

  static bool
  original (void* c, uint32_t op, const uint8_t* d, uint32_t n)
  {
    cout << "original " << reinterpret_cast<uintptr_t> (c) << ' ' << op
         << ' ' << to_hex (d, n) << " -> " << boolalpha << result_now
         << '\n';

    return result_now;
  }

  static bool
  lookup (process_id p, registration& r) noexcept
  {
    for (const auto& e: registrations)
    {
      if (e.first == p)
      {
        r = e.second;
        return true;
      }
    }

    return false;
  }

  static thread_id
  current_thread () noexcept
  {
    return thread_now;
  }

  static timestamp
  now () noexcept
  {
    return time_now;
  }

  static void
  write_diag (const diag_line& l) noexcept
  {
    cout << "diag " << text (l);
  }
};

static_assert (send_platform<test_platform>);

static resend_state state;

// Run the command line.
//
static void
run (const string& l)
{
  istringstream is (l);
  string        command;

  is >> command;

  if (command == "register")
  {
    uint32_t p, a;
    string   x;
    is >> p >> a >> x;

    registration r {app_id (a), static_cast<uint8_t> (x.size ()), {}};
    assert (x.size () < sizeof (r.extra_info));
    x.copy (r.extra_info, x.size ());
    assert (valid (r));

    registrations.emplace_back (process_id (p), r);
  }
  else if (command == "clock")
  {
    uint64_t t;
    int64_t  ms;
    is >> t >> ms;

    thread_now = thread_id (t);
    time_now = timestamp (ms);
  }
  else if (command == "change")
    note_change (state);
  else if (command == "result")
    is >> boolalpha >> result_now;
  else if (command == "send")
  {
    uintptr_t c;
    uint32_t  op;
    string    f;
    is >> c >> op >> f;

    vector<uint8_t> d (f != "-" ? from_hex (f) : vector<uint8_t> ());

    bool r (send_frame<test_platform> (state,
                                       reinterpret_cast<void*> (c),
                                       op,
                                       f != "-" ? d.data () : nullptr,
                                       static_cast<uint32_t> (d.size ())));

    cout << "sent " << boolalpha << r << '\n';
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
