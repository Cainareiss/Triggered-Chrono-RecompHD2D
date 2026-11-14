#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""The HD-2D side fill in a tall window (frontend/hd2d/shaders/final.frag).

usage: test_side_fill_tall.py FRONTEND [ARGS...]  (e.g. ct_sdl_engine --module M)

In a window taller than 4:3 the bars are above and below the picture. Their
fill is the picture's soft copy mirrored at its edge and fading with the
distance from it, as at the sides; it once smeared the edge row down the
whole bar at full strength. At frame 8850 of the Leene Square replay (the
fair, a bright top edge), in the hd2d look at 400x640: the rows at the
window's top must be much darker than the rows just above the picture.
Measured as it is made. Skips (77) where SDL's offscreen driver has no OpenGL.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from test_hdtext_shot import png_rows  # noqa: E402

SCRIPT = os.path.join(HERE, 'replay', 'leene_square.txt')
W, H, FRAME = 400, 640, 8850
PIC_Y = (H - W * 3 // 4) // 2   # the 4:3 picture's top


def rows_luma(rows, y0, y1):
    bpp = len(rows[0]) // W
    total, n = 0.0, 0
    for row in rows[y0:y1]:
        for x in range(0, W * bpp, bpp):
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
        path = os.path.join(tmp, 'tall.png')
        r = subprocess.run([*sys.argv[1:], '--script', SCRIPT, '--frames', str(FRAME + 2),
                            '--fast', '--mute', '--hd2d', '--preset', 'hd2d', '--aspect', '4:3',
                            '--no-save', '--shot', '%d:%s' % (FRAME, path),
                            '--shot-size', '%dx%d' % (W, H)],
                           env=env, capture_output=True, text=True)
        if r.returncode or not os.path.isfile(path):
            out = r.stdout + r.stderr
            print(out[-3000:])
            if 'GL' in out and ('context' in out or 'renderer' in out):
                print('test_side_fill_tall: no OpenGL offscreen here: skipped')
                return 77
            print('FAIL: the run stopped, exit %d' % r.returncode)
            return 1
        rows = png_rows(path)
    far, near = rows_luma(rows, 0, 12), rows_luma(rows, PIC_Y - 12, PIC_Y - 2)
    ok = near > 4.0 and far < near * 0.6
    print(('ok   ' if ok else 'FAIL ') +
          'top bar: mean luma %.1f at the window\'s edge, %.1f by the picture' % (far, near))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
