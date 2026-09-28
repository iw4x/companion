// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <span>
#include <chrono>
#include <cstdint>
#include <cstddef>

namespace companion
{
  // Integer identities.
  //
  // Each identity that the host or the wire represents as a bare integer is
  // an enumeration of the same width. A conversion to or from the integer is
  // always explicit (see value() below) and two different identities never
  // convert into each other.
  //

  // Process id in the pid namespace of the observing process. On Linux this
  // is the Steam client's namespace.
  //
  enum class process_id: std::uint32_t {};

  // Linux user id. Companion status file path includes it.
  //
  enum class user_id: std::uint32_t {};

  // Linux process start time in clock ticks since boot (field 22 of
  // /proc/<pid>/stat). The value is the same in every pid namespace and
  // together with the pid identifies a process across pid reuse.
  //
  enum class start_time: std::uint64_t {};

  // Steam application id. A valid id is non-zero and fits into the 24 bits
  // of a CGameID (see valid()).
  //
  enum class app_id: std::uint32_t {};

  // Operating system thread id (GetCurrentThreadId() on Windows and gettid()
  // on Linux).
  //
  enum class thread_id: std::uint64_t {};

  // Host connection object (CWebSocketConnection) that a frame is sent on.
  // We treat it as an opaque handle that is compared and passed back to the
  // host.
  //
  enum class connection: std::uintptr_t {};

  // Monotonic time point with an unspecified epoch. Only differences between
  // two time points are meaningful.
  //
  using timestamp = std::chrono::milliseconds;

  // Raw byte views.
  //
  using bytes         = std::span<const std::uint8_t>;
  using mutable_bytes = std::span<std::uint8_t>;

  // Return true if the application id is non-zero and fits into 24 bits.
  //
  constexpr bool
  valid (app_id) noexcept;

  // Return the underlying integer value.
  //
  constexpr std::uint32_t
  value (process_id) noexcept;

  constexpr std::uint32_t
  value (user_id) noexcept;

  constexpr std::uint64_t
  value (start_time) noexcept;

  constexpr std::uint32_t
  value (app_id) noexcept;
}

#include <libcompanion/types.ixx>
