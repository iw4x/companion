# companion - Steam client launcher for Companion on Linux

The `companion` executable is a launcher that starts the native Linux Steam
client with the Companion module preloaded.


## Usage

To start the Steam client with the Companion module preloaded, run
`companion` with `--steam`. The arguments after `--` are passed to the Steam
command, for example:

```
companion --steam -- -silent
```

See the `companion(1)` man page for details.


## Configuration variables

This package provides the following configuration variables:

```
[bool] config.companion.develop ?= false
```

If `config.companion.develop` is true, then the command line parser and the
man page are generated with the CLI compiler.
