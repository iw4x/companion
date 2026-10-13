# companion - Steam client launcher for IW4x companion on Linux.

The `companion` program starts the native Linux Steam client with the
Companion module preloaded, which lets IW4x running under Proton show Steam
friends what it is playing. The Steam client only loads the module at
startup, so if it is already running, then `companion` shuts it down and
starts it again. For details on how Companion works, refer to the Companion
manual.

## Usage

The module requires glibc 2.36 or later, and building it requires GCC 16 or
later together with the 32-bit glibc and libstdc++ development files.

The Steam client is a 32-bit program, and the programs that start it are
64-bit. Companion is therefore installed from two build configurations
into the same location: an i386 configuration, which provides the module,
and an x86_64 configuration, which provides the empty 64-bit stand-ins that
keep the dynamic linker of these programs from warning about the preload as
well as the `companion` program.

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
  config.install.filter='lib/companion/@true lib/@false include/@false'

bpkg create -d companion-x86_64 cc \
  config.cxx=g++                   \
  config.cc.coptions=-O2           \
  config.install.root=/usr/local   \
  config.install.sudo=sudo         \
  config.install.filter='lib/companion/@true lib/@false include/@false'
```

The installation filter limits the installation to the modules, the
program, and the documentation, and leaves out the library with its headers
and `pkg-config` files.

Then build and install the module in the i386 configuration and the
stand-ins together with the program in the x86_64 configuration:

```sh
bpkg build -d companion-i686 \
  libcompanion@https://github.com/iw4x/companion.git#main
bpkg install -d companion-i686 libcompanion

bpkg build -d companion-x86_64 \
  libcompanion@https://github.com/iw4x/companion.git#main \
  companion
bpkg install -d companion-x86_64 libcompanion companion
```

Finally, start the Steam client with the module preloaded:

```sh
companion --steam
```

The arguments after `--` are passed to the Steam command, for example,
`companion --steam -- -silent`. To run a different Steam command, set the
`COMPANION_STEAM` environment variable.

See the `companion(1)` man page for details.
