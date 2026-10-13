# libcompanion - Companion library and Steam client modules

The `libcompanion` C++ library provides the platform-independent parts of
Companion as well as the Companion modules built from it.

Note that the library is internal to Companion and its interface may change
in any release.


## Configuration variables

This package provides the following configuration variables:

```
[bool] config.libcompanion.develop ?= false
```

If `config.libcompanion.develop` is true, then the manual is generated with
the CLI compiler.
