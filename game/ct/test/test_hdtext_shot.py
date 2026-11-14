#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""HD text in the game (frontend/hdtext.c, game/ct/text.c): the dialogue box
at frame 4800 of replay/leene_square.txt, drawn with a font found on this
system ($CT_TEST_FONT first; none: skipped, 77) and without one.

usage: test_hdtext_shot.py CT_SDL [CT_SDL ARGS...]   (e.g. --module ct_game.so)

Both screenshots (--shot, SDL's offscreen video driver, the GL renderer
with the original look) go to a temporary folder and are deleted (they
show the game). Checked: above the box the two are the same, byte for
byte; in the box they differ (the font's text instead of the console's).
"""
import os
import subprocess
import sys
import tempfile
import zlib

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(GAME, 'test', 'replay', 'leene_square.txt')
FONTS = [
    '/usr/share/fonts/google-noto/NotoSans-Bold.ttf',
    '/usr/share/fonts/google-noto/NotoSans-Regular.ttf',
    '/usr/share/fonts/noto/NotoSans-Regular.ttf',
    '/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf',
    '/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf',
    '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
    '/usr/share/fonts/TTF/DejaVuSans.ttf',
    '/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf',
    '/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf',
]
W, H, FRAME = 640, 560, 4800


def png_rows(path: str) -> list[bytes]:
    """The rows of an 8-bit RGB or RGBA PNG (the frontend's writer), unfiltered."""
    with open(path, 'rb') as f:
        data = f.read()
    pos, idat, w, h, bpp = 8, b'', 0, 0, 0
    while pos < len(data):
        n = int.from_bytes(data[pos:pos + 4], 'big')
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h = int.from_bytes(body[0:4], 'big'), int.from_bytes(body[4:8], 'big')
            bpp = {2: 3, 6: 4}[body[9]]
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    rows, prev, stride = [], bytes(w * bpp), w * bpp
    for y in range(h):
        ft, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return rows


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    probe, extra = sys.argv[1], sys.argv[2:]
    font = os.environ.get('CT_TEST_FONT') or next((f for f in FONTS if os.path.isfile(f)), None)
    if not font:
        print('test_hdtext_shot: no font on this system: skipped (set CT_TEST_FONT)')
        return 77
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory() as tmp:
        env['XDG_DATA_HOME'] = tmp   # no saved settings, no packs
        shots = {}
        for name, opts in (('plain', []), ('font', ['--hd-font', font])):
            path = os.path.join(tmp, name + '.png')
            r = subprocess.run([probe, *extra, '--script', SCRIPT, '--frames', str(FRAME + 2),
                                '--fast', '--hd2d', '--preset', 'off', '--no-save',
                                '--shot', '%d:%s' % (FRAME, path), '--shot-size', '%dx%d' % (W, H),
                                *opts], env=env, capture_output=True, text=True)
            if r.returncode or not os.path.isfile(path):
                out = r.stdout + r.stderr
                if 'GL' in out and ('context' in out or 'renderer' in out) and r.returncode:
                    print(out[-2000:])
                    print('test_hdtext_shot: no OpenGL offscreen here: skipped')
                    return 77
                print(out[-3000:])
                print('FAIL: the run (%s) stopped, exit %d' % (name, r.returncode))
                return 1
            shots[name] = png_rows(path)
    a, b = shots['plain'], shots['font']
    box = int(H * 0.6)   # the box is the screen's lower part at this frame
    same_above = a[:box] == b[:box]
    differ = sum(1 for y in range(box, H) if a[y] != b[y])
    print(('ok   ' if same_above else 'FAIL ') + 'above the box: the same, byte for byte')
    print(('ok   ' if differ > H // 10 else 'FAIL ') +
          'in the box: %d of %d rows differ (the font\'s text)' % (differ, H - box))
    return 0 if same_above and differ > H // 10 else 1


if __name__ == '__main__':
    sys.exit(main())
