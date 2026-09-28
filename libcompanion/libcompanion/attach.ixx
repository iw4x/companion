// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

namespace companion
{
  constexpr attach_result
  to_attach_result (companion_state s) noexcept
  {
    LIBCOMPANION_PRE (s != companion_state::starting);

    // An unknown state can only come from a newer companion and is mapped
    // to failed.
    //
    switch (s)
    {
    case companion_state::active:      return attach_result::active;
    case companion_state::unsupported: return attach_result::unsupported;
    case companion_state::starting:
    case companion_state::failed:      break;
    }

    return attach_result::failed;
  }
}
