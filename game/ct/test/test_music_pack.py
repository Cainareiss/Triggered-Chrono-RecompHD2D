#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Audio packs end to end (docs/ASSET-PACKS.md): the Gato fight's song
(replay/gato_win.txt, song $45 from about frame 10654) replaced with a
1 kHz tone this test writes (no recording of anything).

usage: test_music_pack.py CT_BOOT [CT_BOOT ARGS...]   (e.g. --module ct_game.so)

Two runs to frame 10900, with and without the pack, each writing the
DSP output (--wav) to a temporary folder (deleted: it is the game's sound):
- --music-log names song $45 as it starts, with the pack's file;
- up to then the two runs' sound is the same, byte for byte;
- from then on, the replaced run holds the tone at its level, and what
  is left when the tone is taken out (the sound effects) is much quieter
  than the music of the run without the pack: the music voices are muted.
"""
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import wave

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(GAME, 'test', 'replay', 'gato_win.txt')
FRAMES = 10900
SONG = 0x45
HZ = 32040.0
FPS = 60.0988
TONE_HZ, TONE_AMP = 1000.0, 6000.0


def write_tone(path: str) -> None:
    rate, n = 44100, 44100 * 2   # 2 s, looped whole: 2000 periods
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for k in range(n):
            v = int(round(TONE_AMP * math.sin(2 * math.pi * TONE_HZ * k / rate)))
            frames += struct.pack('<hh', v, v)
        w.writeframes(bytes(frames))


def read_left(path: str) -> list[int]:
    with wave.open(path, 'rb') as w:
        data = w.readframes(w.getnframes())
    return list(struct.unpack('<%dh' % (len(data) // 2), data))[0::2]


def fit(block: list[int], start: int) -> tuple[float, float]:
    """Least-squares amplitude of the 1 kHz tone in block, and the RMS of what is left."""
    w = 2 * math.pi * TONE_HZ / HZ
    c = [math.cos(w * (start + k)) for k in range(len(block))]
    s = [math.sin(w * (start + k)) for k in range(len(block))]
    cc, ss, cs = sum(x * x for x in c), sum(x * x for x in s), sum(a * b for a, b in zip(c, s))
    yc, ys = sum(a * b for a, b in zip(block, c)), sum(a * b for a, b in zip(block, s))
    det = cc * ss - cs * cs
    a, b = (yc * ss - ys * cs) / det, (ys * cc - yc * cs) / det
    res = [y - a * x - b * z for y, x, z in zip(block, c, s)]
    return math.hypot(a, b), math.sqrt(sum(r * r for r in res) / len(res))


def rms(block: list[int]) -> float:
    return math.sqrt(sum(x * x for x in block) / len(block))


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    probe, extra = sys.argv[1], sys.argv[2:]
    fails = 0

    def check(ok: bool, what: str) -> None:
        nonlocal fails
        print(('ok   ' if ok else 'FAIL ') + what)
        fails += not ok

    with tempfile.TemporaryDirectory() as tmp:
        pack = os.path.join(tmp, 'tonepack')
        os.makedirs(os.path.join(pack, 'music'))
        write_tone(os.path.join(pack, 'music', 'tone.wav'))
        with open(os.path.join(pack, 'pack.toml'), 'w') as f:
            f.write('# A synthetic tone for test_music_pack.py.\n[pack]\nversion = 1\n'
                    'name = "tonepack"\n\n[[music]]\nsong = 0x%02X\nfile = "music/tone.wav"\n'
                    % SONG)
        a_wav, b_wav = os.path.join(tmp, 'a.wav'), os.path.join(tmp, 'b.wav')
        base = [probe, *extra, '--frames', str(FRAMES), '--script', SCRIPT]
        runs = [subprocess.Popen(base + ['--wav', a_wav, '--music-pack', pack, '--music-log'],
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True),
                subprocess.Popen(base + ['--wav', b_wav], stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True)]
        outs = [r.communicate()[0] for r in runs]
        for r, o in zip(runs, outs):
            if r.returncode:
                print(o[-3000:])
                print('FAIL: the run stopped (exit %d)' % r.returncode)
                return 1
        m = re.search(r'music: frame (\d+): play song 0x%02X \((.*)\)' % SONG, outs[0])
        check(bool(m) and 'tone.wav' in m.group(2),
              '--music-log: song $%02X starts, replaced by the pack' % SONG)
        if not m:
            print(outs[0][-2000:])
            return 1
        start = int(m.group(1))
        check(10600 < start < 10720, 'song $%02X starts at frame %d (about 10654)' % (SONG, start))
        a, b = read_left(a_wav), read_left(b_wav)
        cut = int((start - 2) * HZ / FPS)
        check(a[:cut] == b[:cut], 'before it, the sound is the same, byte for byte (%d samples)'
              % cut)
        blk = 534
        amps, left, music = [], [], []
        for at in range(int((start + 5) * HZ / FPS), min(len(a), len(b)) - blk, blk):
            amp, res = fit(a[at:at + blk], at)
            amps.append(amp)
            left.append(res)
            music.append(rms(b[at:at + blk]))
        amps.sort()
        med = amps[len(amps) // 2]
        check(abs(med - TONE_AMP) < 0.1 * TONE_AMP,
              'the tone plays at its level: amplitude %.0f (%.0f written)' % (med, TONE_AMP))
        lm, mm = sum(left) / len(left), sum(music) / len(music)
        check(lm < 0.4 * mm, 'the music is muted: %.0f RMS left besides the tone, the song '
              'without the pack %.0f' % (lm, mm))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
