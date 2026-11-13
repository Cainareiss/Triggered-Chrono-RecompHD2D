#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/replay_gen.py: same seed, same file; spans in range, in the
replay format (runtime/replay.h), the prefix kept verbatim."""
import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(REPO, 'tools', 'replay_gen.py')
NAMES = {'b', 'y', 'select', 'start', 'up', 'down', 'left', 'right', 'a', 'x', 'l', 'r'}


def gen(*args: str) -> str:
    return subprocess.run([sys.executable, GEN, *args], check=True,
                          capture_output=True, text=True).stdout


def main() -> int:
    fails = []
    a = gen('--seed', '7', '--from', '100', '--to', '5000')
    if a != gen('--seed', '7', '--from', '100', '--to', '5000'):
        fails.append('not deterministic')
    if a == gen('--seed', '8', '--from', '100', '--to', '5000'):
        fails.append('seed ignored')
    last = 0
    for line in a.splitlines():
        if line.startswith('#'):
            continue
        m = re.fullmatch(r'(\d+)-(\d+):([a-z+]+)', line)
        if not m:
            fails.append(f'bad line {line!r}')
            continue
        f1, f2 = int(m[1]), int(m[2])
        if not (100 <= f1 <= f2 <= 5000) or f1 <= last:
            fails.append(f'span out of range or order: {line}')
        if not set(m[3].split('+')) <= NAMES:
            fails.append(f'unknown button: {line}')
        last = f2
    menu = gen('--seed', '1', '--from', '1', '--to', '3000', '--buttons', 'x:1,a:1')
    if any(not set(l.split(':')[1].split('+')) <= {'x', 'a'}
           for l in menu.splitlines() if not l.startswith('#')):
        fails.append('--buttons not honored')
    with tempfile.TemporaryDirectory() as tmp:
        pre = os.path.join(tmp, 'pre.txt')
        with open(pre, 'w') as f:
            f.write('# route\n1-5:start\n')
        out = gen('--seed', '1', '--from', '10', '--to', '20', '--prefix', pre)
        if not out.startswith('# route\n1-5:start\n'):
            fails.append('prefix not kept')
    for msg in fails:
        print('FAIL:', msg)
    print(f'replay_gen: {len(fails)} failed')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
