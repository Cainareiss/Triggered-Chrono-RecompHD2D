#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Find the WRAM tables the HDMA of a mode 7 scene reads, for one frame.

A research aid (docs/SCENE.md, W11). Takes the per-line values a frame was
drawn with (from a `ct_boot --log-ppu 2105:2133@F LOG`, as m7_matrix.py) and
a WRAM dump of the same frame (`--wram FILE`), and searches the dump for each
of A, B, C, D as a run of 16-bit words at a stride of 2, 4, 6 or 8 bytes. The
HDMA source tables of the engine are in bank $7E; a match gives the address of
line `--first`'s word, so the table's start and its interleave follow.

usage: m7_tables.py LOG FRAME WRAM [--first 100] [--run 8]
"""
from __future__ import annotations

import argparse
import collections
import sys


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('frame', type=int)
    ap.add_argument('wram')
    ap.add_argument('--first', type=int, default=100, help='first line of the run')
    ap.add_argument('--run', type=int, default=8)
    a = ap.parse_args()
    pend: dict[str, list[int]] = collections.defaultdict(list)
    series: dict[str, list[int]] = collections.defaultdict(list)
    names = {'211B': 'A', '211C': 'B', '211D': 'C', '211E': 'D'}
    with open(a.log) as f:
        for ln in f:
            p = ln.split()
            if len(p) < 5 or int(p[0]) != a.frame or p[3].upper() not in names:
                continue
            if int(p[1]) >= 225:
                continue
            n = names[p[3].upper()]
            pend[n].append(int(p[4], 16))
            if len(pend[n]) == 2:
                series[n].append(pend[n][0] | pend[n][1] << 8)
                pend[n] = []
    w = open(a.wram, 'rb').read(0x20000)
    for n in 'ABCD':
        s = series.get(n, [])
        run = s[a.first:a.first + a.run]
        if len(run) < a.run or len(set(run)) < 3:
            print(f'{n}: run from line {a.first} is constant or short; pick --first')
            continue
        found = []
        for stride in (2, 4, 6, 8):
            for i in range(len(w) - stride * a.run):
                if all(w[i + k * stride] | w[i + k * stride + 1] << 8 == run[k]
                       for k in range(a.run)):
                    found.append((stride, i - stride * a.first))
        for stride, base in found:
            print(f'{n}: stride {stride}, line 0 would be at $7E{base & 0xFFFF:04X}'
                  f' (line {a.first} at $7E{(base + stride * a.first) & 0xFFFF:04X})')
        if not found:
            print(f'{n}: not found as a 16-bit run in WRAM')
    return 0


if __name__ == '__main__':
    sys.exit(main())
