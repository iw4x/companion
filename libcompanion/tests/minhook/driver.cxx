// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <string>
#include <iostream>
#include <stdexcept> // runtime_error

#include <libcompanion/minhook/minhook.h>

#undef NDEBUG
#include <cassert>

// Usage: argv[0] <scenario>
//
// Run the scenario against the MinHook API and print the status of each
// call and the result of each call to a target function, one per line:
//
// <step> <status>|<result>
//
// The scenarios are:
//
// initialize       initialize and uninitialize twice
// not-initialized  call each function before initializing
// hook             hook a function, call it enabled and disabled
// errors           the errors of the hook state changes
// all              enable and disable all the hooks at once
// queue            queue changes and apply them at once
// api              hook an exported function by name
// threads          hook a function that another thread is executing
// uninitialize     uninitialize with an enabled hook
// strings          the status names
//
using namespace std;

// Target functions. The volatile arithmetic keeps each of them long enough
// to hook.
//
[[gnu::noinline]] static int
target_a (int x)
{
  volatile int r (x * 3);
  return r + 1;
}

[[gnu::noinline]] static int
target_b (int x)
{
  volatile int r (x ^ 0x55);
  return r - 7;
}

static atomic<bool> stop;

[[gnu::noinline]] static int
target_spin (int x)
{
  volatile int r (x);
  while (!stop.load (memory_order_relaxed))
    r = r + 1;
  return r;
}

// Detours. Each calls the original function through the trampoline.
//
static int (*original_a) (int);
static int (*original_b) (int);

static int
detour_a (int x)
{
  return original_a (x) + 1000;
}

static int
detour_b (int x)
{
  return original_b (x) + 2000;
}

static int
detour_spin (int x)
{
  return x;
}

static int data_object;

static void
print (const char* step, mh_status s)
{
  cout << step << ' ' << mh_status_to_string (s) << '\n';
}

static void
print (const char* step, int r)
{
  cout << step << ' ' << r << '\n';
}

template <typename F>
concept target_function = is_same_v<F, int (int)>;

template <target_function F>
static mh_status
create (F* target, F* detour, F** original)
{
  return mh_create_hook (reinterpret_cast<void*> (target),
                         reinterpret_cast<void*> (detour),
                         reinterpret_cast<void**> (original));
}

template <target_function F>
static void*
address (F* f)
{
  return reinterpret_cast<void*> (f);
}

static void
initialize ()
{
  print ("initialize", mh_initialize ());
  print ("initialize-again", mh_initialize ());
  print ("uninitialize", mh_uninitialize ());
  print ("uninitialize-again", mh_uninitialize ());
}

static void
not_initialized ()
{
  print ("create", create (&target_a, &detour_a, &original_a));
  print ("remove", mh_remove_hook (address (&target_a)));
  print ("enable", mh_enable_hook (address (&target_a)));
  print ("disable", mh_disable_hook (address (&target_a)));
  print ("queue-enable", mh_queue_enable_hook (address (&target_a)));
  print ("queue-disable", mh_queue_disable_hook (address (&target_a)));
  print ("apply", mh_apply_queued ());
}

static void
hook ()
{
  print ("initialize", mh_initialize ());
  print ("create", create (&target_a, &detour_a, &original_a));
  print ("call-disabled", target_a (5));
  print ("enable", mh_enable_hook (address (&target_a)));
  print ("call-enabled", target_a (5));
  print ("call-original", original_a (5));
  print ("disable", mh_disable_hook (address (&target_a)));
  print ("call-disabled", target_a (5));
  print ("remove", mh_remove_hook (address (&target_a)));
  print ("uninitialize", mh_uninitialize ());
}

static void
errors ()
{
  print ("initialize", mh_initialize ());
  print ("create-data",
         mh_create_hook (&data_object, address (&detour_a), nullptr));
  print ("enable-uncreated", mh_enable_hook (address (&target_a)));
  print ("remove-uncreated", mh_remove_hook (address (&target_a)));
  print ("queue-uncreated", mh_queue_enable_hook (address (&target_a)));
  print ("create", create (&target_a, &detour_a, &original_a));
  print ("create-again", create (&target_a, &detour_a, &original_a));
  print ("disable-disabled", mh_disable_hook (address (&target_a)));
  print ("enable", mh_enable_hook (address (&target_a)));
  print ("enable-enabled", mh_enable_hook (address (&target_a)));
  print ("remove-enabled", mh_remove_hook (address (&target_a)));
  print ("call-removed", target_a (5));
  print ("uninitialize", mh_uninitialize ());
}

static void
all ()
{
  print ("initialize", mh_initialize ());
  print ("create-a", create (&target_a, &detour_a, &original_a));
  print ("create-b", create (&target_b, &detour_b, &original_b));
  print ("enable-all", mh_enable_hook (MH_ALL_HOOKS));
  print ("call-a", target_a (7));
  print ("call-b", target_b (7));
  print ("disable-all", mh_disable_hook (MH_ALL_HOOKS));
  print ("call-a", target_a (7));
  print ("call-b", target_b (7));
  print ("uninitialize", mh_uninitialize ());
}

static void
queue ()
{
  print ("initialize", mh_initialize ());
  print ("create-a", create (&target_a, &detour_a, &original_a));
  print ("create-b", create (&target_b, &detour_b, &original_b));
  print ("queue-enable-all", mh_queue_enable_hook (MH_ALL_HOOKS));
  print ("queue-disable-a", mh_queue_disable_hook (address (&target_a)));
  print ("call-a-queued", target_a (7));
  print ("call-b-queued", target_b (7));
  print ("apply", mh_apply_queued ());
  print ("call-a", target_a (7));
  print ("call-b", target_b (7));
  print ("uninitialize", mh_uninitialize ());
}

static DWORD (WINAPI* original_pid) ();

static DWORD WINAPI
detour_pid ()
{
  return 42;
}

static void
api ()
{
  print ("initialize", mh_initialize ());

  print ("module-not-found",
         mh_create_hook_api (L"nonexistent.dll",
                             "GetCurrentProcessId",
                             reinterpret_cast<void*> (&detour_pid),
                             nullptr));

  print ("function-not-found",
         mh_create_hook_api (L"kernel32.dll",
                             "NonexistentFunction",
                             reinterpret_cast<void*> (&detour_pid),
                             nullptr));

  void* t (nullptr);
  print ("create",
         mh_create_hook_api_ex (L"kernel32.dll",
                                "GetCurrentProcessId",
                                reinterpret_cast<void*> (&detour_pid),
                                reinterpret_cast<void**> (&original_pid),
                                &t));

  // Note: FARPROC is converted through the generic function pointer type.
  //
  auto f (reinterpret_cast<DWORD (WINAPI*) ()> (
            reinterpret_cast<void (*) ()> (
              GetProcAddress (GetModuleHandleW (L"kernel32.dll"),
                              "GetCurrentProcessId"))));

  // Get the process id before hooking since our call to the function goes
  // through the hook as well.
  //
  DWORD p (GetCurrentProcessId ());

  print ("target", reinterpret_cast<void*> (f) == t ? 1 : 0);
  print ("enable", mh_enable_hook (t));
  print ("call-enabled", static_cast<int> (f ()));
  print ("call-original", original_pid () == p ? 1 : 0);
  print ("remove", mh_remove_hook (t));
  print ("uninitialize", mh_uninitialize ());
}

static DWORD WINAPI
spin (void*)
{
  return static_cast<DWORD> (target_spin (0));
}

static void
threads ()
{
  print ("initialize", mh_initialize ());
  int (*o) (int);
  print ("create", create (&target_spin, &detour_spin, &o));

  stop = false;
  HANDLE h (CreateThread (nullptr, 0, &spin, nullptr, 0, nullptr));
  assert (h != nullptr);
  Sleep (50);

  // Each change suspends the thread and moves it out of the code that it
  // patches.
  //
  bool r (true);
  for (size_t i (0); i != 100; ++i)
  {
    r = r && mh_enable_hook (address (&target_spin)) == MH_OK;
    r = r && mh_disable_hook (address (&target_spin)) == MH_OK;
  }

  stop = true;
  WaitForSingleObject (h, INFINITE);
  CloseHandle (h);

  print ("toggle", r ? 1 : 0);
  print ("uninitialize", mh_uninitialize ());
}

static void
uninitialize ()
{
  print ("initialize", mh_initialize ());
  print ("create", create (&target_a, &detour_a, &original_a));
  print ("enable", mh_enable_hook (address (&target_a)));
  print ("uninitialize", mh_uninitialize ());
  print ("call", target_a (5));
}

static void
strings ()
{
  for (int s (MH_UNKNOWN); s <= MH_ERROR_FUNCTION_NOT_FOUND; ++s)
    cout << mh_status_to_string (static_cast<mh_status> (s)) << '\n';

  cout << mh_status_to_string (
            static_cast<mh_status> (MH_ERROR_FUNCTION_NOT_FOUND + 1))
       << '\n';
}

int
main (int argc, char* argv[])
try
{
  if (argc != 2)
    throw runtime_error ("invalid arguments");

  string s (argv[1]);

  if      (s == "initialize")      initialize ();
  else if (s == "not-initialized") not_initialized ();
  else if (s == "hook")            hook ();
  else if (s == "errors")          errors ();
  else if (s == "all")             all ();
  else if (s == "queue")           queue ();
  else if (s == "api")             api ();
  else if (s == "threads")         threads ();
  else if (s == "uninitialize")    uninitialize ();
  else if (s == "strings")         strings ();
  else
    throw runtime_error ("invalid scenario '" + s + '\'');

  return 0;
}
catch (const exception& e)
{
  cerr << "error: " << e.what () << endl;
  return 2;
}
