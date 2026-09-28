// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/utility-win32.hxx>

#include <atomic>
#include <cstdint>
#include <cstring> // memcpy()

#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  void
  write_diag (const diag_line& l) noexcept
  {
    // Note that format_diag() NUL-terminates the line.
    //
    OutputDebugStringA (l.data);
  }

  auto_handle::
  ~auto_handle () noexcept
  {
    if (handle != nullptr)
      CloseHandle (handle);
  }

  HANDLE
  release (auto_handle& h) noexcept
  {
    HANDLE r (h.handle);
    h.handle = nullptr;
    return r;
  }

  bool
  steam_image (wstring_view p) noexcept
  {
    size_t i (p.find_last_of (L"\\/"));

    if (i != wstring_view::npos)
      p.remove_prefix (i + 1);

    return CompareStringOrdinal (p.data (),
                                 static_cast<int> (p.size ()),
                                 steam_executable,
                                 -1,
                                 TRUE) == CSTR_EQUAL;
  }

  bool
  snapshot_mapping (const wchar_t* n, size_t o, mutable_bytes b) noexcept
  {
    LIBCOMPANION_PRE (o % alignof (uint32_t) == 0 &&
                      o + sizeof (uint32_t) <= b.size ());

    void* v;
    {
      auto_handle m (OpenFileMappingW (FILE_MAP_READ, FALSE, n));

      if (m.handle == nullptr)
        return false;

      // Mapping a view of the buffer size fails if the section is smaller,
      // which is the size check we need. The view keeps the section alive
      // after the handle is closed.
      //
      v = MapViewOfFile (m.handle, FILE_MAP_READ, 0, 0, b.size ());
    }

    if (v == nullptr)
      return false;

    // The view is page-aligned and the precondition guarantees that the
    // field is aligned within it.
    //
    uint8_t*             p (static_cast<uint8_t*> (v));
    atomic_ref<uint32_t> f (*reinterpret_cast<uint32_t*> (p + o));
    uint32_t             r (f.load (memory_order_acquire));

    memcpy (b.data (), p, b.size ());
    memcpy (b.data () + o, &r, sizeof (r)); // Value we acquired.

    UnmapViewOfFile (v);
    return true;
  }
}
