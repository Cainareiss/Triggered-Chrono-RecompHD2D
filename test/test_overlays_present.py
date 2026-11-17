#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""The corner overlays are drawn over the plain picture (frontend/overlays.h), headless.

usage: test_overlays_present.py FRONTEND FRAMES

Runs the frontend unpaced twice with a settings folder of its own: once as
it comes, once with the FPS, clock and play time overlays on in settings.ini.
Passes when both runs exit cleanly (the overlay path opens a UI frame over the
picture each frame without failing) and every frame hash of the two runs
matches: the overlays change nothing in the game. Pixels are not checked
(the plain path here is SDL's software renderer; HD-2D needs a GL context).
"""
import os
import subprocess
import sys
import tempfile


def run(frontend, frames, xdg, log):
    env = dict(os.environ, XDG_DATA_HOME=xdg, SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
    return subprocess.run([frontend, "--fast", "--frames", frames, "--hash-log", log],
                          env=env, capture_output=True, text=True, timeout=600)


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    frontend, frames = sys.argv[1], sys.argv[2]
    with tempfile.TemporaryDirectory() as tmp:
        logs = {}
        for name in ("off", "on"):
            xdg = os.path.join(tmp, name)
            folder = os.path.join(xdg, "ct-recomp", "hd2d")
            os.makedirs(folder)
            if name == "on":
                with open(os.path.join(folder, "settings.ini"), "w") as f:
                    f.write("overlay_fps=1\noverlay_clock=1\noverlay_playtime=1\n")
            logs[name] = os.path.join(tmp, name + ".log")
            r = run(frontend, frames, xdg, logs[name])
            if r.returncode:
                print(f"FAIL: {name} run exited {r.returncode}\n{r.stderr[-600:]}")
                return 1
        a, b = (open(logs[n]).read() for n in ("off", "on"))
        if not a or a != b:
            print("FAIL: frame hashes differ with the overlays on" if a else "FAIL: no hashes")
            return 1
    print("overlays_present: ok (clean run, hashes unchanged)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
