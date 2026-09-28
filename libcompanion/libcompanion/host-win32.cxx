// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

// Windows companion host.
//
// The module is loaded by two kinds of processes: the Steam client (by the
// attach, see attach-win32.cxx), where the code below runs, and the 64-bit
// rundll32.exe that performs the attach. In any other process loading the
// module has no effect.
//
// Inside the Steam client the startup sequence is: publish the starting
// status (see protocol.hxx), find the send function in steamclient64.dll,
// install the send hook, and start the record watcher (see hook-win32.hxx).
// The companion is then idle until a game publishes its record.
//
// Creating the status mapping also ensures there is a single companion per
// Steam client. A second copy of the module (for example, from another game
// installation) finds the existing mapping and does nothing.
//
#include <libcompanion/utility-win32.hxx>

#include <atomic>
#include <cstdint>
#include <cstddef> // size_t

#include <libcompanion/pe.hxx>
#include <libcompanion/name.hxx>
#include <libcompanion/types.hxx>
#include <libcompanion/locate.hxx>
#include <libcompanion/status.hxx>
#include <libcompanion/protocol.hxx>
#include <libcompanion/hook-win32.hxx>

using namespace std;

namespace companion
{
  // Size of the image headers that we can read before the image size is
  // known. The loader always maps at least the first page of an image.
  //
  static constexpr size_t header_size (0x1000);

  // Status in the status mapping. The mapping and the view remain until the
  // Steam client exits.
  //
  static status* status_view;

  // Publish the state and the hook. The other fields are stored first and
  // the state last with release ordering (see make_status()).
  //
  static void
  publish (companion_state s, uint32_t hook = 0) noexcept
  {
    if (status_view == nullptr)
      return;

    status  x (make_status (s, hook));
    status& v (*status_view);

    v.magic   = x.magic;
    v.version = x.version;
    v.size    = x.size;
    v.build   = x.build;
    v.hook    = x.hook;

    atomic_ref<uint32_t> (v.state).store (x.state, memory_order_release);
  }

  // Create the status mapping and publish the starting state. Return false
  // if another companion already exists or on failure.
  //
  static bool
  create_status () noexcept
  {
    object_name n (status_name (process_id (GetCurrentProcessId ())));

    auto_handle m (CreateFileMappingW (INVALID_HANDLE_VALUE,
                                       nullptr,
                                       PAGE_READWRITE,
                                       0,
                                       sizeof (status),
                                       n.data ()));

    if (m.handle == nullptr)
    {
      diag ("unable to create the status mapping ({})", GetLastError ());
      return false;
    }

    if (GetLastError () == ERROR_ALREADY_EXISTS)
    {
      diag ("another companion is resident, staying inert");
      return false;
    }

    // Keep the mapping even if the view cannot be mapped below so that no
    // other copy of the module becomes the companion. The attach that
    // loaded us then times out.
    //
    HANDLE h (release (m));
    void*  v (MapViewOfFile (h, FILE_MAP_WRITE, 0, 0, sizeof (status)));

    if (v == nullptr)
    {
      diag ("unable to map the status mapping ({})", GetLastError ());
      return false;
    }

    status_view = static_cast<status*> (v);
    publish (companion_state::starting);
    return true;
  }

  // Locate the send function in the loaded steamclient64.dll. Return its RVA
  // or issue diagnostics and return 0 if the build is not recognized.
  //
  static uint32_t
  locate (const uint8_t* base) noexcept
  {
    uint32_t n (pe_image_size (bytes (base, header_size)));

    // The image description takes about 1.2K of stack, which is fine on our
    // own thread.
    //
    pe_image   x;
    pe_outcome o (parse_pe (bytes (base, n), x));

    if (o != pe_outcome::valid)
    {
      diag ("unable to parse steamclient64.dll: {}", pe_outcome_name (o));
      return 0;
    }

    locate_result r (locate_send_frame (x));

    if (r.outcome != locate_outcome::found)
    {
      diag ("unable to locate the send function: {} "
            "({} references, {} candidates)",
            locate_outcome_name (r.outcome),
            r.references,
            r.candidates);
      return 0;
    }

    diag ("send function at steamclient64.dll+{:#x} ({} references)",
          r.rva,
          r.references);

    return r.rva;
  }

  // Companion thread.
  //
  static DWORD WINAPI
  run (void*) noexcept
  {
    if (!create_status ())
      return 0;

    HMODULE m (GetModuleHandleW (steam_client_library));

    if (m == nullptr)
    {
      diag ("steamclient64.dll is not loaded");
      publish (companion_state::unsupported);
      return 0;
    }

    uint8_t* b (reinterpret_cast<uint8_t*> (m));
    uint32_t r (locate (b));

    if (r == 0)
    {
      publish (companion_state::unsupported);
      return 0;
    }

    if (!install_send_hook (b + r))
    {
      publish (companion_state::failed);
      return 0;
    }

    start_record_watcher ();

    publish (companion_state::active, r);
    diag ("active (build {})", companion_build);
    return 0;
  }

  // Return true if this process is the Steam client.
  //
  static bool
  steam_client () noexcept
  {
    wchar_t p[path_capacity];
    DWORD   n (GetModuleFileNameW (nullptr, p, path_capacity));

    return n != 0 && n < path_capacity && steam_image (wstring_view (p, n));
  }

  // Pin the module and start the companion thread. This function is called
  // with the loader lock held, which precludes doing any real work here.
  //
  static void
  start (HMODULE self) noexcept
  {
    // After the hook is installed Steam executes our code, so the module
    // must never be unloaded.
    //
    HMODULE m;
    if (!GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_PIN,
                             reinterpret_cast<LPCWSTR> (self),
                             &m))
    {
      diag ("unable to pin the module ({}), staying inert", GetLastError ());
      return;
    }

    auto_handle t (CreateThread (nullptr, 0, &run, nullptr, 0, nullptr));

    if (t.handle == nullptr)
      diag ("unable to start the companion ({})", GetLastError ());
  }
}

// Module entry point. Start the companion when loaded into the Steam client.
//
extern "C" BOOL WINAPI
DllMain (HINSTANCE m, DWORD reason, LPVOID)
{
  using namespace companion;

  if (reason == DLL_PROCESS_ATTACH)
  {
    DisableThreadLibraryCalls (m);

    if (steam_client ())
      start (m);
  }

  return TRUE;
}
