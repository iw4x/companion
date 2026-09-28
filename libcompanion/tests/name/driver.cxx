// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <string>
#include <iostream>
#include <stdexcept> // runtime_error
#include <string_view>

#include <libcompanion/name.hxx>

#undef NDEBUG
#include <cassert>

// Usage: argv[0] <name> <key>
//
// Print the name of the specified kind for the key. The name kinds are:
//
// record       Windows record mapping (key is the game pid)
// status       Windows status mapping (key is the Steam pid)
// event        Windows record change event (key is the Steam pid)
// unix-record  Linux record file (key is the wineserver pid)
// unix-status  Linux status file (key is the uid)
//
// The Windows names consist of ASCII characters and are printed narrowed.
//
using namespace std;
using namespace companion;

// Verify the formatting at compile time.
//
static_assert (
  wstring_view (details::format_key<wchar_t, object_name_capacity> (
                  record_name_format, 4242).data ()) ==
  L"Local\\IW4x.SteamPresence.4242");

static_assert (
  string_view (details::format_key<char, unix_path_capacity> (
                 unix_status_format, 4294967295).data ()) ==
  "/dev/shm/iw4x-steam-companion.4294967295");

static_assert (details::key_format_size ("a%u")    == 1 + 10 + 1);
static_assert (details::key_format_size (L"a%lub") == 1 + 10 + 1 + 1);
// Verify that the formats without exactly one %u or %lu conversion are
// rejected.
//
static_assert (details::key_format_size ("a")      == size_t (-1));
static_assert (details::key_format_size ("%d")     == size_t (-1));
static_assert (details::key_format_size ("%u%u")   == size_t (-1));
static_assert (details::key_format_size ("%%")     == size_t (-1));
static_assert (details::key_format_size ("a%")     == size_t (-1));

// Narrow the ASCII string.
//
template <details::key_character C>
static string
narrow (const C* s)
{
  string r;
  for (; *s != 0; ++s)
  {
    assert (*s > 0 && *s < 0x80);
    r += static_cast<char> (*s);
  }

  return r;
}

int
main (int argc, char* argv[])
try
{
  if (argc != 3)
    throw runtime_error ("invalid arguments");

  string        m (argv[1]);
  std::uint32_t k (static_cast<std::uint32_t> (stoul (argv[2])));
  process_id    p (static_cast<process_id> (k));

  if      (m == "record")      cout << narrow (record_name (p).data ());
  else if (m == "status")      cout << narrow (status_name (p).data ());
  else if (m == "event")       cout << narrow (record_event_name (p).data ());
  else if (m == "unix-record") cout << narrow (unix_record_path (p).data ());
  else if (m == "unix-status")
    cout << narrow (unix_status_path (static_cast<user_id> (k)).data ());
  else
    throw runtime_error ("invalid name '" + m + '\'');

  cout << '\n';
  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 2;
}
