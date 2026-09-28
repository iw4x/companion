// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <format>
#include <concepts>
#include <string_view>

#include <libcompanion/types.hxx>
#include <libcompanion/diagnostics.hxx>

// Linux utilities shared by the parts of Companion module that run in
// the Steam client (host-linux.cxx, hook-linux.cxx, and process-linux.cxx).
//
namespace companion
{
  // Diagnostics.
  //
  // The diagnostics are written to stderr, which Companion shares with
  // the Steam client. It is the terminal Steam was started from or wherever
  // the Steam launcher redirects it.
  //
  inline constexpr std::string_view diag_prefix ("[iw4x-steam] ");

  void
  write_diag (const diag_line&) noexcept;

  template <diag_argument... A>
  void
  diag (std::format_string<A...>, A&&...) noexcept;

  // Error number (errno value). Its formatter prints the error description
  // (see below), which is what diagnostics need.
  //
  enum class error_number: int {};

  error_number
  last_error () noexcept;

  // Return the process id of the calling process in its own pid namespace.
  //
  process_id
  current_process () noexcept;

  // RAII type for file descriptors.
  //
  struct auto_fd
  {
    int fd;

    explicit
    auto_fd (int f = -1) noexcept: fd (f) {}

    ~auto_fd () noexcept;

    auto_fd (const auto_fd&) = delete;
    auto_fd& operator= (const auto_fd&) = delete;
  };

  // Release the ownership of the descriptor and return it. The descriptor
  // then remains open until the process exits.
  //
  int
  release (auto_fd&) noexcept;

  // Start a detached thread that calls the function with the argument. On
  // failure, set errno and return false.
  //
  // The thread blocks all the signals. Companion runs in the Steam
  // client's process, so signals sent to the process are for the Steam
  // threads, which may rely on which threads can receive them.
  //
  bool
  start_thread (void* (*) (void*), void*) noexcept;

  namespace details
  {
    template <typename C>
    concept char_format_context =
      std::same_as<typename C::char_type, char> &&
      requires (C& c) {c.out ();};
  }
}

// Format the error number as its description, same as strerror() but
// thread-safe.
//
template <>
struct std::formatter<companion::error_number>:
  std::formatter<std::string_view>
{
  template <companion::details::char_format_context C>
  typename C::iterator
  format (companion::error_number, C&) const;
};

#include <libcompanion/utility-linux.txx>
