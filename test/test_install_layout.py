#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""The installed data dir builds a module: every #include "x.h" in the C that
tools/build_module.py compiles (each game's *.c, runtime/module_entry.c)
resolves to an installed file, the game's own dir, runtime/, or a header the
recompiler generates. Installs the build into a temporary prefix; no compile.

usage: test_install_layout.py CMAKE BUILD_DIR
"""
import os
import re
import subprocess
import sys
import tempfile

GENERATED = {'ct_funcs.h', 'ct_overlays.h', 'ct_gen.h'}   # written into gen/ by recomp/
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    cmake, build = argv[1], argv[2]
    with tempfile.TemporaryDirectory(prefix='ct_install_') as prefix:
        r = subprocess.run([cmake, '--install', build, '--prefix', prefix],
                           capture_output=True, text=True)
        if r.returncode:
            print(r.stdout + r.stderr, file=sys.stderr)
            return 1
        data = os.path.join(prefix, 'share', 'ct-recomp')
        runtime = os.path.join(data, 'runtime')
        games = os.path.join(data, 'game')
        if not os.path.isdir(games):
            print('test_install_layout: no games installed (CT_ROM not set at configure?)')
            return 0
        sources = [os.path.join(runtime, 'module_entry.c')]
        for gid in sorted(os.listdir(games)):
            gdir = os.path.join(games, gid)
            sources += [os.path.join(gdir, f) for f in sorted(os.listdir(gdir)) if f.endswith('.c')]
        missing = []
        for src in sources:
            with open(src, errors='replace') as f:
                text = f.read()
            here = os.path.dirname(src)
            for inc in INCLUDE.findall(text):
                if os.path.basename(inc) in GENERATED:
                    continue
                if not any(os.path.exists(os.path.join(d, inc)) for d in (here, runtime)):
                    missing.append(f'{os.path.relpath(src, data)}: "{inc}"')
        for m in missing:
            print(f'test_install_layout: not installed: {m}', file=sys.stderr)
        print(f'test_install_layout: {len(sources)} files checked, {len(missing)} missing includes')
        return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
