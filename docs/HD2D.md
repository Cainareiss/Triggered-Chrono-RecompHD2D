<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# HD-2D renderer

An optional way of presenting the game. The picture is drawn as a lit diorama, with
depth of field, bloom, soft shadows under characters and a filmic color grade, in the
style of modern "HD-2D" releases.

**It draws nothing of its own.** Every pixel comes from the running game's own frame,
which is generated from your ROM. The renderer only changes how that frame is lit,
blurred and graded, so the project ships no game art.

## Using it

```
CT_ROM=rom/your_dump.sfc cmake -S . -B build && cmake --build build -j
build/ct_sdl --hd2d                 # window
build/ct_sdl --hd2d --fullscreen    # or press F11; automatic under gamescope
```

| Key | Action |
| --- | --- |
| F1 | Cycle the preset: `off`, `subtle`, `hd2d` (default) |
| F2 | Cycle the debug view: layer classes, depth, blur amount, split compare (original pixels on the left), off |
| F3 | Cycle the aspect: 4:3 (default), 8:7 pixel aspect, stretch |
| F11 | Toggle fullscreen |

| Option | Effect |
| --- | --- |
| `--preset off\|subtle\|hd2d` | Choose the starting preset |
| `--aspect 4:3\|pixel\|stretch` | Choose the picture shape |
| `--debug-view N` | Start in debug view N (0 off, 1 classes, 2 depth, 3 blur, 4 split compare) |
| `--log-gpu` | Print the renderer's frame time every 60 frames (stalls the GPU to measure) |
| `--shot F:PATH` | At frame F, render offscreen and save a PNG (repeatable) |
| `--shot-size WxH` | Size of those screenshots (default 1920x1080) |

To render headlessly, for example on CI or over SSH, set `SDL_VIDEODRIVER=offscreen`.
That gives a GPU context through EGL with no window:

```
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy build/ct_sdl --hd2d --fast \
    --frames 9200 --script game/ct/test/replay/leene_square.txt --shot 9200:leene.png
```

## How it works

1. **Layer G-buffer.** The PPU (`third_party/snes/ppu.c`) writes a second buffer next to
   the color frame. It holds one byte per pixel, saying which layer won that pixel and at
   what priority: `z << 4 | math << 3 | layer`. The scheduler presents it with the frame
   (`sched_layers()`), along with a snapshot of PPU state (`sched_ppu_state_get()`). This
   doesn't change normal output: frame and state hashes match the unmodified PPU on all
   9,200 frames of the Leene Square replay.
2. **Classification** (`shaders/light.frag`). The layer byte sorts each pixel into one of
   six classes:
   - *backdrop*
   - *far planes*: low BG3/BG4
   - *ground*: low-priority BG1/BG2
   - *foreground*: high-priority BG1/BG2, such as arches, roofs and canopies
   - *actors*: sprites
   - *UI*: BG3 at the top priority level in mode 1, such as text boxes, menus and the
     battle HUD

   UI pixels skip every effect and stay sharp.
3. **Lighting at console resolution.** Pixel-scale lighting suits pixel art, so this pass
   runs at 256×224:
   - Relief normals from brightness give Lambert lighting from one warm key light.
   - Actor silhouettes cast soft shadows down and to the right.
   - Ground darkens where it meets foreground and actors (contact occlusion).
   - Actor edges facing the light get a rim light.
   - Distance haze is applied to the far planes.
4. **Sharp-bilinear upscale** to the output size.
5. **Tilt-shift depth of field** at half resolution:
   - A sharp band follows the actors' vertical center, and blur grows above and below it.
   - Planes away from the focal depth also blur.
   - Each sample is weighted by its own blur amount, so in-focus pixels don't bleed halos.
   - Actors stay mostly sharp wherever they stand.
   - The effect scales with how much of the screen is ground or foreground, so title
     cards and black-backed screens keep it off.
6. **Bloom**, from two Gaussian levels at 1/4 and 1/8 resolution.
7. **Grade:** a filmic shoulder, saturation, split toning (cool shadows, warm
   highlights), a vignette and light grain.
8. **Side fill:** outside the 4:3 picture, a blurred, mirrored and dimmed reflection of
   its edges.

Menus that cover more than 15–30% of the screen with UI ease the scene effects off.

## Performance

On a ROG Ally (Ryzen Z1 Extreme, Radeon 780M, Mesa radeonsi), the renderer takes about
1.8–2.1 ms per frame at 1706×1120, which is about 1080p's pixel count. That time
includes the uploads and the CPU-side scene analysis. Desktop AMD and NVIDIA GPUs have a
large margin.

## Limits and next steps

- The G-buffer stores only the *winning* pixel. Layers hidden behind others aren't
  available, so true parallax or 3D camera tilt isn't possible yet. That needs per-layer
  output from the PPU, which is a larger change.
- Classification uses PPU state only, with no per-game tables, so it stays game-agnostic
  (`tools/check_agnostic.py`). Some scenes will misclassify. Mode 7 scenes get
  tilt-shift only.
- The renderer works under OpenGL ES 3.0 for Android, but that hasn't been tested on a
  device.
- Settings aren't saved between runs yet.
