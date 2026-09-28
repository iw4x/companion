// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <string>
#include <concepts>
#include <vector>
#include <cstring>   // memcpy(), memset(), memcmp()
#include <sstream>
#include <iostream>
#include <stdexcept> // runtime_error

#include <libcompanion/registration.hxx>

#undef NDEBUG
#include <cassert>

// Usages:
//
// argv[0] record <pid>
// argv[0] unix-record <pid> <start>
// argv[0] --valid <name>
//
// In the first two forms create a valid Windows or Linux record of the
// process (with app 10190 and display name 'IW4x'), modify it according to
// the lines read from stdin, decode it for the process (and start time),
// and print the outcome. For a valid outcome also print the app id and the
// display name. Each stdin line modifies one field:
//
// magic <n>        magic
// version <n>      version
// size <n>         size
// process <n>      process_id
// start <n>        start_time_low and start_time_high (Linux only)
// app <n>          app_id
// info '<text>'    extra_info with the NUL
// info-hex <hex>   extra_info as hex bytes with the NUL
// info-fill <c>    extra_info entirely filled with c (no NUL)
// length <n>       size of the decoded bytes (zero-extended if needed)
//
// Numbers can be decimal or 0x-prefixed hex.
//
// In the third form exit with 0 if the name is a valid display name and
// with 1 otherwise.
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
  "process",
  "start-time",
  "app",
  "extra-info"
};

// Record structures that this driver can modify and lay out.
//
template <typename R>
concept record_layout = same_as<R, record> || same_as<R, unix_record>;

// Apply the modification line to the record.
//
template <record_layout R>
static void
change (R& x, size_t& length, const string& l)
{
  istringstream is (l);
  string        f;
  is >> f;

  auto number = [&is, &l] () -> uint64_t
  {
    string v;
    if (!(is >> v))
      throw runtime_error ("missing value in '" + l + '\'');

    return stoull (v, nullptr, 0);
  };

  if      (f == "magic")   x.magic      = static_cast<uint32_t> (number ());
  else if (f == "version") x.version    = static_cast<uint32_t> (number ());
  else if (f == "size")    x.size       = static_cast<uint32_t> (number ());
  else if (f == "process") x.process_id = static_cast<uint32_t> (number ());
  else if (f == "app")     x.app_id     = static_cast<uint32_t> (number ());
  else if (f == "length")  length       = static_cast<size_t> (number ());
  else if (f == "info")
  {
    size_t b (l.find ('\''));
    size_t e (l.rfind ('\''));

    if (b == e || e - b - 1 >= sizeof (x.extra_info))
      throw runtime_error ("invalid info in '" + l + '\'');

    memset (x.extra_info, 0, sizeof (x.extra_info));
    memcpy (x.extra_info, l.data () + b + 1, e - b - 1);
  }
  else if (f == "info-hex")
  {
    string h;
    if (!(is >> h) || h.size () % 2 != 0 ||
        h.size () / 2 >= sizeof (x.extra_info))
      throw runtime_error ("invalid hex in '" + l + '\'');

    memset (x.extra_info, 0, sizeof (x.extra_info));

    for (size_t i (0); i != h.size () / 2; ++i)
      x.extra_info[i] = static_cast<char> (stoul (h.substr (i * 2, 2),
                                                  nullptr,
                                                  16));
  }
  else if (f == "info-fill")
  {
    char c;
    if (!(is >> c))
      throw runtime_error ("missing character in '" + l + '\'');

    memset (x.extra_info, c, sizeof (x.extra_info));
  }
  else if (f == "start")
  {
    if constexpr (same_as<R, unix_record>)
    {
      uint64_t v (number ());
      x.start_time_low  = static_cast<uint32_t> (v);
      x.start_time_high = static_cast<uint32_t> (v >> 32);
    }
    else
      throw runtime_error ("start time in a Windows record");
  }
  else
    throw runtime_error ("invalid line '" + l + '\'');
}

// Apply the stdin modifications and return the record bytes of the
// requested length.
//
template <record_layout R>
static buffer
read_record (R x)
{
  size_t length (sizeof (R));

  for (string l; getline (cin, l); )
  {
    if (!l.empty ())
      change (x, length, l);
  }

  buffer r (length);
  memcpy (r.data (), &x, min (length, sizeof (R)));
  return r;
}

// Print the outcome and, if valid, the registration.
//
static void
print (registration_outcome o, const registration& r)
{
  cout << outcomes[static_cast<size_t> (o)];

  if (o == registration_outcome::valid)
    cout << ' ' << value (r.app) << " '" << extra_info (r) << '\'';

  cout << '\n';
}

int
main (int argc, char* argv[])
try
{
  string m (argc > 1 ? argv[1] : "");

  if (m == "--valid" && argc == 3)
    return valid_extra_info (argv[2]) ? 0 : 1;

  // Initialize the registration with a pattern that no successful decode
  // produces. A failed decode must not change it (verified below).
  //
  registration r;
  memset (&r, 0xA5, sizeof (r));

  const registration sentinel (r);

  registration_outcome o;

  if (m == "record" && argc == 3)
  {
    process_id p (static_cast<process_id> (stoul (argv[2])));

    record x {};
    x.magic      = record_magic;
    x.version    = record_version;
    x.size       = sizeof (record);
    x.process_id = value (p);
    x.app_id     = preferred_app_id;
    memcpy (x.extra_info, "IW4x", 4);

    o = decode_record (read_record (x), p, r);
  }
  else if (m == "unix-record" && argc == 4)
  {
    process_id p (static_cast<process_id> (stoul (argv[2])));
    start_time t (static_cast<start_time> (stoull (argv[3])));

    unix_record x {};
    x.magic           = unix_record_magic;
    x.version         = unix_record_version;
    x.size            = sizeof (unix_record);
    x.process_id      = value (p);
    x.start_time_low  = static_cast<uint32_t> (value (t));
    x.start_time_high = static_cast<uint32_t> (value (t) >> 32);
    x.app_id          = preferred_app_id;
    memcpy (x.extra_info, "IW4x", 4);

    o = decode_unix_record (read_record (x), p, t, r);
  }
  else
    throw runtime_error ("invalid arguments");

  if (o == registration_outcome::valid)
    assert (valid (r));
  else
    assert (memcmp (&r, &sentinel, sizeof (r)) == 0);

  print (o, r);
  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 2;
}
