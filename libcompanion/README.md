# libcompanion - Steam friends presence for IW4x.

The `libcompanion` C++ library provides the platform-independent parts of the
IW4x Steam companion, a Steam client module that lets IW4x choose what Steam
friends see as the game being played, the line under the player's name in
their friends list. This package also contains the companion modules built
from it: `iw4x-steam64.dll` for the Windows Steam client and
`libiw4x-steam.so` with its `iw4x-steam` launcher for the native Linux Steam
client.

## Usage

On Windows, the game comes with `iw4x-steam64.dll` and loads it into the
Steam client. There is nothing to install.

On Linux, the `iw4x-steam` launcher starts the native Steam client with the
module preloaded. The module requires glibc 2.36 or later, and building it
requires GCC 16 or later together with the 32-bit glibc and libstdc++
development files.

The Steam client is a 32-bit program, and the programs that start it are
64-bit. The companion is therefore installed from two build configurations
into the same location: an i386 configuration, which provides the module and
the launcher, and an x86_64 configuration, which provides the empty 64-bit
stand-ins that keep the dynamic linker of these programs from warning about
the preload.

First, create the two configurations:

```sh
bpkg create -d companion-i686 cc   \
  config.cxx='g++ -m32'            \
  config.cxx.target=i686-linux-gnu \
  config.c='gcc -m32'              \
  config.c.target=i686-linux-gnu   \
  config.cc.coptions=-O2           \
  config.install.root=/usr/local   \
  config.install.sudo=sudo         \
  config.install.filter='lib/iw4x-steam/@true lib/@false include/@false'

bpkg create -d companion-x86_64 cc \
  config.cxx=g++                   \
  config.cc.coptions=-O2           \
  config.install.root=/usr/local   \
  config.install.sudo=sudo         \
  config.install.filter='lib/iw4x-steam/@true lib/@false include/@false'
```

The installation filter limits the installation to the modules, the
launcher, and the documentation, and leaves out the library with its headers
and `pkg-config` files.

Then build and install the companion in each configuration:

```sh
bpkg build -d companion-i686 \
  libcompanion@https://github.com/iw4x/companion.git#main
bpkg install -d companion-i686 libcompanion

bpkg build -d companion-x86_64 \
  libcompanion@https://github.com/iw4x/companion.git#main
bpkg install -d companion-x86_64 libcompanion
```

Finally, exit Steam if it is running and start it with the launcher:

```sh
iw4x-steam
```

The launcher passes its arguments to `steam` and fails if Steam is already
running. To run a different Steam command, set the `IW4X_STEAM` environment
variable.

To start using `libcompanion` in your project, add the following `depends`
value to your `manifest`, adjusting the version constraint as appropriate:

```
depends: libcompanion ^0.1.0
```

Then import the library in your `buildfile`:

```
import libs = libcompanion%lib{companion}
```

Note that the library interface is internal to the companion and is not
stable.

## Importable targets

This package provides the following importable targets:

```
lib{companion}
libs{iw4x-steam64}
libs{iw4x-steam}
exe{iw4x-steam}
```

The `lib{companion}` library contains the platform-independent parts of the
companion along with MinHook. The MinHook headers are installed into the
`libcompanion/minhook/` subdirectory: `hde32.h` and `hde64.h`, the instruction
decoders, on every target and `minhook.h`, the hooking engine, only on Windows.
The `libs{iw4x-steam64}` target is the Windows module and is only built for
x86_64 Windows. The `libs{iw4x-steam}` target is the Linux module and
`exe{iw4x-steam}` is its launcher. Both are only built for i386 Linux and the
launcher only when installing.
