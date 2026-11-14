#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Audio packs' decoders (frontend/packs/music.c: stb_vorbis, dr_flac) on
tones made here with ffmpeg (a 440 Hz sine it generates: no recording
anywhere), played through test_music. Skipped (77) without ffmpeg or its
Vorbis encoder.

usage: test_music_codecs.py TEST_MUSIC OUT_DIR
"""
import os
import shutil
import subprocess
import sys


def main() -> int:
    test_music, out = sys.argv[1], sys.argv[2]
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        print('test_music_codecs: no ffmpeg: skipped')
        return 77
    music = os.path.join(out, 'pack', 'testpack', 'music')   # where test_music's pack is
    os.makedirs(music, exist_ok=True)
    encoders = subprocess.run([ffmpeg, '-hide_banner', '-encoders'], capture_output=True,
                              text=True).stdout
    vorbis = 'libvorbis' if 'libvorbis' in encoders else None
    files = []
    for name, codec in (('tone.flac', 'flac'), ('tone.ogg', vorbis)):
        if not codec:
            continue
        path = os.path.join(music, name)
        extra = ['-strict', '-2'] if codec == 'vorbis' else []
        r = subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi',
                            '-i', 'sine=frequency=440:sample_rate=44100:duration=1',
                            '-ac', '2', '-c:a', codec, *extra, path])
        if r.returncode == 0:
            files.append('music/' + name)   # relative to the pack, as pack.toml has it
    if not any(f.endswith('.ogg') for f in files):
        print('test_music_codecs: ffmpeg made no Ogg Vorbis file: skipped')
        return 77
    return subprocess.run([test_music, os.path.join(out, 'pack'), *files]).returncode


if __name__ == '__main__':
    sys.exit(main())
