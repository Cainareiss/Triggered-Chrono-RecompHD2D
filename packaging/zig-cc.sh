#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The bundled zig as a C compiler, installed as libexec/ct-recomp/zig-cc:
# tools/build_module.py runs $CC as one program, not as "zig cc".
here=$(dirname "$(readlink -f "$0")")
zig="${CT_RECOMP_ZIG:-$here/../../lib/zig/zig}"
[ -x "$zig" ] || zig=zig
# -g0 first: zig cc emits debug info by default; a later -g still wins.
exec "$zig" cc -g0 "$@"
