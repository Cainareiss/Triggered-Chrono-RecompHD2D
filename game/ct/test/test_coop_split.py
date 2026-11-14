#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Co-op split screen (experimental): replay/coop_split.txt in ct_boot.

usage: test_coop_split.py PROBE SCRIPT [probe args...]

With --coop-split, while player 2's party member is far from the leader
(frame 12700) the probe writes a second view (SHOT.view2.png): it must be
there, not blank, and not the first view; once she has walked back (frame
12800) there must be none. Without --coop-split there is never one.
Exit 0 on success, 1 otherwise.
"""
import os
import subprocess
import sys
import tempfile
import zlib


def pixels(path: str) -> bytes:
    """The decompressed image data of a PNG (enough to compare two)."""
    data = open(path, 'rb').read()
    idat, k = b'', 8
    while k < len(data):
        n = int.from_bytes(data[k:k + 4], 'big')
        if data[k + 4:k + 8] == b'IDAT':
            idat += data[k + 8:k + 8 + n]
        k += 12 + n
    return zlib.decompress(idat)


def run(probe: str, script: str, extra: list[str], tmp: str, tag: str) -> dict:
    shots = {f: os.path.join(tmp, f'{tag}_{f}.png') for f in (12700, 12800)}
    args = [probe, '--frames', '12800', '--script', script] + extra
    for f, p in shots.items():
        args += ['--shot', f'{f}:{p}']
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        print(f'test_coop_split: {tag}: probe exit {r.returncode}: {r.stdout[-300:]}')
        sys.exit(1)
    return shots


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    probe, script, extra = sys.argv[1], sys.argv[2], sys.argv[3:]
    with tempfile.TemporaryDirectory() as tmp:
        split = run(probe, script, extra + ['--coop-split'], tmp, 'split')
        view2 = split[12700] + '.view2.png'
        if not os.path.exists(view2):
            print('test_coop_split: no second view at frame 12700')
            return 1
        a, b = pixels(view2), pixels(split[12700])
        if a == b or len(set(a)) < 16:
            print('test_coop_split: the second view is blank or the first one')
            return 1
        if os.path.exists(split[12800] + '.view2.png'):
            print('test_coop_split: still two views at frame 12800')
            return 1
        coop = run(probe, script, extra + ['--coop'], tmp, 'coop')
        if any(os.path.exists(p + '.view2.png') for p in coop.values()):
            print('test_coop_split: a second view without --coop-split')
            return 1
    print('test_coop_split: second view at 12700, one view again at 12800')
    return 0


if __name__ == '__main__':
    sys.exit(main())
