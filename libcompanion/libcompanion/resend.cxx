// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/resend.hxx>

#include <cstring> // memcpy(), memcmp()

#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  // Return true if the state is consistent (invariant helper).
  //
  [[maybe_unused]] static bool
  consistent (const resend_state& s) noexcept
  {
    return s.report.size  <= frame_capacity &&
           s.sent.size    <= frame_capacity &&
           s.scratch.size <= frame_capacity &&
           (s.report.size != 0 || s.sent.size == 0);
  }

  static void
  assign (frame_copy& c, bytes b) noexcept
  {
    LIBCOMPANION_PRE (b.size () <= frame_capacity);

    if (!b.empty ())
      memcpy (c.data, b.data (), b.size ());

    c.size = static_cast<uint32_t> (b.size ());
  }

  static bytes
  view (const frame_copy& c) noexcept
  {
    return bytes (c.data, c.size);
  }

  // Clear the stored report and the frame sent for it.
  //
  static void
  forget_report (resend_state& s) noexcept
  {
    s.report.size = 0;
    s.sent.size = 0;
    s.report_connection.store (connection {}, memory_order_relaxed);
  }

  void
  note_report (resend_state& s,
               connection c,
               bytes r,
               bytes x,
               thread_id t,
               timestamp now) noexcept
  {
    LIBCOMPANION_PRE (!r.empty ());

    lock_guard<mutex> l (s.mutex);
    LIBCOMPANION_INVARIANT (consistent (s));

    // A new report always replaces the stored one since it describes what
    // Steam considers running now.
    //
    if (r.size () > frame_capacity || x.size () > frame_capacity)
    {
      forget_report (s);
      return;
    }

    assign (s.report, r);
    assign (s.sent, x);
    s.report_thread = t;
    s.last_seen.store (now.count (), memory_order_relaxed);
    s.report_connection.store (c, memory_order_relaxed);
  }

  bool
  note_change (resend_state& s) noexcept
  {
    return !s.changed.exchange (true, memory_order_relaxed);
  }

  // Implementation of resend() that runs with the mutex locked.
  //
  static resend_result
  resend_locked (resend_state& s,
                 thread_id t,
                 timestamp now,
                 resend_rewrite rw,
                 resend_send sd) noexcept
  {
    LIBCOMPANION_INVARIANT (consistent (s));

    resend_result r {};
    r.report_connection = s.report_connection.load (memory_order_relaxed);

    // Check the flag again since another thread may have handled the
    // change while we were waiting for the mutex.
    //
    if (!s.changed.load (memory_order_relaxed))
      return r;

    if (s.report.size == 0)
    {
      s.changed.store (false, memory_order_relaxed);
      r.outcome = resend_outcome::no_report;
      return r;
    }

    if (t != s.report_thread)
    {
      r.outcome = resend_outcome::other_thread;
      return r;
    }

    s.changed.store (false, memory_order_relaxed);

    r.report_size = s.report.size;
    r.idle = now - timestamp (s.last_seen.load (memory_order_relaxed));

    if (r.idle > max_report_idle)
    {
      forget_report (s);
      r.outcome = resend_outcome::stale;
      return r;
    }

    s.scratch.size = static_cast<uint32_t> (
      rw (view (s.report), mutable_bytes (s.scratch.data)));

    if (s.scratch.size == 0)
    {
      r.outcome = resend_outcome::no_match;
      return r;
    }

    if (s.scratch.size == s.sent.size &&
        memcmp (s.scratch.data, s.sent.data, s.sent.size) == 0)
    {
      r.outcome = resend_outcome::unchanged;
      return r;
    }

    sd (r.report_connection, view (s.scratch));
    assign (s.sent, view (s.scratch));

    r.resent_size = s.sent.size;
    r.outcome = resend_outcome::resent;
    return r;
  }

  resend_result
  resend (resend_state& s,
          connection c,
          thread_id t,
          timestamp now,
          resend_rewrite rw,
          resend_send sd) noexcept
  {
    LIBCOMPANION_PRE (rw != nullptr && sd != nullptr);

    // This runs for every frame Steam sends, so the common case (no pending
    // change) doesn't lock the mutex. The connection and the time are only
    // used to detect an idle connection, so relaxed ordering is sufficient.
    //
    if (c == s.report_connection.load (memory_order_relaxed))
      s.last_seen.store (now.count (), memory_order_relaxed);

    if (!s.changed.load (memory_order_relaxed)) [[likely]]
      return resend_result {};

    lock_guard<mutex> l (s.mutex);
    return resend_locked (s, t, now, rw, sd);
  }
}
