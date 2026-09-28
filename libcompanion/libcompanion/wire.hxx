// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <bit>     // bit_width()
#include <cstdint>
#include <cstddef> // size_t

#include <libcompanion/types.hxx>

namespace companion
{
  // Protocol buffers wire format reading and writing.
  //
  // The GamesPlayed rewrite only changes two fields of a single message. So
  // we operate on the wire format directly, which requires neither a
  // protobuf runtime nor a copy of Valve's evolving .proto files. The fields
  // we don't change are copied byte for byte, including fields that only
  // newer Steam versions know about.
  //
  // The input is untrusted: every read is bounds-checked and the reader
  // fails on any malformed input. The writer doesn't allocate: it writes to
  // a buffer provided by the caller and fails when the buffer is full.
  //
  enum class wire_type: std::uint8_t
  {
    varint      = 0,
    fixed64     = 1,
    length      = 2, // Length-delimited.
    start_group = 3,
    end_group   = 4,
    fixed32     = 5
  };

  // Maximum field number (29 bits).
  //
  inline constexpr std::uint32_t max_field_number ((1u << 29) - 1);

  // Maximum varint size (64 bits in 7-bit groups).
  //
  inline constexpr std::size_t max_varint_size (10);

  // Field as read from the input.
  //
  // For a varint or fixed field the value member contains the value and the
  // payload is empty. For a length-delimited field the payload contains the
  // content. The raw member always contains the complete field encoding,
  // including the tag, so that the field can be copied unchanged.
  //
  struct wire_field
  {
    bytes         raw;
    bytes         payload;
    std::uint64_t value;
    std::uint32_t number;
    wire_type     type;
  };

  // Input position in the encoded message.
  //
  // After a failed read the reader remains failed. Note that read_field()
  // returns false both at the end of input and on error, so check failed to
  // distinguish the two.
  //
  struct wire_reader
  {
    bytes       data;
    std::size_t position = 0;
    bool        failed = false;
  };

  // Advance the reader past one field and describe it in f. If there are no
  // more fields or the input is malformed, then return false. Groups count
  // as malformed since they are deprecated and Steam never sends them.
  //
  bool
  read_field (wire_reader&, wire_field&) noexcept;

  // Output position in the caller-provided buffer.
  //
  // After a write that doesn't fit, the writer remains overflowed and
  // ignores further writes. As a result, a sequence of writes can be checked
  // once at the end.
  //
  struct wire_writer
  {
    mutable_bytes buffer;
    std::size_t   size = 0;
    bool          overflow = false;
  };

  // Return the encoded varint size.
  //
  constexpr std::size_t
  varint_size (std::uint64_t) noexcept;

  // Return the encoded tag size (it doesn't depend on the wire type).
  //
  constexpr std::size_t
  tag_size (std::uint32_t number) noexcept;

  // Write the value, the tag, or the fixed64 field.
  //
  void
  write_varint (wire_writer&, std::uint64_t) noexcept;

  void
  write_tag (wire_writer&, std::uint32_t number, wire_type) noexcept;

  void
  write_fixed64 (wire_writer&, std::uint32_t number, std::uint64_t) noexcept;

  // Encode the bytes as a complete length-delimited field.
  //
  void
  write_length (wire_writer&, std::uint32_t number, bytes) noexcept;

  // Write the tag and the length of a length-delimited field. The caller
  // then writes exactly size bytes of content.
  //
  void
  write_length_header (wire_writer&,
                       std::uint32_t number,
                       std::size_t size) noexcept;

  // Write the pre-encoded bytes (normally wire_field::raw).
  //
  void
  write_raw (wire_writer&, bytes) noexcept;
}

#include <libcompanion/wire.ixx>
