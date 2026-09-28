// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <cstring> // memcpy()

#include <libcompanion/version.hxx>
#include <libcompanion/protocol.hxx>
#include <libcompanion/registration.hxx>

#undef NDEBUG
#include <cassert>

// Installed library smoke test. Include the public headers and call a few
// functions. The unit tests cover the functionality.
//
int
main ()
{
  using namespace std;
  using namespace companion;

  static_assert (LIBCOMPANION_VERSION != 0);

  // Valid record.
  //
  {
    record x {};
    x.magic      = record_magic;
    x.version    = record_version;
    x.size       = sizeof (record);
    x.process_id = 4242;
    x.app_id     = fallback_app_id;
    memcpy (x.extra_info, "IW4x", 4);

    registration r;
    assert (decode_record (bytes (reinterpret_cast<const uint8_t*> (&x),
                                  sizeof (x)),
                           process_id (4242),
                           r) == registration_outcome::valid);

    assert (value (r.app) == fallback_app_id);
    assert (extra_info (r) == "IW4x");
  }

  // Empty display name.
  //
  assert (!valid_extra_info (""));
}
