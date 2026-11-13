#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""The engine-only build plus the game module must run exactly as the
static build: same per-frame CPU, memory, video and audio hashes.

usage: test_module_parity.py STATIC_BOOT ENGINE_BOOT MODULE SCRIPT FRAMES
"""
import os
import subprocess
import sys
import tempfile


def run(cmd: list[str], log: str) -> None:
    r = subprocess.run(cmd + ['--hash-log', log], capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f'failed: {" ".join(cmd)}')


def main(argv: list[str]) -> int:
    if len(argv) != 6:
        print(__doc__, file=sys.stderr)
        return 2
    static_boot, engine_boot, module, script, frames = argv[1:]
    common = ['--frames', frames, '--script', script]
    with tempfile.TemporaryDirectory() as d:
        a, b = os.path.join(d, 'static.hash'), os.path.join(d, 'module.hash')
        run([static_boot] + common, a)
        run([engine_boot, '--module', module] + common, b)
        with open(a) as fa, open(b) as fb:
            la, lb = fa.readlines(), fb.readlines()
    for n, (x, y) in enumerate(zip(la, lb), 1):
        if x != y:
            print(f'module_parity: frame {n} differs\n  static: {x.strip()}\n  module: {y.strip()}')
            return 1
    if len(la) != len(lb) or not la:
        print(f'module_parity: {len(la)} static frames, {len(lb)} module frames')
        return 1
    print(f'module_parity: {len(la)} frames identical')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
