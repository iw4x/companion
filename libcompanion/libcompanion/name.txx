// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

namespace companion
{
  namespace details
  {
    template <key_character C>
    constexpr std::size_t
    key_format_size (const C* f) noexcept
    {
      // A format other than one with a single %u or %lu doesn't match what
      // this code and the game's swprintf() call expect. Return a size that
      // exceeds every capacity.
      //
      constexpr std::size_t invalid (static_cast<std::size_t> (-1));

      std::size_t n (0); // Format length.
      std::size_t s (0); // Conversion length (0 if none seen).

      for (; *f != '\0'; ++f, ++n)
      {
        if (*f != '%')
          continue;

        if (s != 0)
          return invalid;

        s = 2;

        if (*++f == 'l')
          ++f, ++n, ++s;

        if (*f != 'u')
          return invalid;

        ++n; // The loop increment counts the %, this counts the u.
      }

      if (s == 0)
        return invalid;

      // Replace the conversion with ten digits and add the NUL.
      //
      return n - s + 10 + 1;
    }

    template <key_character C, std::size_t N>
    constexpr std::array<C, N>
    format_key (const C* f, std::uint32_t v) noexcept
    {
      LIBCOMPANION_PRE (key_format_size (f) <= N);

      std::array<C, N> r {}; // Zero-initialized, so always NUL-terminated.
      std::size_t      n (0);

      for (; *f != '%'; ++f)
        r[n++] = *f;

      // Skip the conversion including the optional length modifier.
      //
      for (++f; *f != 'u'; ++f) ;
      ++f;

      // Generate the digits in reverse order and then copy them in the right
      // order.
      //
      C           d[10] {};
      std::size_t k (0);

      do
      {
        d[k++] = static_cast<C> ('0' + v % 10);
        v /= 10;
      }
      while (v != 0);

      while (k != 0)
        r[n++] = d[--k];

      for (; *f != '\0'; ++f)
        r[n++] = *f;

      // The NUL is at r[n] (the array is zero-initialized).
      //
      LIBCOMPANION_ASSERT (n < N);
      return r;
    }

    // Verify that every name fits into its capacity for any id. Note that
    // key_format_size() includes the NUL.
    //
    static_assert (key_format_size (record_name_format) <=
                   object_name_capacity);
    static_assert (key_format_size (status_name_format) <=
                   object_name_capacity);
    static_assert (key_format_size (record_event_format) <=
                   object_name_capacity);
    static_assert (key_format_size (unix_record_format) <=
                   unix_path_capacity);
    static_assert (key_format_size (unix_status_format) <=
                   unix_path_capacity);
    static_assert (key_format_size (wine_record_format) <=
                   unix_path_capacity);
    static_assert (key_format_size (wine_status_format) <=
                   unix_path_capacity);
  }
}
