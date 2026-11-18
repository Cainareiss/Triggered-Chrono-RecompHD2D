<!--
SPDX-FileCopyrightText: 2026 ct-recomp contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# PlayStation 5 (homebrew)

The port runs natively on a jailbroken PS5 as an ELF payload. It is built with the
open-source [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk), not Sony's SDK.
There's no PKG and no Linux layer: the payload talks to the console's own video, audio
and pad libraries through the [ps5-payload-dev SDL2 fork](https://github.com/ps5-payload-dev/SDL).

> **Status: experimental.** It cross-compiles and links against the console's system
> libraries, but nobody has run it on hardware yet. Please report what happens. The log is
> described below.

## What works and what doesn't

| | |
| --- | --- |
| Original renderer (the SNES picture, scaled) | yes: SDL's software renderer to the TV, fullscreen |
| Audio, DualSense, saves, save states, in-game menu | yes, through SDL (DualSense uses its PS5 mapping) |
| Game code | compiled in (`ct_sdl.elf`), or a module (`ct_sdl_engine.elf` + `ct_game.so`), or the interpreter |
| HD-2D renderer, asset packs' art, HD text | **no** by default: these need OpenGL 3.3, and the PS5 has no GL driver. SDL loads `libOSMesa.so.8` if one is present, but that's software GL and too slow for HD-2D's passes. If the GL context fails, the port falls back to the original renderer. |
| First-run setup (finding the ROM, building the module) | no Python or compiler on the console: build on your PC (see below) |

## You need

- A PS5 whose firmware has a public kernel exploit that starts an ELF loader. For
  example, 13.20 is covered by Relapse (7.00 to 13.60). Don't update the console.
- A Linux PC, or WSL, with `clang` 15 or later, `lld`, CMake 3.30 or later, Python 3.11+
  and `git`.
- Your own headerless Chrono Trigger (USA) 1.0 dump.

## Build

```sh
# 1. The SDK (v0.45 was used here).
wget https://github.com/ps5-payload-dev/sdk/releases/download/v0.45/ps5-payload-sdk.zip
sudo unzip ps5-payload-sdk.zip -d /opt
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk

# 2. SDL2 (PS5 fork) and libsamplerate into the SDK. Do this once.
packaging/ps5/build-deps.sh

# 3a. Recommended: the game compiled in, from your ROM.
CT_ROM=rom/your_dump.sfc packaging/ps5/build.sh
#   -> build-ps5/ct_sdl.stripped.elf   (for your console only)

# 3b. Or the engine only (no game code, so it can be shared):
packaging/ps5/build.sh
#   -> build-ps5/ct_sdl_engine.stripped.elf
#   Its game module, built from your ROM with the PS5 compiler:
python3 tools/build_module.py --game game/ct --rom rom/your_dump.sfc \
    --out build-ps5 --cc "$PS5_PAYLOAD_SDK/bin/prospero-clang"
#   -> build-ps5/ct_game.so
```

`ct_sdl.elf` is the safest choice, because it's one self-contained payload.
`ct_sdl_engine.elf` loads `ct_game.so` with `dlopen`. The SDK supports that, but loading
a module that binds back to the program's own symbols hasn't been tried on a console yet.
Without the module, the engine runs the whole game in the interpreter. That's slower,
but the PS5's Zen 2 CPU should keep up.

As with every build of this port, a file built with `CT_ROM` set holds code made from your
ROM. Keep it on your own console and don't share it (docs/MODULES.md).

## Copy the files to the console

The payload looks for its files in these folders, in this order:

```
/mnt/usb0/ct-recomp/    a USB drive (exFAT or FAT32), folder ct-recomp
/mnt/usb1/ct-recomp/
/data/ct-recomp/        internal storage
```

Put `rom.sfc` there, or any single `*.sfc` / `*.smc` file. For the engine build, put
`ct_game.so` there too. A USB stick is the easiest way. You can also use an FTP payload
(e.g. ftpsrv, port 2121):

```sh
curl -T rom/your_dump.sfc ftp://PS5_IP:2121/data/ct-recomp/rom.sfc
```

## Run

1. Run the exploit for your firmware until the ELF loader is listening (port 9021).
2. Send the payload from your PC:

   ```sh
   $PS5_PAYLOAD_SDK/bin/prospero-deploy -h PS5_IP -p 9021 build-ps5/ct_sdl.stripped.elf
   # or: socat -u FILE:build-ps5/ct_sdl.stripped.elf TCP:PS5_IP:9021
   ```

   A homebrew launcher that runs ELFs from `/data` works too.
3. If there's no ROM, a notification tells you where to put it.

The controls are the same as with a controller on the desktop (docs/USER-GUIDE.md). To quit,
hold **Create + Options** for 2 seconds, then press **Cross**. Settings and saves go in
`/user/home/<user id>/ct-recomp/`, through SDL's PS5 preferences path.

## When something goes wrong

Everything the program prints goes to `ct-recomp.log` in the first folder above that
exists (otherwise `/data/ct-recomp/`). It's line-buffered, so it's complete up to a crash.
Fetch it over FTP or from the USB drive. When you report a problem, include it with your
firmware version and the loader you used.

## How the port is put together

- `CMakeLists.txt`: the SDK's toolchain file sets `PS5`. That turns on `CT_PS5`: the
  `-DCT_PS5` define, the project's own context switch (`runtime/compat`) in place of the
  console libc's `getcontext`, an empty target in place of `-lm`, `.elf` program names, and
  `ct_sdl_engine` built without the first-run flow.
- `frontend/ps5.c` / `ps5.h`: the log file, the splash screen, notifications, and finding
  the ROM and module in the folders above. Elsewhere this file compiles to nothing.
- `frontend/main.c` (`#ifdef CT_PS5`): always fullscreen, the ROM and module from those
  folders, and the original renderer when no GL context can be made.
- `packaging/ps5/`: the dependency and build scripts.
