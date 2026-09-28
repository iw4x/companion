// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/registration.hxx>

#include <concepts>
#include <cstring>     // memcpy(), memchr()
#include <type_traits> // is_trivially_copyable_v

#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  // UTF-8 multi-byte sequence form: the lead byte value under the mask, the
  // sequence length, and the minimum code point (a smaller one is an
  // overlong encoding).
  //
  struct utf8_form
  {
    uint8_t  mask;
    uint8_t  lead;
    uint8_t  length;
    uint32_t minimum;
  };

  static constexpr utf8_form utf8_forms[] = {
    {0xE0, 0xC0, 2, 0x80},
    {0xF0, 0xE0, 3, 0x800},
    {0xF8, 0xF0, 4, 0x10000}
  };

  // Decode the multi-byte sequence at the start of s into c and return its
  // length. Return 0 if the sequence is invalid.
  //
  static size_t
  decode_utf8 (string_view s, uint32_t& c) noexcept
  {
    uint8_t b (static_cast<uint8_t> (s[0]));

    for (const utf8_form& f: utf8_forms)
    {
      if ((b & f.mask) != f.lead)
        continue;

      if (s.size () < f.length)
        return 0;

      c = b & static_cast<uint8_t> (~f.mask);

      for (size_t i (1); i != f.length; ++i)
      {
        uint8_t x (static_cast<uint8_t> (s[i]));

        if ((x & 0xC0) != 0x80)
          return 0;

        c = c << 6 | (x & 0x3F);
      }

      return c >= f.minimum ? f.length : 0;
    }

    return 0;
  }

  bool
  valid_extra_info (string_view s) noexcept
  {
    if (s.empty () || s.size () >= extra_info_capacity)
      return false;

    for (size_t i (0); i != s.size (); )
    {
      uint8_t b (static_cast<uint8_t> (s[i]));

      if (b < 0x80)
      {
        // C0 controls and DEL.
        //
        if (b < 0x20 || b == 0x7F)
          return false;

        ++i;
        continue;
      }

      uint32_t c;
      size_t n (decode_utf8 (s.substr (i), c));

      // Surrogates, code points past U+10FFFF, and C1 controls.
      //
      if (n == 0                      ||
          (c >= 0xD800 && c <= 0xDFFF) ||
          c > 0x10FFFF                 ||
          (c >= 0x80 && c <= 0x9F))
        return false;

      i += n;
    }

    return true;
  }

  bool
  valid (const registration& r) noexcept
  {
    return r.extra_info_size < extra_info_capacity &&
           valid (r.app)                           &&
           valid_extra_info (extra_info (r));
  }

  // Requirements shared by record and unix_record, which lets the decoding
  // steps below be written once for both layouts.
  //
  template <typename R>
  concept record_layout =
    is_trivially_copyable_v<R> &&
    requires (const R& x)
    {
      { x.magic }      -> same_as<const uint32_t&>;
      { x.version }    -> same_as<const uint32_t&>;
      { x.size }       -> same_as<const uint32_t&>;
      { x.process_id } -> same_as<const uint32_t&>;
      { x.app_id }     -> same_as<const uint32_t&>;
      { x.extra_info } -> same_as<const char (&)[extra_info_capacity]>;
    };

  static_assert (record_layout<record>);
  static_assert (record_layout<unix_record>);

  // Check the common fields up to the process id. For a known version the
  // size must equal the layout size.
  //
  template <record_layout R>
  static registration_outcome
  decode_header (const R& x,
                 uint32_t magic,
                 uint32_t version,
                 process_id p) noexcept
  {
    if (x.magic != magic)
      return registration_outcome::magic;

    if (x.version != version)
      return registration_outcome::version;

    if (x.size != sizeof (R))
      return registration_outcome::size;

    if (x.process_id != value (p))
      return registration_outcome::process;

    return registration_outcome::valid;
  }

  // Check the common fields after the process id and, if they are valid,
  // store them in the registration.
  //
  template <record_layout R>
  static registration_outcome
  decode_payload (const R& x, registration& r) noexcept
  {
    app_id a (static_cast<app_id> (x.app_id));

    if (!valid (a))
      return registration_outcome::app;

    const void* e (memchr (x.extra_info, '\0', sizeof (x.extra_info)));

    if (e == nullptr)
      return registration_outcome::extra_info;

    size_t n (static_cast<size_t> (static_cast<const char*> (e) -
                                   x.extra_info));

    if (!valid_extra_info (string_view (x.extra_info, n)))
      return registration_outcome::extra_info;

    // Implied by valid_extra_info() and asserted here since it keeps the
    // copy below within the registration.
    //
    LIBCOMPANION_ASSERT (n < extra_info_capacity);

    r.app = a;
    r.extra_info_size = static_cast<uint8_t> (n);
    memcpy (r.extra_info, x.extra_info, n);

    return registration_outcome::valid;
  }

  registration_outcome
  decode_record (bytes b, process_id p, registration& r) noexcept
  {
    registration_outcome o (registration_outcome::truncated);
    LIBCOMPANION_POST (o != registration_outcome::valid || valid (r));

    if (b.size () < sizeof (record))
      return o;

    record x;
    memcpy (&x, b.data (), sizeof (x));

    o = decode_header (x, record_magic, record_version, p);

    if (o == registration_outcome::valid)
      o = decode_payload (x, r);

    return o;
  }

  registration_outcome
  decode_unix_record (bytes b,
                      process_id p,
                      start_time t,
                      registration& r) noexcept
  {
    registration_outcome o (registration_outcome::truncated);
    LIBCOMPANION_POST (o != registration_outcome::valid || valid (r));

    // A longer file is not a record either but we report both as truncated
    // since a partially written file is the likely cause.
    //
    if (b.size () != sizeof (unix_record))
      return o;

    unix_record x;
    memcpy (&x, b.data (), sizeof (x));

    o = decode_header (x, unix_record_magic, unix_record_version, p);

    if (o != registration_outcome::valid)
      return o;

    // The start time distinguishes the process that wrote the record from a
    // later one that reuses its pid (for example, after the game crashed).
    //
    uint64_t s (static_cast<uint64_t> (x.start_time_high) << 32 |
                x.start_time_low);

    if (s != value (t))
      return o = registration_outcome::start_time;

    return o = decode_payload (x, r);
  }
}
