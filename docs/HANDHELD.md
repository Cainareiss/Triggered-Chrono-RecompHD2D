<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Steam Deck and ROG Ally

This page walks through playing the port on a **Steam Deck** or an **ASUS ROG Ally**
running **Bazzite** (or SteamOS on the Deck), started from Steam's **Game Mode**. You
need no keyboard after setup. Everything else, such as the legal note and the full
option list, is in the [player's guide](USER-GUIDE.md).

## 1. Get the AppImage

In **Desktop Mode**, either:

- download `CT_Recomp-x86_64.AppImage` from a release into `~/Applications` (create the
  folder if needed), or
- build it from a checkout with `packaging/appimage/build.sh` (see
  [PACKAGING.md](PACKAGING.md)). The result is `build-appimage/CT_Recomp-x86_64.AppImage`.
  Copy it to `~/Applications`.

Then make it executable, in Konsole:

```
chmod +x ~/Applications/CT_Recomp-x86_64.AppImage
```

## 2. Put your ROM in ~/Documents

Copy your own dump of the US 1.0 cartridge to `~/Documents` (a subfolder such as
`~/Documents/roms` works too). The engine also looks in `~/Downloads`, in the folder the
AppImage is in, and in its own data folder, `~/.local/share/ct-recomp/ct/`.

The file must be **headerless**, exactly **4,194,304 bytes**, with SHA-1
`de5822f4f2f7a55acb8926d4c0eaa63d5d989312`. The name doesn't matter, but it must end in
`.sfc` or `.smc` for the search to find it. A file of 4,194,816 bytes has a copier header:
the first-run screen says so. Remove the header with
`tail -c +513 dump.smc > dump.sfc`.

The search goes two folders deep and stops after about 2000 files, so don't bury the
ROM deep in a big folder.

## 3. Add it to Steam

Run the helper. It prints the exact steps with your paths filled in, and changes nothing
itself:

```
packaging/steam/add-to-steam.sh ~/Applications/CT_Recomp-x86_64.AppImage
```

(Without a checkout, the steps are these.)

1. Steam (Desktop Mode) > **Games** > **Add a Non-Steam Game to My Library...** >
   **Browse...**. Pick the AppImage (choose "All Files" as the file type if it isn't
   listed), then **Add Selected Programs**.
2. Right-click the new entry > **Properties...**. Name it `CT Recomp`. Leave the launch
   options empty: in Game Mode the HD-2D look is on by default.
3. **Properties** > **Controller**: choose the **Gamepad** layout. A keyboard or desktop
   layout sends keys rather than buttons.
4. Optional artwork: `packaging/steam/artwork/` has a capsule (`grid.png`, 600x900), wide
   capsule (`capsule_wide.png`, 920x430), hero (`hero.png`, 1920x620), transparent logo
   (`logo.png`, 1280x720) and icon (`icon.png`, 256x256). Right-click the entry >
   **Manage** > **Set custom artwork** and pick each file in its slot. The helper prints
   the paths. The art is original to this project (GPL-3.0-or-later, made by
   `packaging/icons/make_icons.py --steam`); it uses no game imagery.

Switch back to Game Mode. The game is in your library under **Non-Steam**.

## 4. First launch

1. The engine finds the ROM in the folders above and checks it.
2. It **builds the game code from your ROM on the device**. A progress screen lists the
   steps with a seconds counter. This takes **about 40 seconds on a ROG Ally** and longer
   on a Steam Deck (its CPU is slower, so expect around one to two minutes). It happens
   once; later launches start right away.
3. The game starts.

If no ROM is found, the screen says where to put it and which file is needed (size and
SHA-1). Copy the ROM over, for example from Desktop Mode or over the network, then press
**A** to search again. The search also repeats every few seconds by itself.

If the build fails, the screen shows the last lines of its output. Press **A** to try
again, or **B** to play without the built code, in the interpreter. That is slower but
still full speed on the Ally; on the Deck it hasn't been measured.

## Controls

The built-in controls are read as an Xbox-style controller through Steam Input. Buttons
are mapped by position, as on the SNES pad:

| SNES | Steam Deck / ROG Ally |
| --- | --- |
| B | A (bottom) |
| A | B (right) |
| Y | X (left) |
| X | Y (top) |
| L / R | L1 / R1 |
| Start | Menu (☰) |
| Select | View (⧉) |
| D-pad | D-pad or left stick |

| Combo | What it does |
| --- | --- |
| R3 (click the right stick) | Cycle the look: original, subtle, HD-2D, HD-2D Steam Deck |
| L3 (click the left stick) | Cycle the picture shape: 4:3, pixel 8:7, stretch |
| Hold View + Menu for 2 s | Asks "Quit?": **A** quits, **B** goes back to the game |
| Steam button > Exit Game | Quits too |

While you hold View and Menu together, the game doesn't see them, so the combo doesn't
open the game's menu. The answer to the question doesn't reach the game either.

### Two players on one handheld

Connect a second controller (USB or Bluetooth) and co-op turns on: the built-in controls
are player 1, the second controller is player 2, and it plays party members 2 and 3 in
battle and walks party member 2 in the field ([USER-GUIDE.md](USER-GUIDE.md#two-players-co-op)).

Under Steam's Game Mode both controllers reach the game through Steam Input, as two
virtual Xbox controllers. Steam numbers them in the order they became active. The game
takes the first one it sees as player 1, and that is the built-in controls once you have pressed something on
them. If the order comes out wrong, reorder the controllers in Steam's controller settings,
or unplug the second one and plug it back in.
Give the second controller a gamepad layout in Steam, not a desktop one. A desktop
layout sends keyboard keys, and those reach player 1.

On the Ally, Armoury Crate's controller mode and Steam both have to treat the second pad
as a controller. A pad that shows up only as a joystick is listed as "joystick only: not
used" at startup (run the AppImage from a terminal to see the list). With no keyboard,
co-op can only be turned off by disconnecting the second controller, or by binding F4 to a
back button in Steam Input. The choice is saved.

None of this has been tried on a real Deck or Ally yet. It was tested with scripted
pad 2 input (the `coop_*` ctests).

## Looks, frame rate and battery

In Game Mode the engine starts fullscreen at the screen's size, with no mouse cursor and
the HD-2D look on. Add `--no-hd2d` to the launch options for the original look.

- **Steam Deck:** with no look saved yet, the engine picks the **deck** preset. It is the
  HD-2D look with a shorter depth-of-field blur and a single bloom level. On the 16:10
  screen the narrow side bars are plain black rather than a blurred fill. The Deck is
  recognized by its GPU (`VANGOGH` or `Custom GPU 0405` in the OpenGL renderer name) or
  its firmware product name (Jupiter or Galileo).
- **ROG Ally:** the full HD-2D look. The renderer takes about 2 ms per frame there.

Once you change the look with R3, your choice is saved and used from then on, on any
device.

**Frame pacing.** The console runs at 60.0988 frames per second. The engine paces itself
with a precise timer, sleeping to each frame's time, and the sound's rate control keeps
the audio in step. Under gamescope the newest frame is shown at each refresh, so how
smooth it looks depends on the refresh rate:
- **Deck OLED (90 Hz):** set the refresh rate to **60 Hz** in the Performance menu (the
  **...** button). At 90 Hz, frames alternate between one and two refreshes, which shows
  as judder.
- **Deck LCD:** already 60 Hz. About every 10 seconds one frame is never shown, since
  the console's 60.0988 Hz is a little faster than the panel's 60 Hz. `--pacing vsync`
  avoids that.
- **ROG Ally:** at 120 Hz with VRR the frames are shown as they come.
- **Frame limiter:** not needed. If you set one, set it to 60.
- **`--pacing vsync`:** paces by the display's vertical sync instead, for 60 Hz
  screens. The sound's rate control absorbs the 0.16% difference. When frames run late
  and the sound queue runs low, vsync is switched off until the queue refills, so the
  game briefly runs a little fast instead of the sound cutting out. It isn't the default
  yet: it hasn't been measured in a real Game Mode session.
- **`--log-pacing`:** prints frame-time statistics when started from a terminal, and
  every exit prints them for the whole run.

## Saves and where files live

Everything is in `~/.local/share/ct-recomp/`:

| File | What it is |
| --- | --- |
| `ct/ct_game.so` | The game code built from your ROM. Don't share it. |
| `ct/ct_game.stamp` | What it was built from |
| `ct/rom_path` | Where your ROM is |
| `ct/ct.srm`, `ct/ct.srm.bak` | Your in-game saves, and the copy before the last write |
| `hd2d/settings.ini` | The look and the picture shape |

Saves are written a few seconds after you save in the game, and again on quit,
including through the Steam button. To back them up, copy `ct/ct.srm`. More detail is in
the [player's guide](USER-GUIDE.md#your-saves).

## Troubleshooting

- **It doesn't start from Game Mode.** Run the AppImage once in Desktop Mode from Konsole
  (`~/Applications/CT_Recomp-x86_64.AppImage`) and read the `ct_sdl:` lines. If it
  complains about FUSE, add `--appimage-extract-and-run` as the first launch option.
- **"No ROM found"** although the file is there. Check the size (4,194,304 bytes), the
  extension (`.sfc` or `.smc`) and the depth (at most two folders below `~/Documents`).
  The terminal shows `ct_sdl: search:` lines for each file it rejected and why.
- **Buttons do the wrong thing, or two things.** Set the controller layout to
  **Gamepad** in the game's Steam properties. A desktop layout sends Enter and Esc along
  with A and B.
- **Judder on a Deck OLED:** set the screen to 60 Hz (see above).
- **Crackling sound.** Run from Konsole with `--log-pacing`. Each exit prints
  `ct_sdl: audio: N underruns` and the frame-time statistics. Under heavy load from other
  programs, frames run late and the sound queue empties.
- **Start over:** delete `~/.local/share/ct-recomp/ct/ct_game.stamp` to rebuild the game
  code, or `ct/rom_path` to search for the ROM again. Your saves are left alone.

## Not yet tested on real hardware

These parts were tested on a ROG Ally running Bazzite, but only headless (SDL's
offscreen driver, an SDL virtual controller) or under
`gamescope --backend headless -W 1280 -H 800 -r 60`, never from Steam's real Game Mode:
- the fullscreen size
- the vsync pacing, which held 60.000 Hz under headless gamescope
- the Steam Input layout and the quit combo on the real buttons

The Steam Deck paths (the deck preset's speed, GPU detection on real Deck drivers, build
time) haven't been run on a Deck at all. Reports are welcome.
