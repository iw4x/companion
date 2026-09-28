// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <string>
#include <vector>
#include <cstring>   // memcpy(), memcmp(), strchr(), strpbrk()
#include <iostream>
#include <stdexcept> // runtime_error

#include <libcompanion/wire.hxx>
#include <libcompanion/games-played.hxx>

#undef NDEBUG
#include <cassert>

// Usage: argv[0] [--capacity <n>] [<pid>=<app>:<name>|<pid>=<app>/<hex>...]
//
// Read the GamesPlayed frame in the text format described below from stdin,
// rewrite it with the specified registrations, and print the rewritten body
// in the same format. If the frame is not rewritten, then exit with 1.
//
// Each registration specifies the display name either as text or as UTF-8
// bytes in hex. The hex form is for non-ASCII names since on Windows the
// narrow command line is not UTF-8.
//
// The --capacity option specifies the output buffer size (frame_capacity by
// default).
//
// The frame text starts with the optional message header line followed by a
// line per body field:
//
// emsg <n> [plain]   EMsg (742 by default) without the protobuf flag if plain
// <n> <v>            varint field
// <n> f64 <v>        fixed64 field
// <n> f32 <v>        fixed32 field
// <n> '<text>'       length-delimited field with text content
// <n> {              length-delimited field with message content up to }
// x <hex>            raw bytes (for example, to make the frame malformed)
//
// On output a length-delimited field is printed as a message if it is one
// in the CMsgClientGamesPlayed schema and as text otherwise.
//
using namespace std;
using namespace companion;

using buffer = vector<uint8_t>;

static vector<pair<process_id, registration>> registrations;

// Registration lookup callback over the command line registrations.
//
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

static void
append_varint (buffer& b, uint64_t v)
{
  uint8_t t[max_varint_size];
  wire_writer w {t};
  write_varint (w, v);
  b.insert (b.end (), t, t + w.size);
}

static void
append_tag (buffer& b, uint32_t n, wire_type t)
{
  append_varint (b, uint64_t (n) << 3 | uint8_t (t));
}

static void
append_fixed (buffer& b, uint64_t v, size_t n)
{
  for (size_t i (0); i != n; ++i)
    b.push_back (uint8_t (v >> (8 * i)));
}

static void
append_length (buffer& b, uint32_t n, const buffer& c)
{
  append_tag (b, n, wire_type::length);
  append_varint (b, c.size ());
  b.insert (b.end (), c.begin (), c.end ());
}

// Parse pairs of hex digits skipping spaces.
//
static buffer
parse_hex (const string& s)
{
  string d;
  for (char c: s)
    if (c != ' ')
      d += c;

  buffer r;
  for (size_t i (0); i + 1 < d.size (); i += 2)
    r.push_back (uint8_t (stoul (d.substr (i, 2), nullptr, 16)));

  return r;
}

static string
trim (const string& s)
{
  size_t b (s.find_first_not_of (' '));
  return b == string::npos ? string () : s.substr (b);
}

// Parse the field lines until the end of input or the closing brace line.
//
static buffer
parse_message (istream& is)
{
  buffer r;

  for (string l; getline (is, l); )
  {
    l = trim (l);

    if (l.empty ())
      continue;

    if (l == "}")
      break;

    if (l.compare (0, 2, "x ") == 0)
    {
      buffer x (parse_hex (l.substr (2)));
      r.insert (r.end (), x.begin (), x.end ());
      continue;
    }

    size_t p (l.find (' '));
    if (p == string::npos)
      throw runtime_error ("invalid line '" + l + '\'');

    uint32_t n (uint32_t (stoul (l.substr (0, p))));
    string   v (l.substr (p + 1));

    if (v == "{")
      append_length (r, n, parse_message (is));
    else if (v.front () == '\'')
      append_length (r, n, buffer (v.begin () + 1, v.end () - 1));
    else if (v.compare (0, 4, "f64 ") == 0)
    {
      append_tag (r, n, wire_type::fixed64);
      append_fixed (r, stoull (v.substr (4)), 8);
    }
    else if (v.compare (0, 4, "f32 ") == 0)
    {
      append_tag (r, n, wire_type::fixed32);
      append_fixed (r, stoull (v.substr (4)), 4);
    }
    else
    {
      append_tag (r, n, wire_type::varint);
      append_varint (r, stoull (v));
    }
  }

  return r;
}

// Parse the frame text into the message header, an empty protobuf header,
// and the body.
//
static buffer
parse_frame (istream& is)
{
  uint32_t e (742);
  bool     plain (false);

  if (is.peek () == 'e')
  {
    string l;
    getline (is, l);

    e = uint32_t (stoul (l.substr (5)));
    plain = l.find ("plain") != string::npos;
  }

  buffer r;
  append_fixed (r, e | (plain ? 0 : frame_protobuf_flag), 4);
  append_fixed (r, 0, 4); // Protobuf header size.

  buffer b (parse_message (is));
  r.insert (r.end (), b.begin (), b.end ());
  return r;
}

// Schema position of a message being printed. The text context is for a
// length-delimited field that is not a message.
//
enum class context {body, game, process_info, text};

// Return the context of the length-delimited field n in context c.
//
static context
child (context c, uint32_t n)
{
  if (c == context::body && n == 1)
    return context::game;

  if (c == context::game && n == 32)
    return context::process_info;

  return context::text;
}

// Print the message fields in the frame text format.
//
static void
print_message (ostream& os, bytes m, context c, size_t indent)
{
  wire_reader r {m};
  wire_field  f;
  string      i (indent, ' ');

  while (read_field (r, f))
  {
    os << i << f.number << ' ';

    switch (f.type)
    {
    case wire_type::varint:  os << f.value << '\n'; break;
    case wire_type::fixed64: os << "f64 " << f.value << '\n'; break;
    case wire_type::fixed32: os << "f32 " << f.value << '\n'; break;
    default:
    {
      context x (child (c, f.number));

      if (x == context::text)
      {
        os << '\'' << string (f.payload.begin (), f.payload.end ())
           << "'\n";
      }
      else
      {
        os << "{\n";
        print_message (os, f.payload, x, indent + 2);
        os << i << "}\n";
      }
    }
    }
  }

  assert (!r.failed);
}

// Parse <pid>=<app>:<name> or <pid>=<app>/<hex>.
//
static pair<process_id, registration>
parse_registration (const char* a)
{
  const char* e (strchr (a, '='));
  const char* c (e != nullptr ? strpbrk (e, ":/") : nullptr);

  if (c == nullptr)
    throw runtime_error (string ("invalid registration '") + a + '\'');

  string n (*c == ':' ? string (c + 1) : string ());

  if (*c == '/')
  {
    buffer h (parse_hex (c + 1));
    n.assign (h.begin (), h.end ());
  }

  if (n.size () >= sizeof (registration::extra_info))
    throw runtime_error (string ("name too long in '") + a + '\'');

  registration r {};
  r.app = app_id (stoul (string (e + 1, c)));
  r.extra_info_size = uint8_t (n.size ());
  memcpy (r.extra_info, n.data (), n.size ());

  assert (valid (r));
  return {process_id (stoul (string (a, e))), r};
}

int
main (int argc, char* argv[])
try
{
  size_t capacity (frame_capacity);
  int i (1);

  if (argc > 2 && string (argv[1]) == "--capacity")
  {
    capacity = stoul (argv[2]);
    i = 3;
  }

  for (; i != argc; ++i)
    registrations.push_back (parse_registration (argv[i]));

  buffer f (parse_frame (cin));
  buffer o (capacity);

  size_t n (rewrite_frame (f, &lookup, o));
  if (n == 0)
    return 1;

  assert (memcmp (o.data (), f.data (), frame_header_size) == 0);
  assert (classify (bytes (o.data (), n)) == frame_kind::games_played);

  print_message (cout,
                 bytes (o.data () + frame_header_size,
                        n - frame_header_size),
                 context::body,
                 0);
  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 2;
}
