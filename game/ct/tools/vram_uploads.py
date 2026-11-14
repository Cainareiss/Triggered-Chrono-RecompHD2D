#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Which VRAM words a run rewrites, how often, and every how many frames.

A research aid (docs/SCENE.md, W14). Reads a `ct_boot --log-ppu 2115:2119@F LOG`
(or a wider range: `2115:2122` also gets CGRAM) log of PPU writes, follows
the VRAM address ($2116/$2117 set it, $2118/$2119 write and step it by the
$2115 increment mode) and groups the writes into runs: consecutive words of
one frame. Runs of the same start and length are one "upload". For each
upload it prints the frames it happened in, so the period is visible; with
--cgram it does the same for CGRAM ($2121 sets the colour index, $2122 writes
two bytes per colour), which is palette cycling.

--cgram-diff follows the CGRAM contents frame by frame instead and prints
which colour indices ever change, how many distinct values each takes, the
frames the changes happen in, and groups the indices that change together.

usage: vram_uploads.py LOG [--min-frames 2] [--cgram | --cgram-diff]
"""
from __future__ import annotations

import argparse
import collections


def cgram_diff(path: str) -> int:
    cg = [None] * 256
    cidx = 0
    cbyte: list[int] = []
    hist: dict[int, list[tuple[int, int]]] = collections.defaultdict(list)   # idx -> (frame, value)
    for ln in open(path):
        p = ln.split()
        if len(p) < 5:
            continue
        f, reg, val = int(p[0]), int(p[3], 16), int(p[4], 16)
        if reg == 0x2121:
            cidx, cbyte = val, []
        elif reg == 0x2122:
            cbyte.append(val)
            if len(cbyte) == 2:
                c = cbyte[0] | (cbyte[1] & 0x7F) << 8
                if cg[cidx] is not None and cg[cidx] != c:
                    hist[cidx].append((f, c))
                if cg[cidx] is None:
                    hist[cidx].append((f, c))
                cg[cidx] = c
                cidx = (cidx + 1) & 0xFF
                cbyte = []
    groups: dict[tuple, list[int]] = collections.defaultdict(list)
    for i, h in sorted(hist.items()):
        vals = [v for _, v in h]
        if len(h) < 3 and len(set(vals)) < 2:
            continue
        groups[tuple(f for f, _ in h[1:])].append(i)
    print('colour indices whose value changes (grouped by the frames it changes in):')
    for fr, idx in sorted(groups.items(), key=lambda kv: kv[1][0]):
        if not fr:
            continue
        d = [b - c for c, b in zip(fr, fr[1:])]
        per = collections.Counter(d).most_common(1)[0][0] if d else 0
        vals = {i: len(set(v for _, v in hist[i])) for i in idx}
        print(f'  indices {idx[0]:3d}..{idx[-1]:3d} ({len(idx)}): {len(fr)} changes, '
              f'every {per} frames, distinct values {min(vals.values())}-{max(vals.values())}; '
              f'frames {fr[:6]}')
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--min-frames', type=int, default=2,
                    help='hide uploads seen in fewer frames than this')
    ap.add_argument('--cgram', action='store_true')
    ap.add_argument('--cgram-diff', action='store_true')
    a = ap.parse_args()
    if a.cgram_diff:
        return cgram_diff(a.log)
    incr_hi = True      # $2115 bit 7: step after the high byte
    step = 1
    vaddr = 0
    cidx = 0
    cbyte: list[int] = []
    # upload key -> frames
    ups: dict[tuple, list[int]] = collections.defaultdict(list)
    contents: dict[tuple, set] = collections.defaultdict(set)
    frame_runs: dict[int, list[list[int]]] = collections.defaultdict(list)
    last_frame = -1
    run = None   # [start, words, frame, last_line]
    gap = None
    for ln in open(a.log):
        p = ln.split()
        if len(p) < 5:
            continue
        f, line, reg, val = int(p[0]), int(p[1]), int(p[3], 16), int(p[4], 16)
        if reg == 0x2115:
            incr_hi = bool(val & 0x80)
            step = (1, 32, 128, 128)[val & 3]
        elif reg == 0x2116:
            vaddr = (vaddr & 0xFF00) | val
            run = None
        elif reg == 0x2117:
            vaddr = (vaddr & 0xFF) | (val & 0x7F) << 8
            run = None
        elif reg in (0x2118, 0x2119) and not a.cgram:
            if reg == 0x2118 and incr_hi:
                continue
            if reg == 0x2119 and not incr_hi:
                continue
            if run is None or run[2] != f:
                run = [vaddr, 0, f, []]
                frame_runs[f].append(run)
            run[1] += 1
            run[3].append(val)
            vaddr = (vaddr + step) & 0x7FFF
        elif reg == 0x2121:
            cidx = val
            cbyte = []
            run = None
        elif reg == 0x2122 and a.cgram:
            cbyte.append(val)
            if len(cbyte) == 2:
                if run is None or run[2] != f:
                    run = [cidx, 0, f, []]
                    frame_runs[f].append(run)
                run[1] += 1
                run[3].extend(cbyte)
                cidx = (cidx + 1) & 0xFF
                cbyte = []
    for f, runs in frame_runs.items():
        for r in runs:
            ups[(r[0], r[1])].append(f)
            contents[(r[0], r[1])].add(hash(tuple(r[3])))
    unit = 'colour' if a.cgram else 'word'
    print(f'{"start":>6} {"len":>5}  frames-seen  period  distinct  first frames')
    for (s, n), fr in sorted(ups.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        if len(fr) < a.min_frames:
            continue
        d = [b - c for c, b in zip(fr, fr[1:])]
        per = collections.Counter(d).most_common(1)[0][0] if d else 0
        print(f'${s:04X} {n:5d} {unit}s  {len(fr):5d}  {per:5d}  {len(contents[(s, n)]):6d}  '
              + ' '.join(str(x) for x in fr[:8]))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
