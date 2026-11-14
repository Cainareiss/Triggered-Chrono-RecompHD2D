#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Simulated rumble (game/ct/rumble.c) on the gato_win replay, without a
controller: ct_boot --log-rumble lists the events, and they must be the
battle's, near the frames the HP changes (ct_boot --watch 5E30 / 6130:
Gato hits Crono at 10983, 11340, 12305, 12629 and 13020; Crono hits Gato
at 11115 and 12080, critically at 11729 and 13200, the last one fatal),
plus Leene's Bell in the intro (3545). Nothing else may rumble: no
events in the title, the name entry or the field walk. Rumble only reads:
the frame hashes with --log-rumble equal those without, and the module
build logs the same events.

usage: test_rumble_replay.py STATIC_BOOT ENGINE_BOOT MODULE SCRIPT FRAMES
"""
import os
import subprocess
import sys
import tempfile

# (frame, player, tag); frames as ct_boot's frame hook counts them
EXPECTED = [
    (3546, -1, 'bell'),
    (10984, 0, 'hurt'),
    (11116, 0, 'hit'),
    (11341, 0, 'hurt'),
    (11730, 0, 'crit'),
    (12081, 0, 'hit'),
    (12306, 0, 'hurt'),
    (12630, 0, 'hurt'),
    (13021, 0, 'hurt'),
    (13201, 0, 'crit'),
    (13201, 0, 'kill'),
]
SLACK = 3


def run(cmd: list[str]) -> None:
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f'failed: {" ".join(cmd)}')


def events(path: str) -> list[tuple[int, int, str, int, int, int]]:
    out = []
    with open(path) as f:
        for line in f:
            p = line.split()
            # rumble frame F player P source tag low high ms
            assert p[0] == 'rumble' and p[1] == 'frame' and p[3] == 'player', line
            out.append((int(p[2]), int(p[4]), p[6], int(p[7]), int(p[8]), int(p[9])))
    return out


def main(argv: list[str]) -> int:
    if len(argv) != 6:
        print(__doc__, file=sys.stderr)
        return 2
    static_boot, engine_boot, module, script, frames = argv[1:]
    common = ['--frames', frames, '--script', script]
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        log, log_m = os.path.join(d, 'rumble.txt'), os.path.join(d, 'rumble_module.txt')
        h_on, h_off = os.path.join(d, 'on.hash'), os.path.join(d, 'off.hash')
        run([static_boot] + common + ['--log-rumble', log, '--hash-log', h_on])
        run([static_boot] + common + ['--hash-log', h_off])
        run([engine_boot, '--module', module] + common + ['--log-rumble', log_m])
        with open(h_on) as a, open(h_off) as b:
            la, lb = a.readlines(), b.readlines()
        if la != lb or not la:
            n = next((k for k, (x, y) in enumerate(zip(la, lb), 1) if x != y), min(len(la), len(lb)))
            print(f'rumble_replay: frame hashes differ with rumble on (frame {n})')
            fails += 1
        ev, ev_m = events(log), events(log_m)
    if ev != ev_m:
        print(f'rumble_replay: the module logged other events:\n  static {ev}\n  module {ev_m}')
        fails += 1
    left = list(ev)
    for frame, player, tag in EXPECTED:
        hit = next((e for e in left if e[2] == tag and e[1] == player and abs(e[0] - frame) <= SLACK),
                   None)
        if hit is None:
            print(f'rumble_replay: no {tag} for player {player} near frame {frame}')
            fails += 1
            continue
        left.remove(hit)
        low, high, ms = hit[3:]
        if not (0 < max(low, high) <= 56000 and 50 <= ms <= 900):
            print(f'rumble_replay: {tag} at {hit[0]} out of range: {low} {high} {ms} ms')
            fails += 1
    if left:
        print(f'rumble_replay: unexpected events: {left}')
        fails += 1
    hurt = [e for e in ev if e[2] == 'hurt']
    if hurt and not min(e[3] for e in hurt if e[0] == 12630) < min(e[3] for e in hurt if e[0] != 12630):
        print('rumble_replay: the 1 HP hit (12630) should be the lightest')
        fails += 1
    print(f'rumble_replay: {len(ev)} events, {len(la)} frames, {fails} failed')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
