# Building and testing the native PS5 candidate

This procedure builds the native GPU title `PPSA28361` without a ROM. It produces
a fake-signed homebrew application folder, not a retail PKG. The 2026-11-28
candidate compiled, linked and passed container inspection, but has not yet run
on PS5 hardware.

## Reproducible inputs

Use separate directories for the project, dependencies and output. These names
are examples and do not need to match the original build machine.

```sh
export WORKSPACE=/path/to/ps5-build
export CT_SRC="$WORKSPACE/Triggered-Chrono-RecompHD2D"
export KIT="$WORKSPACE/ps5-native-app-boilerplate"
export GL_BUNDLE="$WORKSPACE/ps5-opengl-sdk-1.0.2"
export GL_PREFIX="$GL_BUNDLE/sdk"
export GL_SRC="$GL_BUNDLE/source-tree/ps5-opengl"
export SDL_PREFIX="$WORKSPACE/ps5-opengl/build/native-ct5/sdk"
export CANDIDATE="$WORKSPACE/ct5-candidate"
```

| Input | Identity |
| --- | --- |
| Engine source (public fork; identical tree to local `9defe61`) | `988807463b5d6a941cdeb73b05f249ed6951039d` |
| Host compiler | Debian Clang/LLVM 19.1.7, with matching compiler-rt builtins |
| Native app kit | `4f531c4b517f80bcb6b1267135848168d2250047` from the `leolobato` mirror |
| Validated fallback kit | `470695e0c557f99ff2df0e36e4df713c5e636526` from `drakmor`, with the repository patch applied in staging |
| Payload SDK | v0.42; archive SHA-256 `8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da` |
| ps5-opengl SDK | 1.0.2; `sdk/manifest.sha256` SHA-256 `0d432edc9d648d6e0cccb4d5eb3db8484f10ba4b075b5ccf23edda3251b66360` |
| SDL2 source | `8c56053f13ca13a0c050de613706ff69eb615836` |
| Module ABI | 11 |

Use the source snapshot shipped with the GL bundle as `GL_SRC`. It matches the
prebuilt runtime; do not silently replace its native runtime shims with files
from a later checkout. The SDL bridge is built separately with the runtime-mode
and audio changes in `packaging/ps5/title/gl/sdl-bridge.patch`. Its completed
`receipt.json` must name the same GL manifest and report `hardware_run: false`.

The pinned kit is available at
<https://github.com/leolobato/ps5-native-app-boilerplate>.
The complete GL SDK and corresponding sources are available at
<https://github.com/darkxex/ps5-opengl/releases/tag/v1.0.2>:
`ps5-opengl-sdk-1.0.2.tar.gz`, SHA-256
`30d8e51dd062739b629346102be5932728ba714bc50b1bdcdf08306c8843d9a4`.
The SDK's `sources/ps5-opengl.tar` extracts to `source-tree/ps5-opengl`.

Prepare the kit's pinned SDK and zlib, then verify the executable wrappers and
GL manifest:

```sh
git -C "$KIT" checkout 4f531c4b517f80bcb6b1267135848168d2250047
(cd "$KIT" && bash tools/setup-native-dependencies.sh)
test -x "$KIT/.deps/native/ps5-payload-sdk/bin/prospero-clang"
test -x "$KIT/.deps/native/ps5-payload-sdk/bin/prospero-cmake"
(cd "$GL_PREFIX" && sha256sum --check manifest.sha256)
```

To rebuild the SDL prefix in a prepared ps5-opengl working tree:

```sh
export GL_WORK="$WORKSPACE/ps5-opengl"
export SDL_SOURCE="$WORKSPACE/SDL"
python3 "$GL_WORK/integration/SDL2/build.py" native \
  --sdl-source "$SDL_SOURCE" \
  --sdk-prefix "$GL_PREFIX" \
  --out "$GL_WORK/build/native-ct5" \
  --payload-sdk "$KIT/.deps/native/ps5-payload-sdk" \
  --compiler-wrapper "$KIT/tooling/prospero-clang18"
export SDL_PREFIX="$GL_WORK/build/native-ct5/sdk"
```

`--out` must name a new directory. Apply the SDL bridge patch to a clean
integration tree before this command; do not apply it twice to a tree that
already contains the same changes. A completed native build writes and verifies
both `receipt.json` and `sdk/manifest.sha256`.

## Build the title

Put the selected LLVM tools, CMake and Python on `PATH`. If the kit wrapper does
not find `clang-18`, set `PS5_CLANG` to the tested Clang executable. The final
link also needs the matching x86-64 compiler builtins archive.

```sh
export PS5_CLANG="$(command -v clang)"
export BUILTINS="$(find "$(llvm-config --libdir)/clang" \
  -name 'libclang_rt.builtins*x86_64.a' -print -quit)"
test -s "$BUILTINS"

GL_PREFIX="$GL_PREFIX" \
SDL_PREFIX="$SDL_PREFIX" \
GL_SRC="$GL_SRC" \
KIT="$KIT" \
BUILTINS="$BUILTINS" \
  "$CT_SRC/packaging/ps5/title/gl/build-title-gl.sh" "$CANDIDATE"
```

The expected folder is:

```text
$CANDIDATE/app/dist/PPSA28361/
  eboot.bin
  sce_module/libc.prx
  sce_sys/icon0.png
  sce_sys/param.json
```

The build loops until its generated import table stops changing. Treat a
non-converged table, failed SELF inspection or missing file as a failed
candidate. Record the source and dependency revisions, GL/SDL receipt hashes,
complete build log, file sizes and SHA-256 values.

The first successful pinned-kit candidate recorded:

```text
imports: 331 from 9 modules
eboot.bin: 26321501 bytes
eboot.bin SHA-256: 08950bb3737261397029ffde6023b1082e1e3e18efe8648e2567209d8b67cb10
libc.prx SHA-256: e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036
```

The validated fallback kit produced the same two hashes.

## exFAT and compressed images

The complete app folder is the primary development output. To also produce the
console-compatible, exFAT-wrapped compressed form, run the kit's MkPFS target
from the staged app while preserving the title's linker group:

```sh
cd "$CANDIDATE/app"
make USE_CCACHE=0 APP_STATIC_ARCHIVES=vendor/libct_group.a ffpfsc
sha256sum dist/PPSA28361.ffpfsc
```

This creates `dist/PPSA28361.ffpfsc`. It is a mountable filesystem image, not a
retail PKG. The repository's FTP convention places either the complete folder at
`/data/homebrew/PPSA28361/` or the image at
`/data/homebrew/PPSA28361.ffpfsc`. Do not keep both same-ID forms in active scan
paths. The [ShadowMountPlus documentation](https://github.com/drakmor/ShadowMountPlus)
specifies `homebrew/` at the USB root as one of its scan locations. Copy a single
`PPSA28361.exfat` or `PPSA28361.ffpfsc` there, then rescan if necessary. Do not
create a literal `mnt/usb0` directory on the drive.

The test delivery also includes a raw `.exfat` image with 64 KiB clusters,
created with the pinned MkPFS exFAT writer and checked by reading its contents
back. The `.ffpfsc` option is compressed and remains experimental in the loader.
Both contain the same native title and no ROM. The raw image is the recommended
first USB test; the full app folder remains an alternative for FTP installation.

No ROM belongs in a shared candidate. For a folder deployment, the owner may put
their own headerless USA 1.0 dump at `/data/homebrew/PPSA28361/rom.sfc`. For an
image deployment, keep the ROM outside the read-only image at
`/data/ct-recomp/rom.sfc`, or at `ct-recomp/rom.sfc` on a USB drive exposed by
the console as `/mnt/usb0` or `/mnt/usb1`.

## Safe first hardware test

Before replacing anything, fully close the title and make recoverable copies of:

- the existing `/data/homebrew/PPSA28361/` folder or same-ID image;
- `/user/home/<user id>/ct-recomp/`, which contains settings and saves;
- any `/data/ct-recomp/` configuration, packs and logs needed for comparison.

Install the entire candidate folder or one image, never only `eboot.bin`. Keep
the console's loader, mount and FTP services on a trusted local network. Launch
manually only after transfer and mounting have completed.

Test in this order:

1. Boot and confirm that the title reaches its log without an unresolved-import notification.
2. Confirm the ROM is found. With no private `ct_game.so`, the log should say that the interpreter is in use.
3. Check video creation, HD-2D rendering, DualSense input, original audio, save creation and save-state load.
4. Check the opening, Crono's bed, first dialogue box, room edges, world flyover, battle return, bike race and Epoch cockpit.
5. Add the owner's MSU-1 pack and check missing-track fallback, loops, jingles, fades, pause/load/resume and an extended session.
6. Repeat at 1080p, 1440p and 4K only after the first stable 1080p run.

After a failure, preserve these files before another launch overwrites them:

```text
/data/ct-recomp/ct-recomp.log
/data/ct-recomp/imports.log
/data/ct-recomp/hd2d-profile.txt
```

Report the console firmware, loader and mount versions, selected resolution,
candidate SHA-256, module or interpreter mode, reproduction steps and logs.
Native compilation success alone is not evidence that a hardware scenario
passed.
