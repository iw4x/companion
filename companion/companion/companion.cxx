// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only WITH AdditionRef-IW4x-Exception-1.1

#include <unistd.h> // getuid(), geteuid()

#include <print>
#include <chrono>
#include <format>       // format_string
#include <string>
#include <vector>
#include <cstdlib>      // getenv()
#include <cstring>      // strcmp()
#include <sstream>
#include <iostream>     // cout, cerr
#include <filesystem>
#include <string_view>
#include <utility>      // forward()
#include <system_error> // system_error

#include <libcompanion/name.hxx>
#include <libcompanion/types.hxx>
#include <libcompanion/diagnostics.hxx>
#include <libcompanion/launch.hxx>
#include <libcompanion/version.hxx>
#include <libcompanion/preload.hxx>

#include <companion/utility.hxx>
#include <companion/version.hxx>
#include <companion/companion-options.hxx>

using namespace std;

namespace companion
{
  namespace
  {
    // Exception that indicates that the diagnostics has already been issued.
    //
    struct failed {};

    // Issue the error and throw failed. Each diagnostics message is a line
    // on stderr that starts with its kind and may continue with an info line
    // for each fact or suggestion ("...\n  info: ...").
    //
    template <diag_argument... A>
    [[noreturn]] void
    fail (format_string<A...> f, A&&... a)
    {
      cerr << text (format_diag ("error: ", f, forward<A> (a)...));
      throw failed ();
    }

    template <diag_argument... A>
    void
    info (format_string<A...> f, A&&... a)
    {
      cerr << text (format_diag ("info: ", f, forward<A> (a)...));
    }

    // How long we wait for the Steam client to exit once we ask it to and
    // how often we check.
    //
    constexpr chrono::seconds      exit_timeout (120);
    constexpr chrono::milliseconds exit_interval (500);

    // Start the Steam client with the module preloaded, executing the Steam
    // command with the arguments in our place (see launch.hxx in
    // libcompanion for the steps).
    //
    [[noreturn]] void
    start_steam (const vector<string>& args)
    {
      // The Steam command, which is searched for in PATH.
      //
      string steam;
      {
        const char* s (getenv ("COMPANION_STEAM"));
        steam = (s != nullptr && *s != '\0' ? s : "steam");
      }

      // Figure out the module directory. It is relative to the directory of
      // our executable, which only an installation fixes (see utility.hxx).
      //
      if (install_lib.empty ())
        fail ("unable to locate module: companion is not installed");

      filesystem::path d;
      {
        error_code ec;
        filesystem::path e (filesystem::read_symlink ("/proc/self/exe", ec));

        if (ec)
          fail ("unable to determine executable path: {}",
                ec.message ());

        d = (e.parent_path () / install_lib / preload_directory)
          .lexically_normal ();
      }

      if (!module_installed (d))
        fail ("no module in {}", (d / preload_platform).string ());

      // If Companion is already active in the running Steam client, then we
      // only pass the arguments on to it.
      //
      {
        filesystem::path s (unix_status_path (static_cast<user_id> (getuid ())).data ());
        bool a;

        try
        {
          a = companion_active (s);
        }
        catch (const system_error& e)
        {
          fail ("unable to inspect status file {}: {}",
                s.string (),
                e.code ().message ());
        }

        if (a)
        {
          info ("Companion is already active in the running Steam client");

          try
          {
            execute_steam (steam, args, "" /* preload */);
          }
          catch (const system_error& e)
          {
            fail ("unable to execute {}: {}",
                  steam,
                  e.code ().message ());
          }
        }
      }

      // The Steam client only loads the module at startup. So if it is
      // running, then we restart it. Otherwise steam passes the arguments on
      // to the running client. Note that we never kill the client and fail
      // if it doesn't exit on its own.
      //
      try
      {
        const user_id u (static_cast<user_id> (geteuid ()));

        if (steam_running ("/proc", u))
        {
          info ("shutting down Steam client for the module to load");

          try
          {
            request_steam_shutdown (steam);
          }
          catch (const system_error& e)
          {
            fail ("unable to execute {} -shutdown: {}",
                  steam,
                  e.code ().message ());
          }

          if (!wait_steam_exit ("/proc", u, exit_timeout, exit_interval))
            fail ("Steam client did not exit within {} seconds\n"
                  "  info: exit it and run 'companion --steam' again",
                  exit_timeout.count ());
        }
      }
      catch (const system_error& e)
      {
        fail ("unable to read directory /proc: {}",
              e.code ().message ());
      }

      // The dynamic linker splits LD_PRELOAD at spaces and colons and offers
      // no way to escape them. But when the module is distributed with the
      // game, the directory is inside the game installation, whose path
      // often contains spaces (for example, .../steamapps/common/Call of
      // Duty Modern Warfare 2/). So in this case we preload the module
      // through a symbolic link in the user's runtime directory, which is
      // private to the user.
      //
      // Note that the link is only needed while the Steam client starts
      // since the module removes itself from LD_PRELOAD (see host-linux.cxx
      // in libcompanion).
      //
      if (!preloadable (d))
      {
        const char* r (getenv ("XDG_RUNTIME_DIR"));

        if (r == nullptr || *r == '\0' || !preloadable (r))
          fail ("spaces or colons in module directory {}\n"
                "  info: XDG_RUNTIME_DIR is not set or contains them as well",
                d.string ());

        try
        {
          d = link_module_directory (d, r);
        }
        catch (const system_error& e)
        {
          fail ("unable to create symbolic link {}: {}",
                (filesystem::path (r) / preload_directory).string (),
                e.code ().message ());
        }
      }

      string p;
      {
        const char* v (getenv ("LD_PRELOAD"));
        p = preload_value (d, v != nullptr ? v : "");
      }

      try
      {
        execute_steam (steam, args, p);
      }
      catch (const system_error& e)
      {
        fail ("unable to execute {}: {}",
              steam,
              e.code ().message ());
      }
    }

    int
    main (int argc, char* argv[])
    try
    {
      // Parse the command line. The arguments after the first -- are for
      // the Steam command, so we only parse the ones before it. Anything
      // there that is not one of our options is an error.
      //
      int n (1);
      for (; n != argc && strcmp (argv[n], "--") != 0; ++n) ;

      vector<string> args;
      if (n != argc)
        args.assign (argv + n + 1, argv + argc);

      options o;

      try
      {
        o.parse (n, argv,
                 false /* erase */,
                 cli::unknown_mode::fail,
                 cli::unknown_mode::fail);
      }
      catch (const cli::exception& e)
      {
        ostringstream os;
        os << e;

        fail ("{}\n"
              "  info: run 'companion --help' for more information",
              os.str ());
      }

      if (o.help ())
      {
        print_companion_usage (cout);
        return 0;
      }

      if (o.version ())
      {
        println ("companion {}\n"
                 "libcompanion {}\n"
                 "Copyright (c) the IW4x authors.\n"
                 "This is free software released under the GNU General "
                 "Public License, version 3, with the IW4x Linking "
                 "Exception, version 1.1.",
                 COMPANION_VERSION_ID,
                 LIBCOMPANION_VERSION_ID);

        return 0;
      }

      if (!o.steam ())
      {
        if (n != argc)
          fail ("Steam arguments specified without --steam\n"
                "  info: run 'companion --help' for more information");

        fail ("nothing to do\n"
              "  info: specify --steam\n"
              "  info: run 'companion --help' for more information");
      }

      start_steam (args);
    }
    catch (const failed&)
    {
      return 1; // Diagnostics already issued.
    }
  }
}

int
main (int argc, char* argv[])
{
  return companion::main (argc, argv);
}
