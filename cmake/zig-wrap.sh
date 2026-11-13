#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# zig-wrap.sh ZIG TOOL [ARGS...]: run "ZIG TOOL ARGS", for the wrappers
# cmake/toolchain-aarch64-zig.cmake writes. zig cc rejects CMake's
# "-Xlinker X" pairs (WHOLE_ARCHIVE libraries); the same as "-Wl,X".
zig=$1
shift
x=
for a do
    shift
    if [ -n "$x" ]; then
        set -- "$@" "-Wl,$a"
        x=
    elif [ "$a" = -Xlinker ]; then
        x=1
    else
        set -- "$@" "$a"
    fi
done
exec "$zig" "$@"
