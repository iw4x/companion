// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <cstddef> // size_t, offsetof

// Game-companion protocol.
//
// The protocol is shared by three components: the game (the 32-bit
// iw4x.dll), the companion in the 64-bit Windows Steam client, and the
// companion preloaded into the 32-bit native Linux Steam client (used when
// the game runs under Proton). Each is built with its own toolchain for its
// own architecture and each is upgraded on its own schedule. The companion
// remains loaded until the Steam client exits, so any game version can
// encounter any companion version.
//
// For this reason every structure below has a fixed layout of 32-bit fields
// only (MSVC aligns a 64-bit integer to 8 bytes while the i386 System V ABI
// aligns it to 4) and starts with a magic, a version, and its size. A reader
// accepts only the exact layout it knows and every layout change increments
// the version.
//
// This header only depends on the standard library so that the game can
// include it directly.
//
namespace companion
{
  // Application ids that a registered game reports. The CM server shows
  // friends only the games that the account owns, with game_extra_info as
  // the title. The game starts with Modern Warfare 2 and, if Steam reports
  // that the account doesn't own it, switches to Spacewar, which every
  // account owns.
  //
  inline constexpr std::uint32_t preferred_app_id (10190);
  inline constexpr std::uint32_t fallback_app_id  (480);

  // Size of game_extra_info with the NUL (Steam's k_cchGameExtraInfoMax).
  //
  inline constexpr std::size_t extra_info_capacity (64);

  // Companion build number. It is incremented with every change of the
  // companion behavior that the game can observe.
  //
  inline constexpr std::uint32_t companion_build (2);

  // Windows registration record.
  //
  // The game publishes the record in a named file mapping (see
  // record_name_format) whose name includes the game's process id. The
  // mapping lives while the game keeps a handle to it, so the record's
  // lifetime is bounded by the game process.
  //
  // The game writes the magic last with release ordering and the companion
  // reads it first with acquire ordering. As a result, a record with the
  // matching magic is always complete.
  //
  inline constexpr std::uint32_t record_magic   (0x50345749); // "IW4P"
  inline constexpr std::uint32_t record_version (1);

  struct record
  {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t size;
    std::uint32_t process_id; // Equals the pid in the mapping name.
    std::uint32_t app_id;     // Can be updated while the game runs.
    char          extra_info[extra_info_capacity]; // UTF-8, NUL-terminated.
  };

  static_assert (sizeof (record) == 20 + extra_info_capacity);
  static_assert (offsetof (record, app_id) == 16);
  static_assert (offsetof (record, extra_info) == 20);

  // Linux registration record.
  //
  // The native Steam client cannot see Wine's named objects (they exist in
  // wineserver). So a game running under Wine also writes its record to a
  // file in /dev/shm, which is shared between the Proton container and the
  // host.
  //
  // Steam identifies the game by its Linux pid, which a Windows program has
  // no way to obtain: Wine performs file operations in wineserver, so a
  // read of /proc/self from the game returns the wineserver of the game's
  // prefix. So the record is keyed by the wineserver pid. Proton starts the
  // wineserver in the process tree that Steam launched (which is where the
  // companion searches) and each prefix is used by a single game.
  //
  // The record also contains the wineserver start time, which prevents a
  // file left over from a crashed game from matching a new process with the
  // same pid.
  //
  inline constexpr std::uint32_t unix_record_magic   (0x55345749); // "IW4U"
  inline constexpr std::uint32_t unix_record_version (1);

  struct unix_record
  {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t size;
    std::uint32_t process_id;      // Wineserver pid (game's pid namespace).
    std::uint32_t start_time_low;  // Wineserver /proc/<pid>/stat field 22.
    std::uint32_t start_time_high;
    std::uint32_t app_id;          // Can be updated while the game runs.
    char          extra_info[extra_info_capacity]; // UTF-8, NUL-terminated.
  };

  static_assert (sizeof (unix_record) == 28 + extra_info_capacity);
  static_assert (offsetof (unix_record, app_id) == 24);
  static_assert (offsetof (unix_record, extra_info) == 28);

  // Companion state as published in the status.
  //
  enum class companion_state: std::uint32_t
  {
    starting    = 0, // Initializing.
    active      = 1, // Hook installed, registrations are applied.
    unsupported = 2, // Unrecognized Steam client build.
    failed      = 3  // Hook installation failed.
  };

  // Windows companion status.
  //
  // The companion publishes the status in a named file mapping (see
  // status_name_format) whose name includes the Steam process id. Creating
  // the mapping also ensures there is a single companion per Steam client.
  // The companion stores the state field last with release ordering.
  //
  inline constexpr std::uint32_t status_magic   (0x53345749); // "IW4S"
  inline constexpr std::uint32_t status_version (1);

  struct status
  {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t size;
    std::uint32_t state;    // companion_state
    std::uint32_t build;    // Running companion's companion_build.
    std::uint32_t hook;     // Hooked function RVA (steamclient64.dll) or 0.
  };

  static_assert (sizeof (status) == 24);
  static_assert (offsetof (status, state) == 12);

  // Linux companion status. There is one status file per user.
  //
  inline constexpr std::uint32_t unix_status_magic   (0x54345749); // "IW4T"
  inline constexpr std::uint32_t unix_status_version (1);

  struct unix_status
  {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t size;
    std::uint32_t state;       // companion_state
    std::uint32_t build;       // Running companion's companion_build.
    std::uint32_t hook;        // Hooked function offset (steamclient.so) or 0.
    std::uint32_t steam_pid;   // Steam client pid in the host namespace.
  };

  static_assert (sizeof (unix_status) == 28);

  // Attach result, which becomes the rundll32.exe exit code (see
  // attach.hxx).
  //
  enum class attach_result: std::uint32_t
  {
    active            = 0,
    unsupported       = 1,
    failed            = 2,
    invalid_arguments = 3,
    not_steam         = 4,
    access_denied     = 5,
    injection_failed  = 6,
    timeout           = 7
  };

  // Companion module file name (the module is installed next to iw4x.dll)
  // and attach entry point name. Note that rundll32 first looks for the
  // export with the W suffix and passes it the command line in UTF-16.
  //
  inline constexpr const wchar_t* companion_module_name (L"iw4x-steam64.dll");
  inline constexpr const char*    attach_entry_point    ("IW4xAttach");

  // Windows object name formats. The game formats them with swprintf() and
  // an unsigned long. The names use the Local\ prefix, which is sufficient
  // since the game and the Steam client always share the interactive
  // session.
  //
  // The record change event is an auto-reset event whose name includes the
  // Steam process id. The game sets it every time it publishes or updates
  // its record, which allows the companion to resend a report that Steam
  // sent before the record was published.
  //
  inline constexpr const wchar_t* record_name_format (
    L"Local\\IW4x.SteamPresence.%lu");

  inline constexpr const wchar_t* status_name_format (
    L"Local\\IW4x.SteamCompanion.%lu");

  inline constexpr const wchar_t* record_event_format (
    L"Local\\IW4x.SteamPresence.Changed.%lu");

  inline constexpr std::size_t object_name_capacity (64);

  // Linux file paths as snprintf() formats with an unsigned int argument
  // (the pid for the record and the uid for the status). The game opens
  // these files using Wine's \\?\unix path prefix.
  //
  inline constexpr const char* unix_record_format (
    "/dev/shm/iw4x-presence.%u");

  inline constexpr const char* unix_status_format (
    "/dev/shm/iw4x-steam-companion.%u");

  inline constexpr const char* unix_record_directory ("/dev/shm");
  inline constexpr const char* unix_record_prefix    ("iw4x-presence.");

  // Game-side paths of the same files as swprintf() formats with an
  // unsigned int argument.
  //
  inline constexpr const wchar_t* wine_record_format (
    L"\\\\?\\unix\\dev\\shm\\iw4x-presence.%u");

  inline constexpr const wchar_t* wine_status_format (
    L"\\\\?\\unix\\dev\\shm\\iw4x-steam-companion.%u");

  inline constexpr std::size_t unix_path_capacity (64);
}
