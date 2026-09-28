// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

// Attach entry point implementation.
//
// The 32-bit game starts the entry point in the 64-bit rundll32.exe since
// only a 64-bit process can create a thread in the 64-bit Steam client (see
// attach.hxx). The entry point loads this module into the Steam client by
// running LoadLibraryW() on a remote thread and then waits for Companion
// to publish a settled state (see host-win32.cxx).
//
// If Companion is already running in the Steam client (for example,
// from an earlier game session), then we report its state without loading
// anything.
//
#include <libcompanion/utility-win32.hxx>

#include <cstdint>
#include <cstddef>     // size_t, offsetof
#include <string_view>

#include <libcompanion/name.hxx>
#include <libcompanion/types.hxx>
#include <libcompanion/attach.hxx>
#include <libcompanion/status.hxx>
#include <libcompanion/protocol.hxx>

using namespace std;

namespace companion
{
  // Timeouts for the remote LoadLibraryW() call and for Companion to
  // settle as well as the status polling interval, all in milliseconds.
  //
  static constexpr DWORD load_timeout   (10000);
  static constexpr DWORD settle_timeout (5000);
  static constexpr DWORD poll_interval  (25);

  // Steam process handle access rights for CreateRemoteThread(),
  // VirtualAllocEx(), WriteProcessMemory(), QueryFullProcessImageNameW(), and
  // IsWow64Process().
  //
  static constexpr DWORD process_access (PROCESS_CREATE_THREAD     |
                                         PROCESS_QUERY_INFORMATION |
                                         PROCESS_VM_OPERATION      |
                                         PROCESS_VM_READ           |
                                         PROCESS_VM_WRITE);

  // If Companion in the Steam process has published a state other than
  // starting, then set the result and return true. Otherwise, return false.
  //
  static bool
  settled (process_id p, attach_result& r) noexcept
  {
    uint8_t b[sizeof (status)];

    if (!snapshot_mapping (status_name (p).data (),
                           offsetof (status, state),
                           mutable_bytes (b)))
      return false;

    companion_status s;
    status_outcome   o (decode_status (bytes (b), p, s));

    // Companion creates the mapping first and writes the status into it
    // afterwards, so keep waiting.
    //
    if (o == status_outcome::magic)
      return false;

    // Any other invalid status comes from an incompatible companion.
    //
    if (o != status_outcome::valid)
    {
      r = attach_result::failed;
      return true;
    }

    if (s.state == companion_state::starting)
      return false;

    r = to_attach_result (s.state);
    return true;
  }

  // Poll Companion status until it settles or the timeout expires.
  //
  static attach_result
  wait_companion (process_id p, DWORD timeout) noexcept
  {
    ULONGLONG d (GetTickCount64 () + timeout);

    for (attach_result r;; Sleep (poll_interval))
    {
      if (settled (p, r))
        return r;

      if (GetTickCount64 () >= d)
        return attach_result::timeout;
    }
  }

  // Result of the remote LoadLibraryW() call.
  //
  enum class load_outcome: uint8_t
  {
    loaded,
    unknown, // LoadLibraryW() returned 0 (see load_module()).
    failed,
    denied,
    timeout
  };

  // Load this module into the process by running LoadLibraryW() with the
  // module path on a remote thread.
  //
  static load_outcome
  load_module (HANDLE h, HMODULE self) noexcept
  {
    wchar_t f[path_capacity];
    DWORD   n (GetModuleFileNameW (self, f, path_capacity));

    if (n == 0 || n >= path_capacity)
      return load_outcome::failed;

    size_t s ((static_cast<size_t> (n) + 1) * sizeof (wchar_t));

    void* r (VirtualAllocEx (h,
                             nullptr,
                             s,
                             MEM_COMMIT | MEM_RESERVE,
                             PAGE_READWRITE));
    if (r == nullptr)
      return load_outcome::failed;

    if (!WriteProcessMemory (h, r, f, s, nullptr))
    {
      VirtualFreeEx (h, r, 0, MEM_RELEASE);
      return load_outcome::failed;
    }

    // Windows maps kernel32.dll at the same address in all the processes
    // of the same architecture until reboot (under Wine, until the
    // wineserver exits). So the address of LoadLibraryW() in this process is
    // also valid in the Steam client.
    //
    // LoadLibraryW() is called as the thread routine. Both take a single
    // pointer argument and return the result in the same register. The
    // intermediate cast to void (*) () suppresses the function type cast
    // warning.
    //
    FARPROC l (GetProcAddress (GetModuleHandleW (L"kernel32.dll"),
                               "LoadLibraryW"));
    if (l == nullptr)
    {
      VirtualFreeEx (h, r, 0, MEM_RELEASE);
      return load_outcome::failed;
    }

    auto_handle t (
      CreateRemoteThread (h,
                          nullptr,
                          0,
                          reinterpret_cast<LPTHREAD_START_ROUTINE> (
                            reinterpret_cast<void (*) ()> (l)),
                          r,
                          0,
                          nullptr));

    if (t.handle == nullptr)
    {
      DWORD e (GetLastError ());
      diag ("unable to start the loader thread ({})", e);
      VirtualFreeEx (h, r, 0, MEM_RELEASE);
      return e == ERROR_ACCESS_DENIED
        ? load_outcome::denied
        : load_outcome::failed;
    }

    // If the loader thread is still running, then it may still be reading
    // the path, so leave the remote memory allocated.
    //
    DWORD x (0);
    if (WaitForSingleObject (t.handle, load_timeout) != WAIT_OBJECT_0 ||
        !GetExitCodeThread (t.handle, &x))
      return load_outcome::timeout;

    VirtualFreeEx (h, r, 0, MEM_RELEASE);

    // The thread exit code is the low 32 bits of the module handle. Zero
    // means that either LoadLibraryW() failed or the module was loaded at a
    // 4G boundary. Companion status resolves this later (see attach()).
    //
    return x != 0 ? load_outcome::loaded : load_outcome::unknown;
  }

  // Verify that the process is a 64-bit Steam client and load this module
  // into it. On failure, set the result.
  //
  static load_outcome
  load_steam (HANDLE h, HMODULE self, attach_result& r) noexcept
  {
    wchar_t p[path_capacity];
    DWORD   n (path_capacity);

    if (!QueryFullProcessImageNameW (h, 0, p, &n) ||
        !steam_image (wstring_view (p, n)))
    {
      r = attach_result::not_steam;
      return load_outcome::failed;
    }

    // This module is 64-bit and requires a 64-bit Steam client.
    //
    BOOL w (FALSE);
    if (!IsWow64Process (h, &w) || w)
    {
      r = attach_result::unsupported;
      return load_outcome::failed;
    }

    load_outcome o (load_module (h, self));

    switch (o)
    {
    case load_outcome::failed:  r = attach_result::injection_failed; break;
    case load_outcome::denied:  r = attach_result::access_denied;    break;
    case load_outcome::timeout: r = attach_result::timeout;          break;
    case load_outcome::loaded:
    case load_outcome::unknown:                                      break;
    }

    return o;
  }

  // Attach to the Steam process specified on the command line.
  //
  static attach_result
  attach (HMODULE self, const wchar_t* a) noexcept
  {
    process_id p;
    if (a == nullptr || !parse_attach_arguments (a, p))
      return attach_result::invalid_arguments;

    attach_result r;
    if (settled (p, r))
      return r;

    load_outcome o;
    {
      auto_handle h (OpenProcess (process_access, FALSE, value (p)));

      if (h.handle == nullptr)
        return GetLastError () == ERROR_ACCESS_DENIED
          ? attach_result::access_denied
          : attach_result::not_steam;

      o = load_steam (h.handle, self, r);
    }

    if (o != load_outcome::loaded && o != load_outcome::unknown)
      return r;

    // If the load outcome is unknown, then Companion timing out means
    // the load failed.
    //
    r = wait_companion (p, settle_timeout);

    return o == load_outcome::unknown && r == attach_result::timeout
      ? attach_result::injection_failed
      : r;
  }
}

// Attach entry point. The name is attach_entry_point plus the W suffix that
// rundll32 looks up first for the Unicode command line (see protocol.hxx).
// The process exit code is the attach result.
//
extern "C" __declspec (dllexport) void CALLBACK
IW4xAttachW (HWND, HINSTANCE, LPWSTR arguments, int)
{
  using namespace companion;

  // Get this module's handle from the address of this function. The lookup
  // always succeeds and doesn't change the reference count.
  //
  HMODULE self;
  GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR> (&IW4xAttachW),
                      &self);

  ExitProcess (static_cast<UINT> (attach (self, arguments)));
}
