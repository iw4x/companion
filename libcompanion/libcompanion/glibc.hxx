// Copyright (c) the IW4x authors (see the AUTHORS file).
// SPDX-License-Identifier: GPL-3.0-only WITH AdditionRef-IW4x-Exception-1.1

// glibc symbol version bindings for the glibc floor (see glibc.cxx).
//
// By default a glibc symbol reference binds to the latest version of the
// symbol in the glibc used for the build, which can exceed the glibc on the
// host that runs the binary. Every translation unit of a binary within the
// floor includes this header first (-include), which binds each symbol to
// its latest version at the floor. The versions differ between
// architectures, so we generate the bindings for each of them (see
// etc/private/glibc-symbols).
//
#pragma once

#if defined(__i386__)
#  include <libcompanion/glibc-i386.hxx>
#elif defined(__x86_64__)
#  include <libcompanion/glibc-x86_64.hxx>
#else
#  error no glibc floor bindings for this architecture
#endif
