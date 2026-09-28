// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <mutex>
#include <atomic>
#include <cstdint>
#include <cstddef> // size_t

#include <libcompanion/types.hxx>
#include <libcompanion/games-played.hxx> // frame_capacity

namespace companion
{
  // GamesPlayed report resending.
  //
  // Steam reports a game right after starting it, normally before the game
  // publishes its record. After that Steam only sends a new report when the
  // set of running games changes. The game can also update its record later
  // (for example, to switch to the fallback app id). To handle this we keep
  // the last report as well as the frame that was actually sent for it. On a
  // record change we rewrite the report again and, if the result differs
  // from the frame the CM server last received, send it on the report's
  // connection.
  //
  // We don't have a thread in Steam's networking code and the CM connection
  // can be idle until the next heartbeat. So the resend is performed when
  // the next frame is sent on the thread that sent the last report. Steam
  // uses a single thread per connection, so our send never interleaves with
  // Steam's own sends on that connection. Also, we only resend while the
  // report's connection is in use.
  //
  // This code is platform-independent: the send hook provides the thread
  // ids, the time, and the rewrite and send functions (see hook.hxx).
  //

  // Maximum time without frames on the report's connection after which we
  // assume the connection is closed and drop the report.
  //
  inline constexpr timestamp max_report_idle (60000);

  enum class resend_outcome: std::uint8_t
  {
    idle,         // No pending record change.
    no_report,    // Record changed before Steam sent any report.
    other_thread, // Frame from another thread (the change stays pending).
    stale,        // Report's connection idle for too long (report dropped).
    no_match,     // Report has no entries of registered processes.
    unchanged,    // Rewritten report equals the last sent frame.
    resent        // Rewritten report was sent.
  };

  // Resend attempt result. The sizes and idle time are set for the
  // outcomes where they apply.
  //
  struct resend_result
  {
    resend_outcome outcome;
    connection     report_connection;
    std::uint32_t  report_size;
    std::uint32_t  resent_size;
    timestamp      idle;
  };

  // Stored frame of up to frame_capacity bytes.
  //
  struct frame_copy
  {
    std::uint32_t size;
    std::uint8_t  data[frame_capacity];
  };

  // Resend state of a send hook.
  //
  // The atomic members are accessed without the mutex when each frame is
  // sent. The remaining members are only accessed with the mutex locked.
  //
  struct resend_state
  {
    std::atomic<bool>                changed {false};
    std::atomic<connection>          report_connection {};
    std::atomic<timestamp::rep>      last_seen {0};

    std::mutex mutex;
    thread_id  report_thread {};
    frame_copy report {};  // Last report as sent by Steam.
    frame_copy sent {};    // Frame that was actually sent for the report.
    frame_copy scratch {}; // Rewrite buffer for the resend.
  };

  // Rewrite the report into the buffer and return the result size or 0 if
  // the report has no entries of registered processes (see rewrite_frame()).
  //
  using resend_rewrite = std::size_t (*) (bytes, mutable_bytes) noexcept;

  // Transmit the frame on the connection (through the original function).
  //
  using resend_send = void (*) (connection, bytes) noexcept;

  // Record the GamesPlayed report that Steam sent on the connection along
  // with the frame that was actually sent (the rewrite or the report
  // itself). If either exceeds frame_capacity, then clear the stored report
  // since a stale one would be incorrect to resend.
  //
  void
  note_report (resend_state&,
               connection,
               bytes report,
               bytes sent,
               thread_id,
               timestamp now) noexcept;

  // Signal a record publication, update, or removal. This function can be
  // called from any thread. Return false if a change is already pending.
  //
  bool
  note_change (resend_state&) noexcept;

  // Handle a non-report frame that Steam sent on the connection by
  // resending the last report if there is a pending change and the frame is
  // from the report's thread.
  //
  resend_result
  resend (resend_state&,
          connection,
          thread_id,
          timestamp now,
          resend_rewrite,
          resend_send) noexcept;
}
