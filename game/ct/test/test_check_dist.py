#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/check_dist.py must pass the engine-only programs and fail
everything built from the ROM: the game module, the static programs, and
pieces of the ROM hidden in archives.

usage: test_check_dist.py CHECK_DIST ROM --pass FILE... --fail FILE...
"""
import io
import os
import subprocess
import sys
import tarfile
import tempfile
import zipfile


def check(tool: str, rom: str, paths: list[str]) -> tuple[int, str]:
    r = subprocess.run([sys.executable, tool, *paths, '--rom', rom, '--quiet'],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main(argv: list[str]) -> int:
    if len(argv) < 5 or '--pass' not in argv or '--fail' not in argv:
        print(__doc__, file=sys.stderr)
        return 2
    tool, rom = argv[1], argv[2]
    i, j = argv.index('--pass'), argv.index('--fail')
    passing, failing = argv[i + 1:j], argv[j + 1:]
    bad = 0

    def expect(paths: list[str], want: int, what: str) -> None:
        nonlocal bad
        code, out = check(tool, rom, paths)
        ok = code == want
        print(f'{"ok" if ok else "FAIL"}: {what}: exit {code}, expected {want}')
        if not ok:
            print(out)
            bad += 1

    expect(passing, 0, 'engine-only programs')
    for f in failing:
        expect([f], 1, os.path.basename(f))

    with open(rom, 'rb') as f:
        data = f.read()
    with tempfile.TemporaryDirectory() as d:
        # A clean archive of the engine passes; 6 KiB of the ROM inside
        # random bytes, in a tar inside a zip, does not.
        clean = os.path.join(d, 'engine.tar.gz')
        with tarfile.open(clean, 'w:gz') as t:
            for p in passing:
                t.add(p, arcname='bin/' + os.path.basename(p))
        expect([clean], 0, 'engine in a .tar.gz')
        # Generic tables that also occur in the ROM (the bit-reversal table,
        # as in the bundled compiler) are not ROM data: a file of them passes.
        generic = os.path.join(d, 'tables.bin')
        rev = bytes(int(f'{i:08b}'[::-1], 2) for i in range(256))
        with open(generic, 'wb') as f:
            f.write(os.urandom(500) + rev * 3 + bytes(range(256)) + os.urandom(500))
        expect([generic], 0, 'generic byte tables (bit reversal, identity)')
        piece = os.urandom(777) + data[0x123457:0x123457 + 6144] + os.urandom(999)
        inner = io.BytesIO()
        with tarfile.open(fileobj=inner, mode='w') as t:
            info = tarfile.TarInfo('share/innocent.dat')
            info.size = len(piece)
            t.addfile(info, io.BytesIO(piece))
        nested = os.path.join(d, 'package.zip')
        with zipfile.ZipFile(nested, 'w') as z:
            z.writestr('README.txt', 'nothing to see\n')
            z.writestr('data/assets.tar', inner.getvalue())
        expect([nested], 1, 'a piece of the ROM in a tar in a zip')
        # Asset packs (docs/ASSET-PACKS.md): their manifest, or one of their
        # key-named pictures, fails a package wherever it is.
        packzip = os.path.join(d, 'with_pack.zip')
        with zipfile.ZipFile(packzip, 'w') as z:
            z.writestr('share/ct-recomp/packs/mine/pack.toml', '[pack]\nversion = 1\n')
        expect([packzip], 1, 'a pack manifest in a zip')
        pictar = os.path.join(d, 'with_tiles.tar')
        with tarfile.open(pictar, 'w') as t:
            info = tarfile.TarInfo('data/sprites/0123456789abcdef.png')
            body = b'\x89PNG\r\n\x1a\n' + os.urandom(64)
            info.size = len(body)
            t.addfile(info, io.BytesIO(body))
        expect([pictar], 1, 'a key-named pack picture in a tar')
        expect([d], 1, 'a directory holding them')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
