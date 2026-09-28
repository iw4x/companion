// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <cstddef> // size_t

#include <libcompanion/types.hxx>
#include <libcompanion/contract.hxx>
#include <libcompanion/protocol.hxx>

namespace companion
{
  // Protocol object names and file paths.
  //
  // Each name is derived from an id of a different kind (game process,
  // Steam process, wineserver, or user). Each function takes the id's
  // strong type, so passing the wrong kind of id is a compile error.
  //
  // The names use the formats from protocol.hxx, which the game also passes
  // to swprintf(). We substitute the id ourselves, which is independent of
  // the locale and lets the compiler verify that every name fits into its
  // capacity for any id (see name.txx).
  //
  // The result is NUL-terminated.
  //
  using object_name = std::array<wchar_t, object_name_capacity>;
  using unix_path   = std::array<char, unix_path_capacity>;

  // Name of the file mapping in which the game process publishes its
  // registration record on Windows.
  //
  object_name
  record_name (process_id game) noexcept;

  // Names of the companion's status mapping and of the event a game sets
  // after changing its record, both specific to the Steam process (Windows).
  //
  object_name
  status_name (process_id steam) noexcept;

  object_name
  record_event_name (process_id steam) noexcept;

  // Linux record file path of the game (identified by its wineserver) and
  // Linux status file path of the user.
  //
  unix_path
  unix_record_path (process_id wineserver) noexcept;

  unix_path
  unix_status_path (user_id) noexcept;

  namespace details
  {
    template <typename C>
    concept key_character = std::same_as<C, char> ||
                            std::same_as<C, wchar_t>;

    // Return the maximum formatted size, including the terminating NUL, of
    // the format with a single %u or %lu conversion, or the maximum size_t
    // value if the format contains any other conversions. A 32-bit id takes
    // at most ten digits.
    //
    template <key_character C>
    constexpr std::size_t
    key_format_size (const C*) noexcept;

    // Substitute the id into the format.
    //
    template <key_character C, std::size_t N>
    constexpr std::array<C, N>
    format_key (const C*, std::uint32_t) noexcept;
  }
}

#include <libcompanion/name.txx>
