#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# trim-zig.sh ZIGDIR: shrink an unpacked zig release for the packages.
# It only ever compiles for the machine it runs on (x86_64 Linux), so the
# libc sources and headers of every other target can go (about 130 MB).
set -eu
z=$1
rm -rf "$z/doc"
libc="$z/lib/libc"
rm -rf "$libc/mingw" "$libc/wasi" "$libc/darwin" "$libc/freebsd" "$libc/netbsd"
for d in "$libc"/include/*; do
    case "${d##*/}" in
        any-linux-any | generic-glibc | generic-musl | x86-linux-* | x86_64-linux-*) ;;
        *) rm -rf "$d" ;;
    esac
done
