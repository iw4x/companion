// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <format>
#include <cstddef> // size_t
#include <utility> // forward()

namespace companion
{
  namespace details
  {
    // Format the diagnostics line and write it to the platform sink.
    //
    template <send_platform P, diag_argument... A>
    void
    send_diag (std::format_string<A...> f, A&&... a) noexcept
    {
      P::write_diag (format_diag (P::diag_prefix, f, std::forward<A> (a)...));
    }

    // Issue diagnostics for the resend outcomes that are worth reporting.
    // The rest happen for almost every frame and are not reported.
    //
    template <send_platform P>
    void
    diag_resend (const resend_result& r, connection c) noexcept
    {
      switch (r.outcome)
      {
      case resend_outcome::stale:
        {
          send_diag<P> ("record changed but the connection of the last "
                        "report was idle for {} ms, dropped",
                        r.idle.count ());
          break;
        }
      case resend_outcome::no_match:
        {
          send_diag<P> ("record changed but the last report matches no "
                        "record");
          break;
        }
      case resend_outcome::resent:
        {
          send_diag<P> ("resent GamesPlayed on connection {:#x} ({} -> {} "
                        "bytes, after a frame on {:#x})",
                        static_cast<std::uintptr_t> (r.report_connection),
                        r.report_size,
                        r.resent_size,
                        static_cast<std::uintptr_t> (c));
          break;
        }
      case resend_outcome::idle:
      case resend_outcome::no_report:
      case resend_outcome::other_thread:
      case resend_outcome::unchanged:
        break;
      }
    }

    // Adapters that bind P for the resend logic, which accepts plain
    // function pointers (see resend.hxx).
    //
    template <send_platform P>
    std::size_t
    rewrite (bytes f, mutable_bytes o) noexcept
    {
      return rewrite_frame (f, &P::lookup, o);
    }

    template <send_platform P>
    void
    send (connection c, bytes f) noexcept
    {
      P::original (reinterpret_cast<void*> (static_cast<std::uintptr_t> (c)),
                   websocket_binary,
                   f.data (),
                   static_cast<std::uint32_t> (f.size ()));
    }
  }

  template <send_platform P>
  bool
  send_frame (resend_state& s,
              void* c,
              std::uint32_t op,
              const std::uint8_t* d,
              std::uint32_t n) noexcept
  {
    // Only non-empty binary frames can contain CM messages.
    //
    if (op != websocket_binary || d == nullptr || n == 0)
      return P::original (c, op, d, n);

    connection k (
      static_cast<connection> (reinterpret_cast<std::uintptr_t> (c)));
    bytes      f (d, n);

    // For any other binary frame, send it first and then let the resend
    // logic decide whether to send the report again.
    //
    if (classify (f) != frame_kind::games_played)
    {
      bool r (P::original (c, op, d, n));

      details::diag_resend<P> (resend (s,
                                       k,
                                       P::current_thread (),
                                       P::now (),
                                       &details::rewrite<P>,
                                       &details::send<P>),
                               k);
      return r;
    }

    // Rewrite into a stack buffer. The original function copies the frame
    // into the connection send buffer before returning, so the buffer only
    // needs to outlive the call. If the rewrite fails, then send the frame
    // as is.
    //
    std::uint8_t b[frame_capacity];
    std::size_t  m (details::rewrite<P> (f, mutable_bytes (b)));
    bytes        x (m != 0 ? bytes (b, m) : f);

    // Remember the report (both forms) for a potential resend.
    //
    note_report (s, k, f, x, P::current_thread (), P::now ());

    if (m != 0)
      details::send_diag<P> ("rewrote GamesPlayed ({} -> {} bytes)", n, m);

    return P::original (c,
                        op,
                        x.data (),
                        static_cast<std::uint32_t> (x.size ()));
  }
}
