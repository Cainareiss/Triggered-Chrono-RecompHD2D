/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Save states: the whole machine as one versioned binary blob.
 *
 * What is in it: the CPU's registers and flags, WRAM, SRAM, the
 * multiply/divide unit and the WRAM port; the PPU (VRAM, CGRAM, OAM, every
 * register and latch of third_party/snes/ppu.c's Ppu, the adapter's open
 * bus and VRAM read latches, the OAM reload address); DMA/HDMA; the
 * scheduler (master clock, line and position in it, NMI/IRQ/H/V timer
 * state, pending transfers and interrupts, auto-joypad, the last frame
 * shown); the APU (SPC700 registers through ares's core, its 64 KB RAM, the
 * DSP's registers, voices and sample buffer, the timers, the CPU<->APU port
 * latches) and the DSP output not yet taken; the interpreter's WAI state.
 * Not in it: settings (native dispatch, players, the behind G-buffer),
 * frontend state (the audio resampler and queue, which a load restarts),
 * profiling counters, and a game module's own patch state (co-op).
 *
 * When: only between sched_run_frame calls, at a frame edge. The CPU runs
 * as a coroutine (sched.c) and is then suspended inside that frame's last
 * instruction accounting; native code may be many C frames deep. The C
 * stack is not saved. A load starts a new CPU coroutine that finishes the
 * interrupted accounting from saved state (sched_state) and then runs the
 * next instruction in the interpreter from PB:PC: native frames that were
 * live are dropped, which is safe because generated code keeps all CPU
 * state in the CPU struct at instruction boundaries (the overlay fallback
 * relies on the same), and every compiled function is dispatched natively
 * again at its next entry. The SPC700 core is a coroutine too and is
 * saved only when it sits at an instruction boundary (savestate_ready):
 * a save asked for elsewhere waits for the next frame edge where it does,
 * usually one to three frames.
 *
 * Format: an 8-byte magic, then little-endian fields: format version,
 * CT_MODULE_ABI, the ROM's SHA-1 (20 bytes), the frame count, then tagged
 * sections (4-byte tag, 4-byte length, payload) in a fixed order. A load
 * refuses another format version, module ABI (one older than
 * SAVESTATE_ABI_MIN, savestate.c, or newer) or ROM, and any section whose
 * length differs from this build's (the vendored structs are stored as
 * they are in memory, minus their pointers). */
#ifndef CT_SAVESTATE_H
#define CT_SAVESTATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAVESTATE_MAGIC "CTSTATE\x1a"
#define SAVESTATE_VERSION 1

/* Can a state be saved now? 0 if so; else 1 with *why (may be NULL) set:
   inside a frame, or the SPC700 between instruction boundaries (try again
   after the next sched_run_frame). Loading is possible whenever the CPU is
   not running (between sched_run_frame calls, or before the first). */
int savestate_ready(const char **why);

/* The state as a malloc'd blob (*len bytes), or NULL with *why. */
uint8_t *savestate_save(size_t *len, const char **why);
/* Restore a blob. 0, or -1 with *why (the machine is unchanged unless the
   blob was damaged past its header; then it is reset by the caller's
   choice: reload another state or restart). */
int savestate_load(const uint8_t *blob, size_t len, const char **why);

/* The header of a blob: 0 and the frame count it was taken at, or -1. */
int savestate_peek(const uint8_t *blob, size_t len, long *frame);

/* Files: write atomically (PATH.tmp, fsync, rename) / read and load. */
int savestate_write_file(const char *path, const char **why);
int savestate_read_file(const char *path, const char **why);

/* The loaded ROM's SHA-1, 40 hex digits (computed once). */
const char *savestate_rom_sha1(void);

/* ---- serialization, for the parts of the machine ----
   One function walks both ways: saving appends the bytes, loading copies
   them back. Every part brackets its fields in ss_begin/ss_end. */
typedef struct ss_io {
    int loading;            /* 0 save, 1 load, 2 check a blob's layout only */
    uint8_t *buf;           /* save: grows */
    size_t len, cap;
    const uint8_t *in;      /* load */
    size_t in_len, pos;
    size_t mark;            /* the open section's length field */
    const char *err;        /* first error; nothing more is read once set */
} ss_io;

void ss_bytes(ss_io *s, void *p, size_t n);
#define SS(s, v) ss_bytes((s), &(v), sizeof(v))
/* Loading for real (not a check), with nothing wrong so far: the time to
   apply what was read and fix up what a load changes. */
#define SS_APPLY(s) ((s)->loading == 1 && !(s)->err)
void ss_begin(ss_io *s, const char tag[4]);
void ss_end(ss_io *s);

/* The parts (each in its own file, next to its state). */
void bus_state(ss_io *s);         /* bus.c */
void snes_hw_state(ss_io *s);     /* snes_adapter.c */
void spc_host_state(ss_io *s);    /* spc700_host.cpp */
void interp_state(ss_io *s);      /* interp.c */
void sched_state(ss_io *s);       /* sched.c */
/* sched.c: 0 if the CPU can be saved now, else 1 with *why. */
int sched_state_ready(const char **why);
/* spc700_host.cpp: the core is at an instruction boundary. */
int spc_host_at_boundary(void);

#ifdef __cplusplus
}
#endif

#endif
