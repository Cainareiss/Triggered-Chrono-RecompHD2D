<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Android and arm64

Target: Android arm64-v8a. The status below covers the engine on 64-bit
ARM Linux, tested under qemu on an x86_64 host. There is no APK yet; the
last section lists what an APK still needs.

## What is done

### Coroutines without `makecontext`

The runtime runs two coroutines. The CPU runs on its own stack and yields
at every frame edge (`runtime/sched.c`), and the ares SPC700 core yields
mid-instruction (`runtime/spc700_host.cpp`). Both use POSIX
`getcontext`/`makecontext`/`swapcontext`. Android's bionic has none of
these, and musl declares them but doesn't implement them.

`runtime/compat/ucontext.h` supplies its own context switch, with one
backend per architecture. Both are written from the platform ABI documents:

| file | ABI | saved on a switch |
|---|---|---|
| `runtime/compat/ucontext_aarch64.c` | AAPCS64 | x19-x28, x29, x30 (resume address), sp, d8-d15 |
| `runtime/compat/ucontext_x86_64.c` | System V AMD64 psABI | rbx, rbp, r12-r15, rsp, rip, MXCSR, x87 control word |

The backends cover only what the runtime uses:

- `getcontext` captures its caller, and resuming that context returns from
  `getcontext` again.
- `makecontext(ucp, fn, 0)` starts `fn` on `uc_stack`, 16-byte aligned,
  through an entry trampoline. Only 0 arguments are supported; any other
  count aborts.
- `swapcontext` and `setcontext`.
- `uc_link`: when the function returns, `uc_link` is resumed. With no
  `uc_link` the program prints `makecontext coroutine returned with no
  uc_link` and aborts.
- No signal mask is saved. glibc makes a `sigprocmask` system call on every
  switch, and the runtime never changes the mask.
- On aarch64, FPCR (rounding mode) is not saved. The runtime never changes
  it.

The CMake option `CT_COMPAT_UCONTEXT` selects it. It defaults to ON for
Android, or when a test program using `makecontext` doesn't link (musl).
Otherwise it is OFF. With it OFF, the header is just `<ucontext.h>`. On
glibc the default stays OFF, and the code is unchanged: the `.text`,
`.rodata`, `.data` and `.bss` of `sched.c`, `overlay.c`, `spc700_host.cpp`,
`ct_boot.c` and `test_native.c` match the previous commit byte for byte (gcc,
the build's flags). `-DCT_COMPAT_UCONTEXT=ON` forces our switch on glibc
for testing.

### `__builtin_setjmp` on aarch64

When native overlay code finds its own bytes have changed, it drops back to
the interpreter with a jump (`runtime/overlay.h`, `overlay_bail` in
`sched.c`). That jump used `__builtin_setjmp`/`__builtin_longjmp`. **Clang
rejects both for aarch64:** zig 0.15.2's clang reports `error:
__builtin_setjmp is not supported for the current target` at `-O0` and
`-O2`. The NDK r27c's clang 18 rejects them the same way, for both
aarch64-linux-android and aarch64-linux-gnu.

`overlay.h` now has `CT_SETJMP`/`CT_LONGJMP` and `ct_jmp_buf`:

- **On aarch64, with any compiler**, they are `_setjmp`/`_longjmp`. Each
  target then has one `overlay_act` layout, however it was compiled.
  `_setjmp` saves the callee-saved registers but never the signal mask.
  Plain `setjmp` on bionic saves the mask with a system call, and the
  setjmp runs on every overlay dispatch. Defining `CT_PORTABLE_SETJMP`
  selects this path on any target.
- Everywhere else (x86_64, i386, and armv7a, where the NDK's clang compiles
  the builtins), they are still the builtins, and the code on those builds
  is unchanged.

This use is safe under ISO C's rules for `setjmp`. The setjmp sits alone in
a `noinline` function (`run_overlay`). No local it reads afterwards changes
between the setjmp and the longjmp. The longjmp always comes from a deeper
frame on the same stack. `test_native` covers this path: an overlay rewrites
itself and falls back to the interpreter. It passes on aarch64.

### Cross build: aarch64 Linux with zig

`cmake/toolchain-aarch64-zig.cmake` builds with `zig cc`/`zig c++
-target aarch64-linux-musl` (tested with zig 0.15.2). The zig path comes
from `CT_ZIG`:

```sh
CT_ROM=/path/to/rom.sfc cmake -B build-arm64 \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-zig.cmake \
    -DCT_ZIG=/path/to/zig -DCT_SDL2_SYSTEM_LIB=OFF
cmake --build build-arm64 -j
cd build-arm64 && ctest -LE rom    # under qemu-aarch64-static
```

- The toolchain file writes one-word wrappers to `build-arm64/zig-wrap/`
  (`zig-cc`, `zig-c++`, `zig-ar`, `zig-ranlib`) that go through
  `cmake/zig-wrap.sh`. zig cc rejects CMake's `-Xlinker X` and
  `--push-state`, so the wrapper rewrites them. `zig-cc` is also a
  single-program CC for `tools/build_module.py --cc`.
- Tests run under `CMAKE_CROSSCOMPILING_EMULATOR`: `qemu-aarch64-static`,
  or `CT_QEMU`. When `CT_QEMU_SYSROOT` is set, the emulator is called with
  `-L` pointing at it.
- Without `-DCT_SDL2_SYSTEM_LIB=OFF`, the build picks up the host's x86_64
  libSDL2. The frontends are not built for arm64 Linux.
- Code generation (`recomp/emit.py`, the overlay images) runs on the host
  as usual.

## How it was verified

The test host is x86_64 Fedora, with qemu-aarch64-static. Every build came
from the same commit.

- **Hash logs.** Each build ran `ct_boot --frames 3000 --script
  game/ct/test/replay/leene_square.txt --hash-log`, which logs per-frame
  hashes of the CPU, WRAM, SRAM, VRAM, CGRAM, OAM, frame and APU. All 3000
  lines are identical across these runs (sha256 of the log `2597688c…`):
  - x86_64 glibc, static `ct_boot`
  - x86_64 glibc, `ct_boot_engine` loading the module `ct_game.so`
  - x86_64 with `-DCT_COMPAT_UCONTEXT=ON`, both of the above
  - **aarch64 musl, static `ct_boot` under qemu**

  The aarch64 run exercises the AAPCS64 switch: the CPU coroutine plus the
  SPC700 coroutine, which switches several times per scanline. It also
  exercises the arithmetic of all the recompiled code.
- **Engine tests under qemu.** In the aarch64 build, `ctest -LE rom` passes
  26/26, including `runtime` and its fatal cases, `sched`, `native`,
  `snes_bus`, `interp_*`, `dsp`, `resample`, `stack_guard` and
  `ucontext`. Tests that run on the host (the Python report tests,
  `check_agnostic`) are counted too.
- **ROM tests under qemu.** In the same build, `ctest -L rom -E diff_all`
  passes 31 of 33:
  - Passing: the lockstep runs against the interpreter (`lockstep_boot`,
    `lockstep_leene_square`, `lockstep_gato_battle`, `lockstep_gato_win`),
    `stack_field_replay`, `battle_no_flicker`, `native_coverage`,
    `boot_interp`, `boot_menu`, the `c0_*`/`c1_*` function tests, and the
    rest.
  - `sync_determinism` skips itself, as it does on the host.
  - `module_parity` fails, because the musl engine can't `dlopen` (below).
- **Host, compat switch forced on.** The x86_64 build with
  `-DCT_COMPAT_UCONTEXT=ON` passes `ctest -E diff_all`, apart from
  `sync_determinism`, which skips itself. The real-time `sdl_audio_60s`
  failed with underruns while a qemu run loaded the machine, and passed when
  rerun alone. The default glibc build passes the same set
  (`ctest_cache_report` needs `cmake` on `PATH`).
- **`test_ucontext`** (new, `test/test_ucontext.c`) passes on aarch64 under
  qemu at `-O0` and `-O2`, and on x86_64 with both the compat switch and
  glibc's. It checks a 1000-round ping-pong with callee-saved integer and
  floating-point values live across switches, a 16-byte aligned entry on an
  unaligned stack buffer, a `getcontext` resumed a second time, and
  `uc_link`. `ucontext_returns` checks the loud abort.

- **NDK compile and link check.** The whole engine without SDL builds with
  NDK r27c (clang 18):
  `-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
  -DCT_SDL2_SYSTEM_LIB=OFF`. The probe turns `CT_COMPAT_UCONTEXT` on
  (bionic has no `makecontext`). The outputs are `ct_boot_engine` and every
  engine test, as Android PIEs that use `/system/bin/linker64`. Those can't
  run on the test host, which has no `linker64`.
- **Static bionic under qemu.** The same libraries, linked `-static`
  against bionic (API 26), run under qemu-aarch64-static and pass:
  `test_ucontext` (and its `returns` abort), `test_sched`, `test_runtime`,
  `test_interp_system`, `test_snes_bus`, `test_dsp`, `test_stack_guard` and
  `test_native` (all 54 checks, with `TMPDIR` set). Two quirks of the setup:
  - qemu rejects bionic's emulated-TLS segment at API 26 ("TLS segment is
    underaligned"). Linking in one 64-byte aligned native TLS variable fixes
    it: `__thread char pad __attribute__((aligned(64), used));`, compiled
    with `-fno-emulated-tls`.
  - Without `TMPDIR`, bionic's `tmpfile()` tries `/data/local/tmp`, and
    `test_native`'s two profile checks fail.

### Not verified

- **The dynamic engine and module on aarch64.** zig links musl statically
  by default, and static musl can't `dlopen`: `ct_boot_engine` reports
  `--module: Dynamic loading not supported`. zig can link against musl's
  dynamic loader (`-dynamic`), but it ships only a link stub of `libc.so`,
  so qemu needs a real aarch64 `ld-musl-aarch64.so.1` (`CT_QEMU_SYSROOT`).
  None is on the test host, so the static `ct_boot` stands in for that
  proof. `ct_game.so` does build for aarch64, but it wasn't loaded. The
  x86_64 hashes show module and static builds match there.
- `tools/build_module.py --cc build-arm64/zig-wrap/zig-cc` wasn't run. The
  CMake target `ct_game_module` built the aarch64 module instead.
- Real arm64 hardware, and Android itself: nothing has run on a device or
  through bionic's dynamic linker. The static bionic run above is the
  closest test.
- The game (`ct_boot`, the replay) with bionic: only the engine tests ran
  under bionic. The replay hashes come from the musl build.

## What an APK still needs

1. **NDK build of the engine.** Configure with
   `-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake
   -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24` (or later).
   This builds and links already (see above); `CT_COMPAT_UCONTEXT`
   and the `_setjmp` path are selected automatically.
2. **SDL2's `android-project` and Gradle.** SDL's Java activity loads
   `libmain.so` and calls `SDL_main`. Build `frontend/main.c` (with the
   HD-2D renderer, GLES 3.0) as a shared library `main` linked against
   SDL2, built from source with the NDK. Drop it into
   `app/src/main/jniLibs/arm64-v8a` or use `externalNativeBuild` with
   this CMake project. Assets: the shaders are already embedded.
3. **The game module.** The APK must not contain anything generated from
   the ROM. Build `ct_game.so` from the user's own ROM with the NDK clang:
   `tools/build_module.py --cc $NDK/toolchains/llvm/prebuilt/<host>/bin/aarch64-linux-android24-clang`
   on a PC, or on the device if a compiler is packaged. Then load it from
   the app's private storage. Android 10+ restricts executing files from
   app-writable storage, so check on a device whether `dlopen` of a `.so`
   copied there is allowed for the target SDK. If it isn't, the first-run
   flow needs another plan. A module built against musl (as in the qemu
   test) is not ABI-compatible with bionic.
4. **ROM access.** Use a Storage Access Framework picker in place of the
   desktop first-run ROM search (`frontend/firstrun.c`), checked with the
   same SHA-1.
5. **Touch overlay.** On-screen controls mapped to the SNES pad, handled by
   another branch.
6. **Audio and lifecycle.** SDL's audio on AAudio/OpenSL, pausing the
   scheduler on `SDL_APP_WILLENTERBACKGROUND`, and save data in app storage.
