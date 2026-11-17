#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""The F1 quick menu (frontend/quickmenu.h) in the real frontend, headless.

usage: test_quickmenu.py FRONTEND FRAMES OPEN_AT

Runs the frontend unpaced on the plain (SDL_Renderer) path with a settings
folder of its own, three times:
- without the menu, writing per-frame hashes (--hash-log);
- "pad": a --menu-test script opens the quick menu with its action at frame
  OPEN_AT, moves the ring with the d-pad (Save to slot, the Volume slider),
  saves slot 1, lowers the volume, takes a picture of the sheet (--menu-test
  shot) and closes it with B;
- "touch": no script opens it: a --touch-test script taps three fingers at
  OPEN_AT, a second time taps the handle (after a touch showed it), and
  the --menu-test script taps above the sheet to close it, the second time
  presses the keyboard's Escape.
Passes when the pad run's settings.ini has volume=95, slot 1 has a state,
the picture is a PNG, the touch run opened it twice and closed it, the
frame count did not move while it was open in either, and every frame hash
matches the run without it: the quick menu changes nothing in the game.
"""
import os
import re
import subprocess
import sys
import tempfile


def run(frontend, args, xdg):
    env = dict(os.environ, XDG_DATA_HOME=xdg, SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
    return subprocess.run([frontend, "--fast"] + args, env=env,
                          capture_output=True, text=True, timeout=600)


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    frontend, frames, open_at = sys.argv[1], sys.argv[2], int(sys.argv[3])
    with tempfile.TemporaryDirectory() as tmp:
        shot = os.path.join(tmp, "quick.png")
        scripts = {
            "pad": [f"quick {open_at}", "wait 14", f"shot {shot}", "wait 2",
                    "button dpdown",            # Resume -> Slot
                    "button dpright",           # -> Save to slot
                    "button a",                 # slot 1 saved
                    "button dpdown",            # -> Volume
                    "button dpleft",            # 100 -> 95
                    "button rightshoulder",     # the next section
                    "button leftshoulder",
                    "wait 2",
                    "button b"],                # closes
            "touch": ["wait 14",
                      "touch 500 20",           # above the sheet: closes
                      "wait 3",                 # second opening, by the handle
                      "key Escape"],
        }
        touch_script = os.path.join(tmp, "fingers.txt")
        with open(touch_script, "w") as f:
            f.write("\n".join([
                f"{open_at} down 0.4,0.5 0", f"{open_at} down 0.5,0.5 1", f"{open_at} down 0.6,0.5 2",
                f"{open_at + 4} up 0", f"{open_at + 4} up 1", f"{open_at + 4} up 2",
                f"{open_at + 60} down 0.2,0.5 3", f"{open_at + 62} up 3",   # shows the overlay
                f"{open_at + 70} down 0.5,0.02 4", f"{open_at + 72} up 4",  # the handle
            ]) + "\n")
        logs, errs, inis = {}, {}, {}
        for name in ("plain", "pad", "touch"):
            xdg = os.path.join(tmp, name)
            os.makedirs(os.path.join(xdg, "ct-recomp", "hd2d"))
            logs[name] = os.path.join(tmp, name + ".log")
            args = ["--frames", frames, "--hash-log", logs[name]]
            if name != "plain":
                path = os.path.join(tmp, name + ".txt")
                with open(path, "w") as f:
                    f.write("\n".join(scripts[name]) + "\n")
                args += ["--menu-test", path]
            if name == "touch":
                args += ["--touch-test", touch_script]
            r = run(frontend, args, xdg)
            if r.returncode:
                print(f"test_quickmenu: {name} run exit {r.returncode}: {r.stderr.strip()[-600:]}")
                return 1
            errs[name] = r.stderr
            if name == "pad":
                inis[name] = open(os.path.join(xdg, "ct-recomp", "hd2d", "settings.ini")).read()
        err = errs["pad"]
        if f"frame {open_at}: quick menu open" not in err:
            print("test_quickmenu: the quick menu did not open:\n" + err[-800:])
            return 1
        if "volume=95" not in inis["pad"].split("\n"):
            print(f"test_quickmenu: settings.ini has no volume=95:\n{inis['pad']}")
            return 1
        found = [f for d, _, fs in os.walk(os.path.join(tmp, "pad")) for f in fs
                 if f in ("slot1.state", "slot1.png")]
        if len(found) != 2:
            print(f"test_quickmenu: slot 1 not saved from the quick menu: {found}\n" + err[-800:])
            return 1
        if not os.path.exists(shot) or open(shot, "rb").read(8) != b"\x89PNG\r\n\x1a\n":
            print("test_quickmenu: no picture of the sheet:\n" + err[-800:])
            return 1
        for name in ("pad", "touch"):
            e = errs[name]
            n = len(re.findall(r"menu closed after \d+ ticks; frame count (\d+) \(was \1\)", e))
            want = 1 if name == "pad" else 2
            if n != want:
                print(f"test_quickmenu: {name}: {n} openings kept the frame count, wanted {want}:\n"
                      + e[-1200:])
                return 1
            if "0 frames ran while open" not in e:
                print(f"test_quickmenu: {name}: frames ran while it was open")
                return 1
        a = open(logs["plain"]).read().splitlines()
        for name in ("pad", "touch"):
            b = open(logs[name]).read().splitlines()
            if len(a) != len(b) or len(a) < int(frames) - 1:
                print(f"test_quickmenu: {len(a)} frames hashed without the menu, {len(b)} with it ({name})")
                return 1
            for x, y in zip(a, b):
                if x != y:
                    print(f"test_quickmenu: {name}: frame {x.split()[0]} differs: {x} vs {y}")
                    return 1
    print(f"test_quickmenu: quick menu opened by the action, three fingers and the handle; "
          f"volume and slot 1 saved, {len(a)} frame hashes unchanged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
