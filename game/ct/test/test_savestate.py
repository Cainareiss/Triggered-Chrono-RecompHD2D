#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Save states (runtime/savestate.h) are deterministic: on a replay, run
to frame N and save; the run goes on to frame T. Then start from the
state instead and run to T. Every frame after the save must have the same
per-frame hashes (CPU, WRAM, SRAM, VRAM, CGRAM, OAM, the frame picture,
APU) in both runs, and saving must not change the run that saved. Then
--state-roundtrip K: a state saved and loaded straight back every K
frames (a new CPU coroutine each time, native frames dropped) must leave
the whole run unchanged.

usage: test_savestate.py CT_BOOT SCRIPT N T K [ct_boot options, e.g. --interp-only]
"""
import os
import subprocess
import sys
import tempfile


def run(cmd: list[str]) -> str:
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f'failed: {" ".join(cmd)}')
    return r.stdout


def lines(path: str) -> list[str]:
    with open(path) as f:
        return f.readlines()


def first_diff(a: list[str], b: list[str]) -> str | None:
    for x, y in zip(a, b):
        if x != y:
            return f'  expected: {x.strip()}\n  got:      {y.strip()}'
    if len(a) != len(b):
        return f'  {len(a)} frames expected, {len(b)} got'
    return None


def main(argv: list[str]) -> int:
    if len(argv) < 6:
        print(__doc__, file=sys.stderr)
        return 2
    boot, script, n, t, k = argv[1:6]
    extra = argv[6:]
    common = [boot, '--script', script, '--frames', t] + extra
    with tempfile.TemporaryDirectory() as d:
        state = os.path.join(d, 's.state')
        plain, saved, loaded, rt = (os.path.join(d, x) for x in ('plain', 'saved', 'loaded', 'rt'))
        run(common + ['--hash-log', plain])
        out = run(common + ['--hash-log', saved, '--save-state', f'{n}:{state}'])
        at = [x for x in out.splitlines() if 'state saved at frame' in x]
        if not at:
            print('savestate: no state was saved')
            return 1
        frame = int(at[0].split('state saved at frame ')[1].split()[0])
        run(common + ['--hash-log', loaded, '--load-state', state])
        out = run(common + ['--hash-log', rt, '--state-roundtrip', k])
        trips = [x for x in out.splitlines() if 'state round trips' in x]
        p, s, l, r = lines(plain), lines(saved), lines(loaded), lines(rt)
    tail = [x for x in s if int(x.split()[0]) > frame]
    bad = 0
    for what, exp, got in (('saving changed the run', p, s),
                           (f'the run from the state saved at frame {frame} differs', tail, l),
                           (f'round trips every {k} frames changed the run', p, r)):
        d = first_diff(exp, got)
        if d:
            print(f'savestate: {what}\n{d}')
            bad = 1
    if not bad:
        print(f'savestate: saved at frame {frame}, {len(l)} frames after it identical; '
              f'{trips[0].split("ct_boot: ")[-1] if trips else "? round trips"}, '
              f'{len(p)} frames identical')
    return bad


if __name__ == '__main__':
    sys.exit(main(sys.argv))
