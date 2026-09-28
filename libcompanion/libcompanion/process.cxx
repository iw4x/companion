// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/process.hxx>

#include <charconv>    // from_chars()
#include <concepts>
#include <algorithm>   // min()
#include <string_view>

#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  // Parse s as a decimal number. The number must span the entire string
  // (no sign or whitespace) and fit into T.
  //
  template <unsigned_integral T>
  static bool
  parse_decimal (string_view s, T& v) noexcept
  {
    const char*       e (s.data () + s.size ());
    from_chars_result r (from_chars (s.data (), e, v));

    return r.ec == errc () && r.ptr == e;
  }

  // /proc/<pid>/stat fields that we use.
  //
  struct process_stat
  {
    process_id parent;
    start_time start;
  };

  // Field numbers (1-based as in proc(5)).
  //
  static constexpr size_t stat_command_field (2);
  static constexpr size_t stat_parent_field  (4);
  static constexpr size_t stat_start_field   (22);

  // Parse the /proc/<pid>/stat content, which is a single line of fields
  // separated with single spaces.
  //
  static bool
  parse_stat (string_view s, process_stat& r) noexcept
  {
    // The command is enclosed in parentheses and can contain any characters
    // (including spaces, parentheses, and newlines) since the process sets
    // it and the kernel doesn't escape it. All the subsequent fields are
    // numbers, so we count them from the last closing parenthesis.
    //
    size_t p (s.rfind (')'));

    if (p == string_view::npos)
      return false;

    s.remove_prefix (p + 1);

    uint32_t parent (0);

    for (size_t f (stat_command_field + 1); ; ++f)
    {
      if (s.empty () || s[0] != ' ')
        return false;

      s.remove_prefix (1);

      size_t      e (s.find_first_of (" \n"));
      string_view v (s.substr (0, e));

      if (f == stat_parent_field && !parse_decimal (v, parent))
        return false;

      if (f == stat_start_field)
      {
        uint64_t t;

        if (!parse_decimal (v, t))
          return false;

        r = process_stat {process_id (parent), start_time (t)};
        return true;
      }

      if (e == string_view::npos)
        return false;

      s.remove_prefix (e);
    }
  }

  // Process pids, one per pid namespace, from the outermost namespace in.
  //
  struct namespace_ids
  {
    uint8_t    count;
    process_id items[namespace_capacity];
  };

  // Parse the NSpid line of the /proc/<pid>/status content. The line starts
  // with "NSpid:" followed by tab-separated pids. Return false if there is
  // no such line (Linux before 4.1), it is truncated (the content didn't
  // fit), or it is malformed. The result is only changed on success.
  //
  static bool
  parse_namespace_ids (string_view s, namespace_ids& r) noexcept
  {
    static constexpr string_view key ("\nNSpid:");

    size_t p (s.find (key));

    if (p == string_view::npos)
      return false;

    s.remove_prefix (p + key.size ());

    // The kernel terminates every line with a newline, so a line without it
    // is truncated.
    //
    size_t e (s.find ('\n'));

    if (e == string_view::npos)
      return false;

    s = s.substr (0, e);

    namespace_ids x {0, {}};

    while (!s.empty ())
    {
      if (s[0] != '\t' || x.count == namespace_capacity)
        return false;

      s.remove_prefix (1);

      size_t   t (min (s.find ('\t'), s.size ()));
      uint32_t v;

      if (!parse_decimal (s.substr (0, t), v) || v == 0)
        return false;

      x.items[x.count++] = process_id (v);
      s.remove_prefix (t);
    }

    if (x.count == 0)
      return false;

    r = x;
    return true;
  }

  // Breadth-first search queue. The processes before head have been
  // searched. Each process is added at most once, so the queue also serves
  // as the set of the processes seen so far.
  //
  struct process_queue
  {
    uint16_t   head;
    uint16_t   count;
    bool       overflow;
    process_id items[search_capacity];
  };

  // Add the process unless it is already in the queue. If the queue is full,
  // then set the overflow flag.
  //
  static void
  enqueue (process_queue& q, process_id p) noexcept
  {
    for (uint16_t i (0); i != q.count; ++i)
    {
      if (q.items[i] == p)
        return;
    }

    if (q.count == search_capacity)
    {
      q.overflow = true;
      return;
    }

    q.items[q.count++] = p;
  }

  // Parse the children lists (each pid is followed by a space) and add the
  // children to the queue. If the content is incomplete, then the last pid
  // without the trailing space may be truncated and is skipped.
  //
  static void
  parse_children (string_view s, bool complete, process_queue& q) noexcept
  {
    while (!s.empty ())
    {
      size_t e (s.find (' '));

      if (e == string_view::npos)
      {
        if (!complete)
          break;

        e = s.size ();
      }

      uint32_t v;

      if (parse_decimal (s.substr (0, e), v) && v != 0)
        enqueue (q, process_id (v));

      s.remove_prefix (min (e + 1, s.size ()));
    }
  }

  using read_function = size_t (*) (process_id, mutable_bytes) noexcept;

  // Read the file of the process into the buffer and return the part that
  // fits. Set complete to false if the file didn't fit.
  //
  static string_view
  read (read_function f, process_id p, mutable_bytes b, bool& complete)
    noexcept
  {
    size_t n (f (p, b));

    complete = n <= b.size ();
    return string_view (reinterpret_cast<const char*> (b.data ()),
                        min (n, b.size ()));
  }

  // Read and parse /proc/<pid>/stat.
  //
  static bool
  read_stat (const process_source& s,
             process_id p,
             mutable_bytes b,
             process_stat& r) noexcept
  {
    bool c;
    string_view v (read (s.stat, p, b, c));

    return c && parse_stat (v, r);
  }

  // Check whether p is self (the Steam client) or lies on the parent chain
  // from self up to init. Pid 1 always qualifies.
  //
  static bool
  related (const process_source& s,
           process_id self,
           process_id p,
           mutable_bytes b) noexcept
  {
    if (value (p) == 1)
      return true;

    process_id c (self);

    for (size_t i (0); i != ancestor_capacity && value (c) > 1; ++i)
    {
      if (c == p)
        return true;

      process_stat x;

      if (!read_stat (s, c, b, x))
        break;

      c = x.parent;
    }

    return false;
  }

  // Look up the process's record under each of its pids.
  //
  static bool
  find_record (const process_source& s,
               process_id p,
               mutable_bytes b,
               registration& r) noexcept
  {
    process_stat x;

    if (!read_stat (s, p, b, x))
      return false;

    // If there is no NSpid line, then the process is only in our namespace.
    //
    namespace_ids n {1, {p}};
    bool          c;
    string_view   v (read (s.status, p, b, c));

    parse_namespace_ids (v, n);

    for (uint8_t i (0); i != n.count; ++i)
    {
      size_t k (s.record (n.items[i], b));

      if (decode_unix_record (bytes (b.data (), min (k, b.size ())),
                              n.items[i],
                              x.start,
                              r) == registration_outcome::valid)
        return true;
    }

    return false;
  }

  const char*
  search_outcome_name (search_outcome o) noexcept
  {
    switch (o)
    {
    case search_outcome::found:    return "found";
    case search_outcome::related:  return "related";
    case search_outcome::none:     return "none";
    case search_outcome::overflow: return "overflow";
    }

    LIBCOMPANION_UNREACHABLE ();
  }

  search_outcome
  find_registration (const process_source& s,
                     process_id self,
                     process_id p,
                     registration& r) noexcept
  {
    search_outcome o (search_outcome::related);
    LIBCOMPANION_POST (o != search_outcome::found || valid (r));

    uint8_t       d[proc_file_capacity];
    mutable_bytes b (d);

    if (related (s, self, p, b))
      return o;

    process_queue q;
    q.head = 0;
    q.count = 0;
    q.overflow = false;

    enqueue (q, p);

    for (; q.head != q.count; ++q.head)
    {
      process_id c (q.items[q.head]);

      if (find_record (s, c, b, r))
        return o = search_outcome::found;

      bool        f;
      string_view v (read (s.children, c, b, f));

      parse_children (v, f, q);
    }

    return o = q.overflow ? search_outcome::overflow : search_outcome::none;
  }
}
