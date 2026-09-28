# companion - Steam friends presence for IW4x.

The companion makes Steam show IW4x to a player's friends as the game they
are playing. The Steam servers only show friends a game that the account
owns, and IW4x is not a Steam app. The companion is a module that runs
inside the Steam client, hooks the function through which the client sends
its messages to the Steam servers, and rewrites the "now playing" report of
each registered IW4x process. The report then names an app the account owns
(Call of Duty: Modern Warfare 2, or Spacewar if the account doesn't own it)
with the display name that the game publishes as the title.

The companion exists in two builds: a 64-bit Windows module that the game
loads into the Windows Steam client, and a 32-bit Linux module that is
preloaded into the native Linux Steam client for the game running under
Proton. It does not change anything else Steam sends, and it does nothing
until a game registers.

## Usage

On Windows, the game ships the module (`iw4x-steam64.dll`) next to
`iw4x.dll` and loads it into the running Steam client itself. Nothing needs
to be installed separately.

On Linux, the module has to be loaded when the Steam client starts. Install
it together with its launcher and start Steam through the launcher instead
of directly (exit Steam first if it is running):

```
iw4x-steam
```

The launcher accepts the same arguments as `steam`. See the package
[`README.md`](libcompanion/README.md) for how to build and install the
module and the launcher.

## Development

This repository holds the `libcompanion` package. The library is
platform-independent and is built and tested on any host, while each
companion module is built only for its target: the Windows module for
x86_64 Windows and the Linux module for i386 Linux (the Linux Steam client
is 32-bit). MinHook is vendored under `upstream/` as a documented exception
(see `LEGAL`).

The prerequisites are the [build2](https://build2.org/install.xhtml)
toolchain, version 0.18.0 or later, and GCC 16 or later. Building the Linux
module also requires the 32-bit glibc and libstdc++ development files (for
example, `glibc-devel.i686` and `libstdc++-devel.i686` on Fedora), and
building the Windows module on Linux requires a MinGW-w64 GCC and, to run
its tests, [Wine](https://www.winehq.org/).

The development setup uses the standard `bdep`-based workflow with one
configuration per target. On Linux:

```
git clone https://github.com/iw4x/companion.git
cd companion

bdep init -C ../companion-gcc @gcc cc config.cxx=g++

bdep init -C ../companion-gcc32 @gcc32 cc                 \
  config.cxx='g++ -m32' config.cxx.target=i686-linux-gnu  \
  config.c='gcc -m32'   config.c.target=i686-linux-gnu

bdep init -C ../companion-mingw @mingw cc \
  config.cxx=x86_64-w64-mingw32-g++

bdep update -a
bdep test @gcc @gcc32
```

The `@mingw` tests run under Wine and need the MinGW runtime DLLs on its
search path. For example, on Fedora:

```
WINEPATH='Z:\usr\x86_64-w64-mingw32\sys-root\mingw\bin' bdep test @mingw
```

The `@gcc` configuration builds the library, its tests, and the 64-bit
stand-in of the Linux module, `@gcc32` builds the Linux module and its
launcher, and `@mingw` builds the Windows module. Note that GCC only reports
the i386 target that `-m32` selects when it is built with multiarch support
(Debian, Ubuntu), which is why the target is specified explicitly above.

On Windows, use a MinGW-w64 GCC from, for example,
[MSYS2](https://www.msys2.org/):

```
git clone https://github.com/iw4x/companion.git
cd companion

bdep init -C ..\companion-mingw @mingw cc config.cxx=g++
bdep update
bdep test
```

To try a Linux build in the real Steam client, `etc/private/steam-restart`
installs the module from the `@gcc32` and `@gcc` configurations, restarts
Steam through the launcher, and prints the companion's diagnostics.

The contract checking level of a development build can be raised with
`config.cxx.poptions=-DLIBCOMPANION_CONTRACT=2` (see
`libcompanion/libcompanion/contract.hxx`).

## Contributing

See [`.github/CONTRIBUTING.md`](.github/CONTRIBUTING.md).

## License

companion is licensed under the GNU General Public License, version 3
(GPL-3.0-only). MinHook, vendored under `upstream/minhook/`, is licensed
under the BSD 2-Clause License.

See LICENSE.md, LEGAL, and AUTHORS.
