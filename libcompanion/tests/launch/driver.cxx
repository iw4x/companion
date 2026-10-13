// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only WITH AdditionRef-IW4x-Exception-1.1

#include <fcntl.h>    // open()
#include <unistd.h>   // close(), symlink(), geteuid()
#include <sys/file.h> // flock()

#include <chrono>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <system_error> // system_error, errc

#include <libcompanion/types.hxx>
#include <libcompanion/launch.hxx>
#include <libcompanion/preload.hxx>
#include <libcompanion/contract.hxx>

#undef NDEBUG
#include <cassert>

// Usages:
//
// argv[0] <dir>
// argv[0] execute <steam> <preload> [<argument>...]
//
// In the first form, test the Steam client launch functions using <dir>,
// which must not exist, for the files that the tests create.
//
// In the second form, execute <steam> with the arguments and with
// LD_PRELOAD set to <preload> unless it is empty.
//
using namespace std;
using namespace companion;

namespace fs = std::filesystem;

namespace
{
  // Create the file with the content.
  //
  void
  write_file (const fs::path& p, const string& s)
  {
    ofstream f (p);
    f << s;
    assert (f);
  }

  // Throw the violation, which lets us verify the preconditions.
  //
  void
  throwing_handler (const contract_violation& v)
  {
    throw v;
  }
}

int
main (int argc, char* argv[])
try
{
  if (argc >= 4 && string (argv[1]) == "execute")
  {
    execute_steam (argv[2],
                   vector<string> (argv + 4, argv + argc),
                   argv[3]);
  }

  if (argc != 2)
  {
    cerr << "error: invalid arguments" << endl;
    return 1;
  }

  set_contract_handler (&throwing_handler);

  const fs::path d (argv[1]);
  fs::create_directory (d);

  const user_id u (static_cast<user_id> (geteuid ()));

  // Module installation.
  //
  {
    const fs::path m (d / "lib" / preload_directory);

    assert (!module_installed (m));

    fs::create_directories (m / preload_platform);
    assert (!module_installed (m));

    // A directory in place of the module.
    //
    fs::create_directory (m / preload_platform / preload_file);
    assert (!module_installed (m));
    fs::remove (m / preload_platform / preload_file);

    write_file (m / preload_platform / preload_file, "");
    assert (module_installed (m));
  }

  // Companion status.
  //
  {
    const fs::path s (d / "status");

    assert (!companion_active (s));

    write_file (s, "");
    assert (!companion_active (s));

    // The lock that Companion holds while it runs. Note that it is held
    // through a separate open file description, as in a different process.
    //
    {
      int f (open (s.c_str (), O_RDONLY | O_CLOEXEC));
      assert (f != -1 && flock (f, LOCK_EX) == 0);

      assert (companion_active (s));

      close (f);
    }

    assert (!companion_active (s));

    // A symbolic link to the locked file is not the status.
    //
    {
      const fs::path l (d / "status-link");
      fs::create_symlink (s, l);

      int f (open (s.c_str (), O_RDONLY | O_CLOEXEC));
      assert (f != -1 && flock (f, LOCK_EX) == 0);

      assert (!companion_active (l));

      close (f);
    }

    // Neither is a directory.
    //
    fs::create_directory (d / "status-directory");
    assert (!companion_active (d / "status-directory"));
  }

  // Steam processes.
  //
  {
    const fs::path p (d / "proc");

    // Missing /proc.
    //
    try
    {
      steam_running (p, u);
      assert (false);
    }
    catch (const system_error& e)
    {
      assert (e.code () == errc::no_such_file_or_directory);
    }

    fs::create_directories (p / "self");
    write_file (p / "self" / "comm", "steam\n");

    fs::create_directories (p / "100");
    write_file (p / "100" / "comm", "steamwebhelper\n");

    fs::create_directories (p / "200");
    write_file (p / "200" / "comm", "bash\n");

    assert (!steam_running (p, u));
    assert (wait_steam_exit (p,
                             u,
                             chrono::milliseconds (0),
                             chrono::milliseconds (1)));

    fs::create_directories (p / "300");
    write_file (p / "300" / "comm", "steam\n");

    assert (steam_running (p, u));
    assert (!steam_running (p, static_cast<user_id> (value (u) + 1)));
    assert (!wait_steam_exit (p,
                              u,
                              chrono::milliseconds (10),
                              chrono::milliseconds (1)));
  }

  // Steam shutdown request.
  //
  {
    // The command receives -shutdown and its output is discarded.
    //
    const fs::path c (d / "steam");
    const fs::path a (d / "steam-arguments");

    write_file (c,
                "#!/bin/sh\n"
                "echo \"$@\" >'" + a.string () + "'\n"
                "echo output\n"
                "echo diagnostics 1>&2\n");

    fs::permissions (c, fs::perms::owner_all);

    request_steam_shutdown (c.string ());

    ifstream f (a);
    string   l;
    assert (getline (f, l) && l == "-shutdown");

    // Missing command.
    //
    try
    {
      request_steam_shutdown ((d / "no-steam").string ());
      assert (false);
    }
    catch (const system_error& e)
    {
      assert (e.code () == errc::no_such_file_or_directory);
    }
  }

  // Preloadable directories.
  //
  {
    assert (preloadable ("/usr/local/lib/companion"));
    assert (!preloadable ("/home/user/Call of Duty/linux/lib/companion"));
    assert (!preloadable ("/home/user/a:b/lib/companion"));
  }

  // Module directory link.
  //
  {
    const fs::path m (d / "game directory" / "lib" / preload_directory);
    const fs::path r (d / "runtime");

    fs::create_directories (m);
    fs::create_directory (r);

    fs::path l (link_module_directory (m, r));
    assert (l == r / preload_directory);
    assert (fs::read_symlink (l) == m);

    // Replace the existing link.
    //
    const fs::path n (d / "other directory" / "lib" / preload_directory);
    fs::create_directories (n);

    l = link_module_directory (n, r);
    assert (fs::read_symlink (l) == n);

    // The runtime directory must itself be preloadable.
    //
#if LIBCOMPANION_CONTRACT >= LIBCOMPANION_CONTRACT_DEFAULT
    try
    {
      link_module_directory (m, d / "runtime directory");
      assert (false);
    }
    catch (const contract_violation& v)
    {
      assert (v.kind == contract_kind::precondition);
    }
#endif
  }

  // LD_PRELOAD value.
  //
  {
    assert (preload_value ("/opt/iw4x/lib/companion", "") ==
            "/opt/iw4x/lib/companion/$PLATFORM/libcompanion.so");

    assert (preload_value ("/opt/iw4x/lib/companion/", "") ==
            "/opt/iw4x/lib/companion/$PLATFORM/libcompanion.so");

    assert (preload_value ("/opt/iw4x/lib/companion", "libfoo.so") ==
            "/opt/iw4x/lib/companion/$PLATFORM/libcompanion.so libfoo.so");

#if LIBCOMPANION_CONTRACT >= LIBCOMPANION_CONTRACT_DEFAULT
    try
    {
      preload_value ("/home/user/Call of Duty/lib/companion", "");
      assert (false);
    }
    catch (const contract_violation& v)
    {
      assert (v.kind == contract_kind::precondition);
    }
#endif
  }
}
catch (const system_error& e)
{
  cerr << "error: " << e.what () << endl;
  return 1;
}
