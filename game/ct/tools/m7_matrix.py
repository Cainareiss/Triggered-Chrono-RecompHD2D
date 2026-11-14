#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Per-scanline mode 7 state of one frame, from a ct_boot --log-ppu log.

A research aid (docs/SCENE.md, W11). Reads "frame line dot reg value" records
(`ct_boot --log-ppu 2105:2133@F LOG`, every PPU write, CPU, DMA and HDMA) and
for one frame prints, per scanline, the values the line was drawn with:
BGMODE ($2105), the matrix A B C D ($211B-$211E, each two writes, low then
high, signed 8.8), the centre X0 Y0 ($211F/$2120, 13-bit signed) and the
BG1 scroll ($210D/$210E, 13-bit); the state carries over from the previous
frame (its writes need to be in the log). A line that has no write keeps the earlier
value; `*` marks a value written on that line (an HDMA entry shows as a run
of `*`). The log's line is the line the write happened on (HDMA writes land
at dot ~278+ of the line before the one they shape: the logged line L
shapes line L+1; verify with --hdma-lag).

usage: m7_matrix.py LOG FRAME [--every N] [--from L --to L] [--summary]
"""
from __future__ import annotations

import argparse
import collections


def s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def s13(v: int) -> int:
    v &= 0x1FFF
    return v - 0x2000 if v & 0x1000 else v


def load(path: str, frame: int):
    recs = []
    with open(path) as f:
        for ln in f:
            p = ln.split()
            if len(p) < 5 or int(p[0]) not in (frame - 1, frame):
                continue
            # the previous frame's records come first, as line - 1000
            ln = int(p[1]) - (1000 if int(p[0]) != frame else 0)
            recs.append((ln, int(p[2]), int(p[3], 16), int(p[4], 16)))
    return recs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('frame', type=int)
    ap.add_argument('--every', type=int, default=1)
    ap.add_argument('--from', dest='lo', type=int, default=0)
    ap.add_argument('--to', dest='hi', type=int, default=261)
    ap.add_argument('--summary', action='store_true',
                    help='per register: writes, lines touched, first/last line')
    a = ap.parse_args()
    recs = load(a.log, a.frame)
    if a.summary:
        by = collections.defaultdict(list)
        for ln, dot, reg, v in recs:
            if ln >= 0:
                by[reg].append(ln)
        for reg in sorted(by):
            ls = by[reg]
            print(f'${reg:04X}: {len(ls):4d} writes, {len(set(ls)):3d} lines, '
                  f'first {min(ls)} last {max(ls)}')
        return 0
    # Registers with a write-twice latch: collect bytes in order.
    state = {'mode': None, 'A': 0, 'B': 0, 'C': 0, 'D': 0, 'X0': 0, 'Y0': 0, 'H': 0, 'V': 0}
    pend: dict[int, list[int]] = {}
    names = {0x211B: 'A', 0x211C: 'B', 0x211D: 'C', 0x211E: 'D',
             0x211F: 'X0', 0x2120: 'Y0', 0x210D: 'H', 0x210E: 'V'}
    per_line: dict[int, dict] = collections.defaultdict(dict)
    for ln, dot, reg, v in recs:
        if ln < 0:
            ln = -1   # the previous frame (state carried over)
        if reg == 0x2105:
            per_line[ln]['mode'] = v & 7
        elif reg in names:
            lst = pend.setdefault(reg, [])
            lst.append(v)
            if len(lst) == 2:
                w = lst[0] | lst[1] << 8
                per_line[ln][names[reg]] = s16(w) if reg < 0x211F and reg > 0x211A \
                    else s13(w)
                pend[reg] = []
    state.update(per_line.get(-1, {}))
    cols = ['mode', 'A', 'B', 'C', 'D', 'X0', 'Y0', 'H', 'V']
    print('line ' + ' '.join(f'{c:>6}' for c in cols))
    for ln in range(a.lo, min(a.hi, 261) + 1):
        for c in cols:
            if c in per_line.get(ln, {}):
                state[c] = per_line[ln][c]
        if (ln - a.lo) % a.every:
            continue
        row = []
        for c in cols:
            v = state[c]
            mark = '*' if c in per_line.get(ln, {}) else ' '
            row.append(f'{"-" if v is None else v:>5}{mark}')
        print(f'{ln:4d} ' + ' '.join(row))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
