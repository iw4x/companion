// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/hook-win32.hxx>

#include <libcompanion/utility-win32.hxx>

#include <libcompanion/minhook/minhook.h>

#include <cstdint>
#include <cstddef>     // offsetof
#include <string_view>

#include <libcompanion/hook.hxx>
#include <libcompanion/name.hxx>
#include <libcompanion/types.hxx>
#include <libcompanion/resend.hxx>
#include <libcompanion/contract.hxx>
#include <libcompanion/protocol.hxx>
#include <libcompanion/registration.hxx>

using namespace std;

namespace companion
{
  // Trampoline to the original function (created by MinHook) and the
  // process-wide resend state.
  //
  static send_frame_function original_function;
  static resend_state        pending;

  // Windows implementation of send_platform.
  //
  struct win32_platform
  {
    static constexpr string_view diag_prefix {companion::diag_prefix};

    static bool
    original (void* c, uint32_t op, const uint8_t* d, uint32_t n)
    {
      return original_function (c, op, d, n);
    }

    // Read the registration from the game process's record mapping. The
    // game keeps the mapping open for as long as it is registered, so the
    // mapping exists only for a live registration.
    //
    static bool
    lookup (process_id p, registration& r) noexcept
    {
      uint8_t b[sizeof (record)];

      return snapshot_mapping (record_name (p).data (),
                               offsetof (record, magic),
                               mutable_bytes (b)) &&
             decode_record (bytes (b), p, r) == registration_outcome::valid;
    }

    static thread_id
    current_thread () noexcept
    {
      return thread_id (GetCurrentThreadId ());
    }

    static timestamp
    now () noexcept
    {
      return timestamp (static_cast<timestamp::rep> (GetTickCount64 ()));
    }

    static void
    write_diag (const diag_line& l) noexcept
    {
      companion::write_diag (l);
    }
  };

  static_assert (send_platform<win32_platform>);

  // Detour function. It is called by Steam, so it doesn't throw.
  //
  static bool
  detour (void* c, uint32_t op, const uint8_t* d, uint32_t n) noexcept
  {
    return send_frame<win32_platform> (pending, c, op, d, n);
  }

  bool
  install_send_hook (void* t) noexcept
  {
    LIBCOMPANION_PRE (t != nullptr);

    mh_status s (mh_initialize ());

    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED)
    {
      diag ("unable to initialize MinHook: {}", mh_status_to_string (s));
      return false;
    }

    void* o;
    s = mh_create_hook (t, reinterpret_cast<void*> (&detour), &o);

    if (s != MH_OK)
    {
      diag ("unable to create the send hook: {}", mh_status_to_string (s));
      return false;
    }

    // Set the trampoline before enabling the hook since the first frame can
    // arrive right after.
    //
    original_function = reinterpret_cast<send_frame_function> (o);

    s = mh_enable_hook (t);

    if (s != MH_OK)
    {
      diag ("unable to enable the send hook: {}", mh_status_to_string (s));
      mh_remove_hook (t);
      return false;
    }

    return true;
  }

  // Record watcher thread. Signal a change to the resend logic every time
  // the event is set.
  //
  static DWORD WINAPI
  watch_records (void* e) noexcept
  {
    while (WaitForSingleObject (e, INFINITE) == WAIT_OBJECT_0)
    {
      if (note_change (pending))
        diag ("record changed");
    }

    diag ("record watcher stopped ({})", GetLastError ());
    return 0;
  }

  bool
  start_record_watcher () noexcept
  {
    object_name n (record_event_name (process_id (GetCurrentProcessId ())));

    auto_handle e (CreateEventW (nullptr,
                                 FALSE, // Auto-reset.
                                 FALSE, // Initially non-signaled.
                                 n.data ()));

    if (e.handle == nullptr)
    {
      diag ("unable to create the record change event ({})", GetLastError ());
      return false;
    }

    auto_handle t (
      CreateThread (nullptr, 0, &watch_records, e.handle, 0, nullptr));

    if (t.handle == nullptr)
    {
      diag ("unable to start the record watcher ({})", GetLastError ());
      return false;
    }

    // The watcher thread uses the event until the Steam client exits, so
    // keep the handle open.
    //
    release (e);
    return true;
  }
}
