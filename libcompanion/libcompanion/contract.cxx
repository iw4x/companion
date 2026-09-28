// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#include <libcompanion/contract.hxx>

#include <atomic>
#include <cstdio>  // fprintf(), fflush()
#include <cstdlib> // abort()

using namespace std;

namespace companion
{
  const char*
  contract_kind_name (contract_kind k) noexcept
  {
    switch (k)
    {
    case contract_kind::precondition:  return "precondition";
    case contract_kind::postcondition: return "postcondition";
    case contract_kind::invariant:     return "invariant";
    case contract_kind::assertion:     return "assertion";
    case contract_kind::unreachable:   return "unreachable";
    }

    return "contract";
  }

  namespace
  {
    atomic<contract_handler> current_handler {nullptr};

    // Return the path leaf recognizing both / and \ as separators or
    // <unknown> if the path is NULL.
    //
    const char*
    file_name (const char* p) noexcept
    {
      if (p == nullptr)
        return "<unknown>";

      const char* r (p);

      for (const char* i (p); *i != '\0'; ++i)
      {
        if (*i == '/' || *i == '\\')
          r = i + 1;
      }

      return r;
    }

    // Print the violation diagnostics to stderr.
    //
    void
    report (const contract_violation& v) noexcept
    {
      const char* f  (file_name (v.file));
      const char* fn (v.function   != nullptr ? v.function   : "<unknown>");
      const char* e  (v.expression != nullptr ? v.expression : "<unknown>");

      switch (v.kind)
      {
      case contract_kind::precondition:
      case contract_kind::postcondition:
      case contract_kind::invariant:
        fprintf (stderr,
                 "%s:%u: error: %s violated: %s\n",
                 f, v.line, contract_kind_name (v.kind), e);
        break;

      case contract_kind::assertion:
        fprintf (stderr,
                 "%s:%u: error: assertion failed: %s\n",
                 f, v.line, e);
        break;

      case contract_kind::unreachable:
        fprintf (stderr,
                 "%s:%u: error: control reached unreachable code\n",
                 f, v.line);
        break;
      }

      fprintf (stderr, "%s:%u: note: in '%s'\n", f, v.line, fn);

      if (v.message != nullptr)
        fprintf (stderr, "%s:%u: note: %s\n", f, v.line, v.message);

      fflush (stderr);
    }
  }

  contract_handler
  set_contract_handler (contract_handler h) noexcept
  {
    // The handler pointer is the only state published through this atomic,
    // so relaxed ordering is sufficient.
    //
    return current_handler.exchange (h, memory_order_relaxed);
  }

  [[noreturn]] void
  contract_fail (contract_kind k,
                 const char* file,
                 unsigned line,
                 const char* function,
                 const char* expression,
                 const char* message) noexcept (false)
  {
    const contract_violation v {file, function, expression, message, line, k};

    // The handler may throw (hence noexcept(false)). If it returns, then
    // report and abort as if there were no handler.
    //
    if (contract_handler h = current_handler.load (memory_order_relaxed))
      h (v);

    report (v), abort ();
  }
}
