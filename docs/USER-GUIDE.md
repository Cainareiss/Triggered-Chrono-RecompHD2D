<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Player's guide

This guide is for playing the port. Developer topics, such as building from source, the
tests and the recompiler, are in the [README](../README.md) and the other files in
`docs/`.

## Legal note

**No game data comes with this program.** The AppImage and the Flatpak hold only the
engine: the runtime, the renderer and the tools that turn a ROM into game code. They
contain no ROM, no game code, no graphics, no music and no text from the game, and none
of those is distributed by this project.

You supply your own copy of the game, dumped from a cartridge you own. On the first
launch, the engine translates the code in that ROM into a "game module" (`ct_game.so`)
**on your computer**. The module is built from your ROM, so it counts as game data: it
stays on your machine, nothing is uploaded anywhere, and you must not share it, just as
you must not share the ROM.

## What you need

- **A Linux PC** with x86-64 (64-bit Intel or AMD), such as a desktop, a Steam Deck or a
  ROG Ally running Linux. Windows and macOS are not supported yet.
- **Your own ROM dump** of the North American (US) 1.0 release of the game, in this
  format:
  - **headerless**, exactly **4,194,304 bytes** (4 MiB)
  - SHA-1 `de5822f4f2f7a55acb8926d4c0eaa63d5d989312` (CRC32 `2D206BF7`)
  - The file extension doesn't matter (`.sfc` and `.smc` are both fine); only the
    contents are checked.

  A file of 4,194,816 bytes has a 512-byte copier header. Remove the header, for example
  with `tail -c +513 dump.smc > dump.sfc`, and use the new file. Other regions,
  revisions, translations and ROM hacks don't match the checksum and won't be accepted.
- **Some free disk space** in your home folder while the module is built: the
  generated C files and objects are written to the data folder (see
  [Where your files live](#where-your-files-live)) and deleted once the module is linked.

The packages bundle everything the module build needs: a C compiler (zig) in both, and
Python in the AppImage. The Flatpak uses the Python in its runtime.

**On a Steam Deck or ROG Ally**, see [HANDHELD.md](HANDHELD.md): a step-by-step guide for
Game Mode, with no keyboard needed after setup.

## Installing

### AppImage

1. Get `CT_Recomp-x86_64.AppImage` from a release, or build it yourself with
   `packaging/appimage/build.sh` (see [PACKAGING.md](PACKAGING.md)).
2. Make it executable, either in your file manager's Properties dialog or with:
   ```
   chmod +x CT_Recomp-x86_64.AppImage
   ```
3. Double-click it, or run `./CT_Recomp-x86_64.AppImage`.

If it complains about FUSE, run it as
`./CT_Recomp-x86_64.AppImage --appimage-extract-and-run`.

The AppImage built with `packaging/appimage/build-container.sh` needs glibc 2.35 or
newer (Ubuntu 22.04, Debian 12, Fedora, Arch, SteamOS 3 and Bazzite all qualify). An
image built directly on a very new distribution needs that distribution's glibc, so
use a release image or the container script. The module is built on your machine, so
it needs about 1 GB of free space and a minute or two the first time.

### Flatpak

The Flatpak isn't on Flathub. Build and install it from the source tree as described in
[PACKAGING.md](PACKAGING.md), or, if you were given a `.flatpak` bundle file:

```
flatpak install --user ct-recomp.flatpak
flatpak run io.github.jerbanpls.CtRecomp
```

After that, it also appears in your desktop's application menu as **CT Recomp**.

The Flatpak can read only your **Downloads** and **Documents** folders, so keep the ROM in
one of those. To use another folder, grant read-only access first:

```
flatpak override --user --filesystem=/path/to/roms:ro io.github.jerbanpls.CtRecomp
```

### Options at launch

Engine options go after the program, for example `--hd2d` to turn on the HD-2D look
(see [The HD-2D look](#the-hd-2d-look)):

```
./CT_Recomp-x86_64.AppImage --hd2d
flatpak run io.github.jerbanpls.CtRecomp --hd2d
```

| Option | What it does |
| --- | --- |
| `--help` | Print the options and the keyboard controls, then quit |
| `--rom FILE` | Use this ROM file |
| `--hd2d` | Draw with the HD-2D renderer (on by default in Steam's Game Mode) |
| `--no-hd2d` | The original look, also in Game Mode |
| `--fullscreen` | Start fullscreen |
| `--scale N` | Window size, as a multiple of the console's 256×224 picture (default 3). Without it, the size you last left the window at is used |
| `--interp-only` | Don't build or load the game module; run everything in the interpreter |
| `--preset off\|subtle\|hd2d\|deck` | Starting HD-2D look (with `--hd2d`); `deck` is a lighter HD-2D, the default on a Steam Deck |
| `--aspect 4:3\|pixel\|stretch` | Picture shape (with `--hd2d`) |
| `--log-gpu` | Print the HD-2D renderer's frame time every 60 frames |
| `--pacing auto\|timer\|vsync` | How frames are paced: the engine's 60.0988 Hz timer (`timer`, and `auto`, the default), or the display's vsync on a 60 Hz screen (`vsync`, see [HANDHELD.md](HANDHELD.md#looks-frame-rate-and-battery)) |
| `--log-pacing` | Print frame-time statistics every 60 frames (every exit prints them for the whole run) |
| `--save FILE` | Keep the game's save memory in FILE instead of `ct/ct.srm` (see [Your saves](#your-saves)) |
| `--no-save` | Don't load or write the save file: in-game saves last only until you quit |

## First launch

1. **Choose the ROM.** The engine looks for a ROM in this order:
   1. `--rom FILE`
   2. the `CT_ROM` environment variable
   3. the ROM that worked last time
   4. a `.sfc` or `.smc` file of the right size and checksum in `~/Documents`,
      `~/Downloads`, the AppImage's folder or the data folder (up to two subfolders
      deep, at most about 2000 files)

   If none of those works, the window says where to put the ROM, with its size and
   SHA-1. Copy the file there and press **A** (or Enter) to search again, or **drop the
   file on the window** from your file manager. When a file is rejected, the screen says
   why (see [Troubleshooting](#troubleshooting)).
2. **The module build.** The first time, and again whenever the ROM or the engine
   changes in a way that needs it, the engine builds the game module. A progress screen
   lists each step as it runs, with a seconds counter at the bottom:
   1. checking the ROM
   2. decoding overlays
   3. translating to C
   4. compiling
   5. linking
   6. done (the generated C has been deleted by then)

   This takes about a minute (around 40 seconds on a ROG Ally), and happens once.
   **Ctrl+Q** (in Game Mode, the Steam button) stops the build and quits.
3. **Play.** The screen shows "Starting..." and the game begins. On later launches, the
   remembered ROM and the existing module are reused, so the game starts right away.

## Controls

### Keyboard

| SNES | Key |
| --- | --- |
| D-pad | Arrow keys |
| B | Z |
| A | X |
| Y | A |
| X | S |
| L | Q |
| R | W |
| Start | Enter |
| Select | Right Shift |

**F11** switches fullscreen on and off. The window remembers its size, place and
fullscreen state between runs (in `~/.local/share/ct-recomp/hd2d/settings.ini`, or under
`~/.var/app/io.github.jerbanpls.CtRecomp/data/` in the Flatpak, next to the saved look
and picture shape). A saved place is ignored if no screen shows it any more, and on
Wayland only the size and fullscreen state are kept, as the desktop picks the place.
`--scale` and Steam's Game Mode (always fullscreen) override it. `--help` lists the keys.

**Ctrl+Q**, or closing the window, quits. On a controller, hold **View + Menu** (Back +
Start) for 2 seconds: the game asks whether to quit, and **A** quits, **B** goes back. Esc does not quit: Steam Input sends Esc for a
controller button in some layouts.

### Controller

Any controller SDL recognizes as a game controller (Xbox, PlayStation, Switch Pro, Steam
Deck, ROG Ally and most others) works as pad 1. Buttons are mapped by position:

| SNES | Controller |
| --- | --- |
| B | bottom face button (Xbox A) |
| A | right face button (Xbox B) |
| Y | left face button (Xbox X) |
| X | top face button (Xbox Y) |
| L / R | left / right shoulder buttons |
| Start | Start / Menu |
| Select | Back / View / Share |
| D-pad | D-pad or left stick |

You can plug a controller in or out while the game runs.

### The HD-2D look

Start with `--hd2d` to draw the game as a lit diorama, with depth of field, bloom, soft
shadows and a color grade. Every pixel still comes from the game's own picture: the
renderer adds lighting and blur, not new art. More detail is in [HD2D.md](HD2D.md).

| Key | Controller | What it does |
| --- | --- | --- |
| F1 | R3 (click the right stick) | Cycle the look: original, subtle, HD-2D (the default), HD-2D Steam Deck |
| F2 | | Cycle the debug views: layers, depth, blur, side-by-side compare, off |
| F3 | L3 (click the left stick) | Cycle the picture shape: 4:3 (default), pixel 8:7, stretch |
| F11 | | Toggle fullscreen |

F11 works without `--hd2d` too. F1–F3, R3 and L3 only do something with `--hd2d`. Each
change shows a short message on screen, and the look and the picture shape are
remembered for next time.

### Two players (co-op)

A second controller is player 2. As soon as two controllers are connected, co-op is
on (**F4** turns it on or off; `--coop` and `--no-coop` at launch). The choice is saved in
`settings.ini` as `coop=auto`, `on` or `off`. Player 1 keeps the keyboard and the first
controller.

- **Battle:** when the command menu of party member 2 or 3 opens, player 2's controller
  picks the command and the target. Player 1's buttons do nothing until it closes, and
  a button player 2 is already holding when the menu opens counts only once it is let
  go. `coop_chars=2,3` in `settings.ini` lists the party members (1-3) player 2 plays.
  With a third controller, `coop_chars3=3` gives party member 3 to player 3 (pad 3).
  If a controller is unplugged, its characters go back to player 1 at once, and the
  battle doesn't wait.
- **Field:** player 2 walks party member 2 with the d-pad (hold **B** to run). Walls stop
  them. When player 2 lets go, the character walks back into line behind the leader
  and follows as usual. It snaps back into line if it gets stuck, wanders more than a
  screen away, or when a cutscene starts. After a battle it rejoins the same way, and
  after a door or map change it starts where the game puts it. Only the leader opens
  doors, takes exits, talks to people, and starts events and battles.
- **Split screen (experimental):** with `--coop-split`, when player 2's character is more than
  about 3/4 of a screen from the leader, the window splits in two. On a wide window the
  views sit side by side; otherwise one is above the other. The second view follows
  player 2's character, and the views merge again when the two are close. It needs the
  plain picture (`--no-hd2d`), and the leash is two screens. People near player 2 are
  drawn too, but this changes how the game handles its sprites. A busy map can run out
  of the game's sprite slots, so some people may not show in either view. The
  background in the second view has no parallax.

Co-op needs the game module (the normal setup). With `--interp-only` the game plays as
one player.

## Where your files live

The engine keeps its files in your user data folder, `~/.local/share/ct-recomp/` (or
`$XDG_DATA_HOME/ct-recomp/` if you set `XDG_DATA_HOME`). In the Flatpak, that folder is
`~/.var/app/io.github.jerbanpls.CtRecomp/data/ct-recomp/`.

| File | What it is |
| --- | --- |
| `ct/ct_game.so` | The game module built from your ROM. Don't share it. |
| `ct/ct_game.stamp` | The ROM checksum and engine version the module was built for |
| `ct/rom_path` | Where your ROM is, so you don't have to pick it again |
| `ct/ct.srm` | Your in-game saves (see [Your saves](#your-saves)) |
| `ct/ct.srm.bak` | The save file as it was before the last write |
| `hd2d/settings.ini` | The HD-2D look and picture shape, the window, co-op |
| `ct/gen/`, `ct/obj/` | Temporary build files, normally deleted after the build. If a build fails they can be left behind: they hold code generated from your ROM, so don't share them. Deleting them is safe. |

The ROM itself is never copied: only its location is saved.

### Your saves

The cartridge's battery-backed save memory (8 KiB, which holds the game's save slots) is
kept in `ct/ct.srm`, as a plain 8,192-byte copy of that memory. It is loaded at start-up
and written back a few seconds after the game changes it, for example right after you
save in the game, and again when you quit, also when you close the window or the
program stops with an error. Each write goes to a temporary file first and then replaces
`ct.srm`, so a crash or a power cut leaves either the old save or the new one, never half
of each. The save before the last write is kept as `ct.srm.bak`. To go back to it, quit
and copy `ct.srm.bak` over `ct.srm`.

A `ct.srm` that isn't exactly 8,192 bytes is never used or overwritten: the engine
renames it to `ct.srm.bad`, prints a warning, and starts with empty save memory.

To keep the saves somewhere else, start with `--save /path/to/file.srm`. Save states and
importing saves from other emulators are planned (see [ROADMAP.md](../ROADMAP.md)).

## Troubleshooting

Messages in the window are kept short. For details, start the program from a terminal
and read its output. The program's lines start with `ct_sdl:`, and the `ct_sdl: exit:`
line at the end gives the reason it stopped.

### "... is not the expected ROM" or "... is N bytes, expected 4194304"

The file isn't the dump the port needs (see [What you need](#what-you-need)).
- **"It has a 512-byte copier header: remove it."** Remove the first 512 bytes, as shown
  above, and drop the new file on the window.
- **A different size, or a different SHA-1:** it's another region, revision or a modified
  ROM. You need the unmodified US 1.0 dump.
- **"Not a file" or "Cannot read":** check the path. In the Flatpak, check that the file
  is in Downloads or Documents, or grant access with `flatpak override`.

### "The game code could not be built."

The window shows the last lines of the build's output. Press **A** (or Enter) to try the build
again, or **B** (or any other key) to **play anyway in the interpreter**: the game runs without the module, with every instruction
interpreted. It's slower: on a ROG Ally it measured about 290 frames per second without
a window, against the 60 the game needs, so it's still playable on most PCs. The build is tried again on the next launch.

Common causes:
- **Out of disk space:** free some space in your home folder.
- **"cannot start the build tool (is python3 installed?)":** the package's Python is
  missing. Reinstall the AppImage or the Flatpak.
- **Something else:** run the program from a terminal and keep its output for a bug
  report.

"The game code would not load." means a module was built but couldn't be loaded. (A
module left over from an earlier run is rebuilt once before this message.) Again, any
A tries again, and B or any other key plays in the interpreter.

### Rebuilding the module

The engine rebuilds the module by itself when your ROM or the engine's module format
changes. To force a rebuild, for example after an update that seems not to have taken
effect, quit and delete the stamp file (or the module itself):

```
rm ~/.local/share/ct-recomp/ct/ct_game.stamp
# Flatpak:
rm ~/.var/app/io.github.jerbanpls.CtRecomp/data/ct-recomp/ct/ct_game.stamp
```

The next launch builds it again. To use a different ROM file, start once with
`--rom /path/to/file`, or delete `ct/rom_path` to be asked again.

### Graphics and drivers

The HD-2D look needs **OpenGL 3.3**, or OpenGL ES 3.0. If it isn't available, the
terminal shows `no usable OpenGL 3.3 / ES 3.0 context` and the program exits. Start
without `--hd2d` to play with the original look, which works on any graphics driver.

**Steam Deck, ROG Ally and other handhelds.** Add the AppImage to Steam as a non-Steam
game; for the Flatpak, add it with Steam's "Add a Non-Steam Game" and pick
**CT Recomp**. In Game Mode HD-2D is on by default; for the original look, add
`--no-hd2d` to the shortcut's launch options (for the Flatpak shortcut, at the end of
the existing launch options).
- In Game Mode (gamescope) the game starts fullscreen, with HD-2D on and no mouse
  cursor. The full guide is [HANDHELD.md](HANDHELD.md).
- Use R3 and L3 for the look and the picture shape, since handhelds lack F-keys.
- Use a Steam Input layout that sends gamepad buttons, not a keyboard or desktop layout.
- The renderer takes about 2 ms per frame on a ROG Ally at roughly 1080p's pixel count,
  so a lower TDP is usually fine. `--log-gpu` prints the frame time if you want to check.

**NVIDIA (proprietary driver).**
- Flatpak: the GL driver comes from the `org.freedesktop.Platform.GL.nvidia-*` extension,
  which must match your installed driver version. After a driver update, run
  `flatpak update` so they match again. A mismatch shows up as the OpenGL error above.
- If the window doesn't open on a Wayland desktop, try X11 through XWayland:
  `SDL_VIDEODRIVER=x11 ./CT_Recomp-x86_64.AppImage --hd2d` (for the Flatpak,
  `flatpak run --env=SDL_VIDEODRIVER=x11 io.github.jerbanpls.CtRecomp --hd2d`).

- The HD-2D code was checked against a strict GL 3.3 core profile on Mesa
  (radeonsi and llvmpipe, with every GL error reported), but never on NVIDIA's driver.
  If you see a black or garbled picture there, run with `CT_HD2D_GLERR=1` set and
  send the terminal's `hd2d: GL error` lines.

**AMD and Intel (Mesa).** Use an up-to-date Mesa from your distribution. Old Intel GPUs
without OpenGL 3.3 can still play without `--hd2d`.

### No sound, or no controller

- **Sound:** the program uses your desktop's sound server (PulseAudio or PipeWire). Check
  that the output device is right in your system's sound settings.
- **Controller:** run from a terminal. At startup the program lists every joystick it
  sees, and marks the ones it can't use as `(joystick only: not used)`. Only
  controllers that SDL recognizes as game controllers work.
