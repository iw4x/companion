# companion - Steam friends presence for IW4x.

`companion` is a Steam client module that shows IW4x to your Steam friends as
the game you are playing.

<p>
  <img alt="Steam friends window showing IW4x: Modern Warfare 2 as the game"
       src=".github/steam-friends.png" width="320">
</p>

This file contains setup instructions and other details that are more
appropriate for development. If you want to use `companion` in your
`build2`-based project, then see the accompanying package
[`README.md`](libcompanion/README.md) file.

## Development

The companion requires the `build2` toolchain 0.18.0 or later and GCC 16 or
later. The Linux module also requires the 32-bit glibc and libstdc++
development files (`glibc-devel.i686` and `libstdc++-devel.i686` on Fedora).
The Windows module requires MinGW-w64 GCC and, to run its tests on Linux,
[Wine](https://www.winehq.org/).

The development setup uses the standard `bdep`-based workflow with a build
configuration for each target. For example, on Linux:

```
git clone https://github.com/iw4x/companion.git
cd companion

bdep init -C ../companion-gcc @gcc cc \
  config.cxx=g++

bdep init -C ../companion-gcc32 @gcc32 cc \
  config.cxx='g++ -m32'                   \
  config.cxx.target=i686-linux-gnu        \
  config.c='gcc -m32'                     \
  config.c.target=i686-linux-gnu

bdep init -C ../companion-mingw @mingw cc \
  config.cxx=x86_64-w64-mingw32-g++

bdep update -a
bdep test @gcc @gcc32
```

Each configuration builds and tests the library. In addition, `@gcc` builds
the 64-bit stand-ins, `@gcc32` the Linux module and its launcher, and
`@mingw` the Windows module. Note that GCC reports the i386 target of `-m32`
only if it is built with multiarch support (Debian, Ubuntu), which is why
`@gcc32` specifies the target explicitly.

The `@mingw` tests run under Wine, which needs the MinGW-w64 runtime DLLs on
its search path. For example, on Fedora:

```
WINEPATH='Z:\usr\x86_64-w64-mingw32\sys-root\mingw\bin' \
  bdep test @mingw
```

On Windows, only the `@mingw` configuration applies. For example, with
MinGW-w64 GCC from [MSYS2](https://www.msys2.org/) in `PATH`, in the command
prompt (`cmd`), where `^` continues a line:

```
git clone https://github.com/iw4x/companion.git
cd companion

bdep init -C ..\companion-mingw @mingw cc ^
  config.cxx=g++

bdep update
bdep test
```

In PowerShell, a line is continued with a backtick. Note also that a
command line argument with a leading `@` has a special meaning in
PowerShell. To work around this, use the alternative `-@mingw` syntax:

```
bdep init -C ..\companion-mingw -@mingw cc `
  config.cxx=g++
```

The Steam client loads the Linux module only at startup, so trying a build
of the module requires a client restart. To install the module from the
`@gcc32` and `@gcc` configurations, restart Steam with the launcher, and
print the companion's diagnostics:

```
etc/private/steam-restart         \
  --install ../companion-gcc32    \
  --install ../companion-gcc
```

The comments at the beginning of each script in `etc/private/` describe its
options.

The library checks its preconditions and assertions by default. To also
check its postconditions and invariants, configure it with the audit
checking level (see `libcompanion/libcompanion/contract.hxx` for details):

```
b configure: ../companion-gcc/ \
  config.cxx.poptions=-DLIBCOMPANION_CONTRACT=2
```

## Contributing

See [`.github/CONTRIBUTING.md`](.github/CONTRIBUTING.md).

## License

`companion` is licensed under the GNU General Public License, version 3
(GPL-3.0-only). MinHook, vendored under `upstream/minhook/`, is licensed
under the BSD 2-Clause License.

See [`LICENSE.md`](LICENSE.md), [`LEGAL`](LEGAL), and [`AUTHORS`](AUTHORS).
