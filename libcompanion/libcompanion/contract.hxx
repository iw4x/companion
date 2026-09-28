// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <concepts>
#include <exception> // uncaught_exceptions()
#include <utility>   // move()

#include <libcompanion/export.hxx>

// Contract checking.
//
// A precondition is a requirement on the caller. An assertion or an
// unreachability mark is a fact established by the implementation. A
// postcondition is a property checked on scope exit and an invariant is a
// property checked on both scope entry and exit.
//
// LIBCOMPANION_CONTRACT selects one of the following checking levels:
//
//   0  off      preconditions are not evaluated and assertions become
//               optimizer assumptions
//   1  default  preconditions, assertions, and unreachability are checked
//   2  audit    postconditions and invariants are also checked
//
// Postconditions and invariants are audit-level since they add a check to
// every exit from the scope.
//
// Contract expressions must be free of side effects since whether and how
// they are evaluated depends on the checking level.
//
#define LIBCOMPANION_CONTRACT_OFF     0
#define LIBCOMPANION_CONTRACT_DEFAULT 1
#define LIBCOMPANION_CONTRACT_AUDIT   2

#ifndef LIBCOMPANION_CONTRACT
#  define LIBCOMPANION_CONTRACT LIBCOMPANION_CONTRACT_DEFAULT
#endif

namespace companion
{
  enum class contract_kind: unsigned char
  {
    precondition,
    postcondition,
    invariant,
    assertion,
    unreachable
  };

  // Return the contract kind name (for example, precondition).
  //
  LIBCOMPANION_SYMEXPORT const char*
  contract_kind_name (contract_kind) noexcept;

  // Description of a violated contract that is passed to the handler. The
  // expression is the stringized source text and the message is NULL if
  // unspecified.
  //
  struct contract_violation
  {
    const char*   file;
    const char*   function;
    const char*   expression;
    const char*   message;
    unsigned      line;
    contract_kind kind;
  };

  // Handle a contract violation.
  //
  // First call the installed handler, if any. The handler may throw in
  // which case the exception propagates to the caller (this is how tests
  // verify contracts). If there is no handler or it returns, then print the
  // diagnostics to stderr and abort the process.
  //
  [[noreturn]] LIBCOMPANION_SYMEXPORT void
  contract_fail (contract_kind,
                 const char* file,
                 unsigned line,
                 const char* function,
                 const char* expression,
                 const char* message = nullptr) noexcept (false);

  using contract_handler = void (*) (const contract_violation&);

  // Set the process-wide contract handler and return the previous one. A
  // NULL handler restores the default behavior.
  //
  LIBCOMPANION_SYMEXPORT contract_handler
  set_contract_handler (contract_handler) noexcept;

  template <typename F>
  concept contract_predicate = requires (F& f)
  {
    { f () } -> std::convertible_to<bool>;
  };

  // Postcondition guard that evaluates the predicate on normal scope exit.
  //
  // The predicate is normally a lambda that captures the enclosing scope by
  // reference (see LIBCOMPANION_POST() below). Locals declared before the
  // guard are destroyed after it, so the predicate sees their final values.
  //
  template <contract_predicate F>
  class postcondition_guard
  {
  public:
    postcondition_guard (F p,
                         const char* f,
                         unsigned l,
                         const char* fn,
                         const char* e)
      : predicate_  (std::move (p)),
        file_       (f),
        function_   (fn),
        expression_ (e),
        line_       (l),
        uncaught_   (std::uncaught_exceptions ())
    {
      // The postcondition is evaluated by the destructor. The exception
      // count recorded above lets it detect stack unwinding.
      //
    }

    ~postcondition_guard () noexcept (false)
    {
      // Skip the check if we are unwinding since the operation has not
      // completed.
      //
      if (std::uncaught_exceptions () != uncaught_)
        return;

      if (!static_cast<bool> (predicate_ ()))
        contract_fail (contract_kind::postcondition,
                       file_,
                       line_,
                       function_,
                       expression_);
    }

    // Non-copyable since a copy would evaluate the postcondition a second
    // time.
    //
    postcondition_guard (const postcondition_guard&) = delete;
    postcondition_guard& operator= (const postcondition_guard&) = delete;

  private:
    F           predicate_;
    const char* file_;
    const char* function_;
    const char* expression_;
    unsigned    line_;
    int         uncaught_;
  };

  // Invariant guard that evaluates the predicate on construction and on
  // normal scope exit.
  //
  template <contract_predicate F>
  class invariant_guard
  {
  public:
    invariant_guard (F p,
                     const char* f,
                     unsigned l,
                     const char* fn,
                     const char* e)
      : predicate_  (std::move (p)),
        file_       (f),
        function_   (fn),
        expression_ (e),
        line_       (l),
        uncaught_   (std::uncaught_exceptions ())
    {
      // Note that if this check fails, then the guard is never constructed
      // and its destructor (with the exit check) does not run.
      //
      if (!static_cast<bool> (predicate_ ()))
        contract_fail (contract_kind::invariant,
                       file_,
                       line_,
                       function_,
                       expression_,
                       "on entry");
    }

    ~invariant_guard () noexcept (false)
    {
      // Skip the check if we are unwinding since the object may be left in
      // an intermediate state that the operation was going to repair.
      //
      if (std::uncaught_exceptions () != uncaught_)
        return;

      if (!static_cast<bool> (predicate_ ()))
        contract_fail (contract_kind::invariant,
                       file_,
                       line_,
                       function_,
                       expression_,
                       "on exit");
    }

    // Non-copyable since a copy would evaluate the invariant a second time.
    //
    invariant_guard (const invariant_guard&) = delete;
    invariant_guard& operator= (const invariant_guard&) = delete;

  private:
    F           predicate_;
    const char* file_;
    const char* function_;
    const char* expression_;
    unsigned    line_;
    int         uncaught_;
  };
}

// Optimizer assumption for an unchecked assertion.
//
// Note that with some compilers the expression is not evaluated at runtime.
//
#if defined(__clang__)

#  define LIBCOMPANION_CONTRACT_ASSUME_(e) __builtin_assume (e)

#elif defined(_MSC_VER)

#  define LIBCOMPANION_CONTRACT_ASSUME_(e) __assume (e)

#elif defined(__GNUC__)

     // Not all the supported GCC versions have __builtin_assume() so mark
     // the false branch unreachable.
     //
#  define LIBCOMPANION_CONTRACT_ASSUME_(e)                                          \
     do                                                                        \
     {                                                                         \
       if (!(e))                                                               \
         __builtin_unreachable ();                                             \
     }                                                                         \
     while (false)

#else

     // No assumption primitive. Only type-check the expression.
     //
#  define LIBCOMPANION_CONTRACT_ASSUME_(e) ((void) sizeof (e))

#endif

// Optimizer hint for an unreachable point in an unchecked build.
//
#if defined(__clang__) || defined(__GNUC__)

#  define LIBCOMPANION_CONTRACT_UNREACHABLE_() __builtin_unreachable ()

#elif defined(_MSC_VER)

#  define LIBCOMPANION_CONTRACT_UNREACHABLE_() __assume (0)

#else

#  define LIBCOMPANION_CONTRACT_UNREACHABLE_() ((void) 0)

#endif

// Token pasting with macro expansion of the arguments (for __LINE__).
//
#define LIBCOMPANION_CONTRACT_CAT_(x, y) x##y
#define LIBCOMPANION_CONTRACT_CAT(x, y)  LIBCOMPANION_CONTRACT_CAT_ (x, y)

// Check the contract of the specified kind and call contract_fail() with the
// stringized expression if it doesn't hold. This is a void expression and
// can be used wherever one is allowed.
//
#define LIBCOMPANION_CONTRACT_CHECK_(k, e, m)                                       \
  (static_cast<bool> (e)                                                       \
   ? void ()                                                                   \
   : ::companion::contract_fail ((k),                                               \
                            __FILE__,                                          \
                            __LINE__,                                          \
                            __func__,                                          \
                            #e,                                                \
                            (m)))

#if LIBCOMPANION_CONTRACT >= LIBCOMPANION_CONTRACT_DEFAULT

#  define LIBCOMPANION_PRE(e)                                                       \
     LIBCOMPANION_CONTRACT_CHECK_ (::companion::contract_kind::precondition,             \
                              e,                                               \
                              nullptr)

#  define LIBCOMPANION_PRE_MSG(e, m)                                                \
     LIBCOMPANION_CONTRACT_CHECK_ (::companion::contract_kind::precondition,             \
                              e,                                               \
                              m)

#  define LIBCOMPANION_ASSERT(e)                                                    \
     LIBCOMPANION_CONTRACT_CHECK_ (::companion::contract_kind::assertion,                \
                              e,                                               \
                              nullptr)

#  define LIBCOMPANION_ASSERT_MSG(e, m)                                             \
     LIBCOMPANION_CONTRACT_CHECK_ (::companion::contract_kind::assertion,                \
                              e,                                               \
                              m)

     // There is no expression to stringize so pass a fixed description.
     //
#  define LIBCOMPANION_UNREACHABLE()                                                \
     ::companion::contract_fail (::companion::contract_kind::unreachable,                \
                            __FILE__,                                          \
                            __LINE__,                                          \
                            __func__,                                          \
                            "control reached this point")

#else

// Preconditions are not evaluated. They guard against caller errors,
// including bad external input, so they must not become optimizer
// assumptions.
//
#  define LIBCOMPANION_PRE(e)        ((void) 0)
#  define LIBCOMPANION_PRE_MSG(e, m) ((void) 0)

// Assertions state facts established by the implementation, so they become
// optimizer assumptions.
//
#  define LIBCOMPANION_ASSERT(e)    \
     LIBCOMPANION_CONTRACT_ASSUME_ (static_cast<bool> (e))

#  define LIBCOMPANION_ASSERT_MSG(e, m)    \
     LIBCOMPANION_CONTRACT_ASSUME_ (static_cast<bool> (e))

#  define LIBCOMPANION_UNREACHABLE() LIBCOMPANION_CONTRACT_UNREACHABLE_ ()

#endif

#if LIBCOMPANION_CONTRACT >= LIBCOMPANION_CONTRACT_AUDIT

     // Declare a postcondition guard for the rest of the enclosing scope.
     //
     // Everything the expression references must outlive the guard, which
     // is the case for locals declared before LIBCOMPANION_POST(). The guard
     // name is derived from __LINE__ so there can be at most one such
     // declaration per source line.
     //
#  define LIBCOMPANION_POST(e)                                                      \
     ::companion::postcondition_guard                                               \
       LIBCOMPANION_CONTRACT_CAT (libcompanion_postcondition_, __LINE__) (               \
         [&] () -> bool {return static_cast<bool> (e);},                       \
         __FILE__,                                                             \
         __LINE__,                                                             \
         __func__,                                                             \
         #e)

     // Declare an invariant guard for the rest of the enclosing scope. The
     // same lifetime and one-per-line restrictions apply.
     //
#  define LIBCOMPANION_INVARIANT(e)                                                 \
     ::companion::invariant_guard                                                   \
       LIBCOMPANION_CONTRACT_CAT (libcompanion_invariant_, __LINE__) (                   \
         [&] () -> bool {return static_cast<bool> (e);},                       \
         __FILE__,                                                             \
         __LINE__,                                                             \
         __func__,                                                             \
         #e)

#else

#  define LIBCOMPANION_POST(e)      ((void) 0)
#  define LIBCOMPANION_INVARIANT(e) ((void) 0)

#endif
