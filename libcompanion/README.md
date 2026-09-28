# libcompanion - Steam friends presence for IW4x.

The `libcompanion` package builds the IW4x companion, the module that runs
inside the Steam client and rewrites the "now playing" report of each
registered IW4x process, so that friends see IW4x as the game being played.
It contains the platform-independent `libcompanion` library, which parses
the Steam client image, locates and detours the send function, and rewrites
the reports, as well as the two modules built from it: `iw4x-steam64.dll`
for the 64-bit Windows Steam client and `libiw4x-steam.so` for the 32-bit
Linux Steam client, together with the Linux launcher, `iw4x-steam`.

## Usage

The modules are loaded into the Steam client, never linked against, so the
package is normally consumed by installing it. On Linux, install the module
and the launcher from an i386 configuration and the 64-bit stand-ins of the
module (which keep the 64-bit programs that start Steam from warning that
they cannot preload it) from an x86_64 configuration into the same
location:

```
b install: ../companion-gcc32/libcompanion/ config.install.root=/opt/iw4x
b install: ../companion-gcc/libcompanion/   config.install.root=/opt/iw4x
```

This also installs the library with its headers and `pkg-config` files. To
install only what running Steam requires, add the following to both
commands:

```
config.install.filter='lib/iw4x-steam/@true lib/@false include/@false'
```

Then start Steam through the launcher, `/opt/iw4x/bin/iw4x-steam`, after
exiting it if it is running. The launcher passes its arguments to `steam`,
and the `IW4X_STEAM` environment variable selects another command to run.

On Windows, the game installs `iw4x-steam64.dll` next to `iw4x.dll` and
loads it into the Steam client itself.

The library itself has no stable interface. To use it in another
`build2`-based project regardless, add the following `depends` value to
your `manifest`:

```
depends: libcompanion ^0.1.0
```

Then import the library in your `buildfile`:

```
import libs = libcompanion%lib{companion}
```

## Importable targets

This package provides the following importable targets:

```
lib{companion}
libs{iw4x-steam64}
libs{iw4x-steam}
exe{iw4x-steam}
```

The `lib{companion}` library holds the platform-independent parts of the
companion. `libs{iw4x-steam64}` is the Windows module and is only built for
x86_64 Windows targets. `libs{iw4x-steam}` is the Linux module and
`exe{iw4x-steam}` its launcher, both only built for i386 Linux targets (the
launcher only when installing).
