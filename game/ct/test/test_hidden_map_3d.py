#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""The 3D diorama look only where the console shows the map (frontend/hd2d).

usage: test_hidden_map_3d.py FRONTEND [ARGS...]  (e.g. ct_sdl_engine --module M)

At frame 3700 of the Leene Square replay the narrator speaks on a black
screen: the house's map is already loaded (the module reports a field
scene) but BG1 and BG2 are off on both screens. The hd2d3d look must show
the console's black picture there, not the loaded map in 3D (a bright grid
of tiles, as it once did). At frame 4400 the house is on screen and the 3D
view is back. Both pictures are measured as they are made: the mean
brightness of the screen's lower half (the narrator's box is at the top).

Skips (77) where SDL's offscreen driver has no OpenGL.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from test_hdtext_shot import png_rows  # noqa: E402

SCRIPT = os.path.join(HERE, 'replay', 'leene_square.txt')
W, H = 512, 448
HIDDEN, SHOWN = 3700, 4400


def lower_luma(path: str) -> float:
    rows = png_rows(path)
    bpp = len(rows[0]) // W
    total, n = 0.0, 0
    for row in rows[H // 2:]:
        for x in range(0, W * bpp, bpp * 2):
            total += 0.299 * row[x] + 0.587 * row[x + 1] + 0.114 * row[x + 2]
            n += 1
    return total / n


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory() as tmp:
        env['XDG_DATA_HOME'] = tmp   # no saved settings, no packs
        env['XDG_CONFIG_HOME'] = tmp
        paths = {f: os.path.join(tmp, '%d.png' % f) for f in (HIDDEN, SHOWN)}
        cmd = [*sys.argv[1:], '--script', SCRIPT, '--frames', str(SHOWN + 2), '--fast',
               '--mute', '--hd2d', '--preset', 'hd2d3d', '--no-save', '--shot-size',
               '%dx%d' % (W, H)]
        for f, p in paths.items():
            cmd += ['--shot', '%d:%s' % (f, p)]
        r = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if r.returncode or not all(os.path.isfile(p) for p in paths.values()):
            out = r.stdout + r.stderr
            print(out[-3000:])
            if 'GL' in out and ('context' in out or 'renderer' in out):
                print('test_hidden_map_3d: no OpenGL offscreen here: skipped')
                return 77
            print('FAIL: the run stopped, exit %d' % r.returncode)
            return 1
        hidden, shown = lower_luma(paths[HIDDEN]), lower_luma(paths[SHOWN])
    ok_hidden, ok_shown = hidden < 3.0, shown > 6.0
    print(('ok   ' if ok_hidden else 'FAIL ') +
          'frame %d (map loaded, BG1/BG2 off): lower half mean luma %.1f (< 3)' % (HIDDEN, hidden))
    print(('ok   ' if ok_shown else 'FAIL ') +
          'frame %d (the house in 3D): lower half mean luma %.1f (> 6)' % (SHOWN, shown))
    return 0 if ok_hidden and ok_shown else 1


if __name__ == '__main__':
    sys.exit(main())
