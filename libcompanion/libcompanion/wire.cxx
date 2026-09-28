// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/wire.hxx>

#include <cstring> // memcpy()

#include <libcompanion/endian.hxx>
#include <libcompanion/contract.hxx>

using namespace std;

namespace companion
{
  // Decode a varint and advance the reader past it. Truncated varints and
  // those that don't fit into 64 bits yield false.
  //
  static bool
  read_varint (wire_reader& r, uint64_t& v) noexcept
  {
    const uint8_t* p (r.data.data () + r.position);
    size_t n (r.data.size () - r.position);

    // Fast path for single-byte varints, which most tags and small values
    // are.
    //
    if (n != 0 && p[0] < 0x80)
    {
      v = p[0];
      r.position++;
      return true;
    }

    if (n > max_varint_size)
      n = max_varint_size;

    uint64_t x (0);
    for (size_t i (0); i != n; ++i)
    {
      uint64_t b (p[i]);

      // Only one bit of the tenth byte fits into 64 bits.
      //
      if (i == max_varint_size - 1 && b > 1)
        return false;

      x |= (b & 0x7F) << (7 * i);

      if (b < 0x80)
      {
        v = x;
        r.position += i + 1;
        return true;
      }
    }

    return false;
  }

  // Read the 4 or 8-byte fixed value. Return false if it is truncated.
  //
  static bool
  read_fixed (wire_reader& r, size_t n, uint64_t& v) noexcept
  {
    if (r.data.size () - r.position < n)
      return false;

    const uint8_t* p (r.data.data () + r.position);
    v = n == 8 ? load64 (p) : load32 (p);
    r.position += n;
    return true;
  }

  // Mark the reader failed and return false.
  //
  static bool
  fail (wire_reader& r) noexcept
  {
    r.failed = true;
    return false;
  }

  bool
  read_field (wire_reader& r, wire_field& f) noexcept
  {
    LIBCOMPANION_PRE (r.position <= r.data.size ());

    if (r.failed || r.position == r.data.size ())
      return false;

    size_t b (r.position);

    uint64_t t;
    if (!read_varint (r, t))
      return fail (r);

    uint64_t n (t >> 3);
    if (n == 0 || n > max_field_number)
      return fail (r);

    f.number  = static_cast<uint32_t> (n);
    f.type    = static_cast<wire_type> (t & 7);
    f.value   = 0;
    f.payload = {};

    switch (f.type)
    {
    case wire_type::varint:
    {
      if (!read_varint (r, f.value))
        return fail (r);

      break;
    }
    case wire_type::fixed64:
    case wire_type::fixed32:
    {
      size_t w (f.type == wire_type::fixed64 ? 8 : 4);

      if (!read_fixed (r, w, f.value))
        return fail (r);

      break;
    }
    case wire_type::length:
    {
      uint64_t l;
      if (!read_varint (r, l) || l > r.data.size () - r.position)
        return fail (r);

      f.payload = r.data.subspan (r.position, static_cast<size_t> (l));
      r.position += static_cast<size_t> (l);
      break;
    }
    default:
      return fail (r);
    }

    f.raw = r.data.subspan (b, r.position - b);

    LIBCOMPANION_ASSERT (r.position <= r.data.size ());
    return true;
  }

  // Append n bytes to the output and return a pointer to them. If they
  // don't fit, then mark the writer overflowed and return NULL.
  //
  static uint8_t*
  reserve (wire_writer& w, size_t n) noexcept
  {
    if (w.overflow || w.buffer.size () - w.size < n)
    {
      w.overflow = true;
      return nullptr;
    }

    uint8_t* p (w.buffer.data () + w.size);
    w.size += n;
    return p;
  }

  void
  write_varint (wire_writer& w, uint64_t v) noexcept
  {
    uint8_t* p (reserve (w, varint_size (v)));
    if (p == nullptr)
      return;

    for (; v >= 0x80; v >>= 7)
      *p++ = static_cast<uint8_t> (v | 0x80);

    *p = static_cast<uint8_t> (v);
  }

  void
  write_tag (wire_writer& w, uint32_t n, wire_type t) noexcept
  {
    LIBCOMPANION_PRE (n != 0 && n <= max_field_number);

    write_varint (w,
                  static_cast<uint64_t> (n) << 3 | static_cast<uint8_t> (t));
  }

  void
  write_fixed64 (wire_writer& w, uint32_t n, uint64_t v) noexcept
  {
    write_tag (w, n, wire_type::fixed64);

    if (uint8_t* p = reserve (w, 8))
      store64 (p, v);
  }

  void
  write_length_header (wire_writer& w, uint32_t n, size_t s) noexcept
  {
    write_tag (w, n, wire_type::length);
    write_varint (w, s);
  }

  void
  write_length (wire_writer& w, uint32_t n, bytes c) noexcept
  {
    write_length_header (w, n, c.size ());
    write_raw (w, c);
  }

  void
  write_raw (wire_writer& w, bytes c) noexcept
  {
    // An empty span can have a NULL data pointer and passing it to memcpy()
    // is undefined even for the zero size.
    //
    if (c.empty ())
      return;

    if (uint8_t* p = reserve (w, c.size ()))
      memcpy (p, c.data (), c.size ());
  }
}
