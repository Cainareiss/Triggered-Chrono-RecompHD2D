#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""The in-game menu (frontend/menu.h), headless.

usage: test_menu.py FRONTEND FRAMES OPEN_AT [--hd2d]

Runs the frontend unpaced with a settings folder of its own, twice:
- without the menu, writing per-frame hashes (--hash-log);
- with a --menu-test script that opens the menu with the menu action at
  frame OPEN_AT, moves with the d-pad to the Look row and changes it,
  goes to the Audio tab with R1 and turns the volume down, then closes
  the menu with B.
Passes when the second run's settings.ini has the new look and volume,
the frame count did not move while the menu was open ("0 frames ran
while open", "frame count N (was N)"), and the two runs' hashes match on
every frame: the menu changes nothing in the game.

--hd2d: the same through the HD-2D renderer (needs an OpenGL context:
SDL's offscreen driver); skipped (exit 0) when there is none.
"""
import os
import re
import subprocess
import sys
import tempfile


def run(frontend, args, xdg, env_extra):
    env = dict(os.environ, XDG_DATA_HOME=xdg, SDL_AUDIODRIVER="dummy", **env_extra)
    return subprocess.run([frontend, "--fast", "--no-save"] + args, env=env,
                          capture_output=True, text=True, timeout=600)


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    frontend, frames, open_at = sys.argv[1], sys.argv[2], int(sys.argv[3])
    hd2d = "--hd2d" in sys.argv[4:]
    extra = ["--hd2d"] if hd2d else []
    video = {"SDL_VIDEODRIVER": "offscreen" if hd2d else "dummy"}
    with tempfile.TemporaryDirectory() as tmp:
        logs = {}
        for name in ("plain", "menu"):
            xdg = os.path.join(tmp, name)
            os.makedirs(os.path.join(xdg, "ct-recomp", "hd2d"))
            logs[name] = os.path.join(tmp, name + ".log")
            args = ["--frames", frames, "--hash-log", logs[name]] + extra
            if name == "menu":
                script = os.path.join(tmp, "menu.txt")
                with open(script, "w") as f:
                    f.write("\n".join([
                        f"open {open_at}",
                        "wait 3",
                        "button dpdown",          # Renderer -> Look
                        "button dpright",         # HD-2D -> HD-2D Steam Deck
                        "button rightshoulder",   # Look -> Display -> ... Audio
                        "button rightshoulder",
                        "button rightshoulder",
                        "button rightshoulder",
                        "button rightshoulder",
                        "button rightshoulder",
                        "button dpleft",          # volume 100 -> 95
                        "wait 2",
                        "button b",               # closes the menu
                    ]) + "\n")
                args += ["--menu-test", script]
            r = run(frontend, args, xdg, video)
            if hd2d and "no usable OpenGL" in r.stderr:
                print("test_menu: no OpenGL context here: skipped")
                return 0
            if r.returncode:
                print(f"test_menu: {name} run exit {r.returncode}: {r.stderr.strip()[-600:]}")
                return 1
            if name == "menu":
                err = r.stderr
                ini = open(os.path.join(xdg, "ct-recomp", "hd2d", "settings.ini")).read()
        if "frame %d: menu open" % open_at not in err:
            print("test_menu: the menu did not open:\n" + err[-800:])
            return 1
        m = re.search(r"menu closed after (\d+) ticks; frame count (\d+) \(was (\d+)\)", err)
        if not m or m.group(2) != m.group(3) or int(m.group(1)) < 10:
            print("test_menu: the frame count moved while the menu was open:\n" + err[-800:])
            return 1
        if "0 frames ran while open" not in err:
            print("test_menu: frames ran while the menu was open")
            return 1
        for want in ("preset=deck", "volume=95"):
            if want not in ini.split("\n"):
                print(f"test_menu: settings.ini has no {want}:\n{ini}")
                return 1
        a = open(logs["plain"]).read().splitlines()
        b = open(logs["menu"]).read().splitlines()
    if len(a) != len(b) or len(a) < int(frames) - 1:
        print(f"test_menu: {len(a)} frames hashed without the menu, {len(b)} with it")
        return 1
    for x, y in zip(a, b):
        if x != y:
            print(f"test_menu: frame {x.split()[0]} differs with the menu: {x} vs {y}")
            return 1
    print(f"test_menu: menu open {m.group(1)} ticks at frame {open_at}, settings saved, "
          f"{len(a)} frame hashes unchanged{' (HD-2D)' if hd2d else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
