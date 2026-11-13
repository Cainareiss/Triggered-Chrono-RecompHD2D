#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""The world-map menu opens and saves in-game (replay/world_map_save.txt).

usage: test_save_replay.py CT_BOOT [--interp-only]

Runs the replay to frame 10100 with --hash-log and checks:
- the run reaches the end: opening the menu runs decimal-mode ADC/SBC
  ($C2F15E, --expect-pc), which used to stop it at frame 7737;
- SRAM is untouched up to the save, changes at the edge of frame 8001 (the
  A press at 8000 on slot 1), and doesn't change again through the reset
  and the Continue that loads it.
The hash log is written to a temporary folder and deleted (it is derived
from the game).
"""
import os
import subprocess
import sys
import tempfile

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(GAME, 'test', 'replay', 'world_map_save.txt')
FRAMES = 10100
SAVE_EDGE = 8001


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    probe, extra = sys.argv[1], sys.argv[2:]
    with tempfile.TemporaryDirectory() as tmp:
        log = os.path.join(tmp, 'hash.log')
        r = subprocess.run([probe, '--frames', str(FRAMES), '--script', SCRIPT,
                            '--hash-log', log, '--expect-pc', 'C2F15E', *extra],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        out = [line for line in r.stdout.splitlines() if 'stubbed' not in line]
        if r.returncode != 0:
            print('\n'.join(out[-80:]))
            print(f'FAIL: ct_boot exited {r.returncode}')
            return 1
        sram = {}
        with open(log) as f:
            for line in f:
                fields = line.split()
                sram[int(fields[0])] = dict(kv.split('=') for kv in fields[1:])['sram']
    if len(sram) != FRAMES:
        print(f'FAIL: {len(sram)} frames in the hash log, want {FRAMES}')
        return 1
    changes = [f for f in range(2, FRAMES + 1) if sram[f] != sram[f - 1]]
    if changes != [SAVE_EDGE]:
        print(f'FAIL: SRAM changed at frames {changes[:20]}, want [{SAVE_EDGE}]')
        return 1
    print(f'save_replay: SRAM {sram[1]} -> {sram[SAVE_EDGE]} at frame {SAVE_EDGE}; '
          f'{FRAMES} frames, no fatal')
    return 0


if __name__ == '__main__':
    sys.exit(main())
