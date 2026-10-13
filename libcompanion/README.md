# libcompanion - Steam friends presence for IW4x.

The `libcompanion` C++ library provides the platform-independent parts of the
IW4x Steam companion, a Steam client module that lets IW4x choose what Steam
friends see as the game being played, the line under the player's name in
their friends list. This package also contains Companion modules built
from it: `companion64.dll` for the Windows Steam client and
`libcompanion.so` for the native Linux Steam client.

## Usage

On Windows, the game comes with `companion64.dll` and loads it into the
Steam client. There is nothing to install.

On Linux, the `companion` program starts the native Steam client with the
module preloaded. See the `companion` package for how to install it together
with the module.

To start using `libcompanion` in your project, add the following `depends`
value to your `manifest`, adjusting the version constraint as appropriate:

```
depends: libcompanion ^0.1.0
```

Then import the library in your `buildfile`:

```
import libs = libcompanion%lib{companion}
```

Note that the library interface is internal to Companion and is not
stable.

## Importable targets

This package provides the following importable targets:

```
lib{companion}
libs{companion64}
i686/libs{companion}
```

The `lib{companion}` library contains the platform-independent parts of the
companion along with MinHook. The MinHook headers are installed into the
`libcompanion/minhook/` subdirectory: `hde32.h` and `hde64.h`, the instruction
decoders, on every target and `minhook.h`, the hooking engine, only on Windows.
The `libs{companion64}` target is the Windows module and is only built for
x86_64 Windows. The `i686/libs{companion}` target is the Linux module and is only built for
i386 Linux.
