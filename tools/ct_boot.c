/* Boot probe: run the game in $CT_ROM from reset in the system-mode
 * interpreter under the frame scheduler, headless.
 *
 * usage: ct_boot [--frames N] [--dump DIR] [--needed-hw FILE] [options]
 *
 * --dump DIR       write DIR/frame_NNNNN.png for every frame the PPU drew
 *                  anything into (any nonblack pixel) that differs from the
 *                  last one written
 * --shot-every N DIR  write DIR/shot_NNNNNNN.png at every Nth frame edge (all
 *                  frames, black or not): checkpoints along a long replay
 * --resync REF LAG OUT [--resync-jobs N]  play the --script movie against a
 *                  video of it (REF: 64x56 gray frames; script frame s is
 *                  video frame s+LAG), shifting inputs where the run falls
 *                  out of step, with N trial runs at a time (default 8);
 *                  OUT gets the inputs as played (tools/tas_resync.c)
 * --thumbs FILE    append every frame to FILE as a 64x56 8-bit luma thumbnail
 *                  (4x4 box average of (R+2G+B)/4), 3584 bytes a frame: for
 *                  lining a long replay up with a video of the same inputs
 * --needed-hw FILE write FILE: the fatal error that stopped the run, if any
 *                  (where the boot stops next), and the hardware stubbed
 *                  on purpose along the way
 * --input F1-F2:B  hold buttons B on pad 1 for frames F1..F2 (1-based,
 *                  inclusive; repeatable). B: names joined by '+' (b y
 *                  select start up down left right a x l r) or a hex mask
 * --script FILE    --input specs from FILE, one per line ('#' comments)
 * --hash-log FILE  write per-frame state hashes to FILE, taken at the frame
 *                  edge (native and interpreted runs must agree): "frame
 *                  cpu=H wram=H sram=H vram=H cgram=H oam=H frame=H apu=H",
 *                  FNV-1a 64 each; apu covers the SPC700 registers, ports,
 *                  timers, its RAM and the DSP registers
 * --expect-pc ADDR exit 1 unless the instruction at ADDR (hex) ran
 *                  (repeatable)
 * --expect-wram F:ADDR=V / F:ADDR!=V  exit 1 unless the WRAM byte at ADDR
 *                  (hex offset, 0-1FFFF) is (is not) V (hex) at the edge of
 *                  frame F (repeatable): a replay's outcome, e.g. an enemy's
 *                  HP at 0
 * --poke-wram F:ADDR^M  XOR the WRAM byte at ADDR (hex offset, 0-1FFFF)
 *                  with M (hex, nonzero) at the edge of frame F, before that
 *                  frame's hashes: a known difference for
 *                  tools/ref_compare.py --inject-wram to find
 * --log-ppu LO:HI[@F] FILE  log every write to PPU registers LO-HI (hex,
 *                  $2100-$213F; CPU, DMA or HDMA) from frame F on (default
 *                  1) as "frame line dot reg value"
 * --ref-log FILE  write "frame wram_hash frame_hash" per frame to FILE, the
 *                  hashes tools/mesen_ref.py logs from a reference emulator:
 *                  FNV-1a 64 of WRAM, and of the frame as 15-bit pixels
 *                  (" dma" appended when the frame edge fell inside a general
 *                  DMA: its bytes all landed at its start)
 * --wram FILE      write the 128 KB of WRAM to FILE at the last frame edge
 * --vram FILE      likewise the 64 KB of VRAM, then CGRAM (512 bytes) and
 *                  OAM (544 bytes)
 * --aram FILE      likewise the SPC700's 64 KB of RAM
 * --scene FILE     likewise the scene the game module describes (module.h,
 *                  ct_scene_capture), as text: map, camera, layer tilemaps,
 *                  cells and objects (test_scene.py checks it)
 * --profile N[@F]  at the end, print the native share of instructions and
 *                  the N functions most interpreted instructions ran in,
 *                  with why each didn't run natively (and, for entries run
 *                  with another DB/DP, the DB/DP pairs seen); @F counts only
 *                  from frame F on
 * --min-native P   exit 1 unless at least P percent of instructions ran
 *                  natively
 * --max-stack N    print the deepest the CPU's C stack got (bytes, measured
 *                  at the dispatcher and at each line's events) and exit 1
 *                  if it passed N: native code must not nest without bound
 *                  (#101)
 * --watch A[:N]    print every change to WRAM bytes A..A+N-1 (A a WRAM
 *                  offset in hex, 0-1FFFF; N default 2): frame, line, the
 *                  instruction that made it, old and new bytes (repeatable)
 * --at ADDR        print frame, line and master clock each time the
 *                  instruction at ADDR (hex) runs, the first 20 times
 *                  (repeatable)
 * --log-entry ADDR[:MEM] print one line every time the instruction at ADDR
 *                  (hex) runs: "entry ADDR frame F line L clock C A X Y S DP
 *                  DB PB m x" and the 16 bytes at MEM (hex, bank 0 WRAM;
 *                  default DP), for tools that read a routine's arguments
 *                  (repeatable)
 * --snap-at ADDR[@F] DIR  write the 128 KB of WRAM to DIR/snap_NNNN.bin each
 *                  time the instruction at ADDR (hex) runs, from frame F on
 *                  (first 256 times), for checking what a routine left in
 *                  memory
 * --wram-entries F  write "ADDR mMxX" to F for every WRAM address ($7E/$7F)
 *                  execution arrives at from ROM other than by a return
 *                  (jumps, calls, interrupts into WRAM code), once per
 *                  address and state, for finding entries into code the
 *                  game puts in WRAM
 * --overlay-strict / --overlay-fallback  when native overlay code (#92) finds
 *                  its own bytes changed while it runs: fail loudly (debug
 *                  builds' default; lockstep and reference runs use it), or
 *                  carry on in the interpreter (release builds' default)
 * --first-exec F   write "ADDR clock" to F the first time each instruction
 *                  address runs (for timing comparisons against a reference)
 * --trace LO:HI[@N] F  write "ADDR clock X Y" to F for every instruction
 *                  starting at master clock LO-HI (from frame N on: a reset
 *                  restarts the clock), for trace diffs against a reference
 * --interp-only    run everything in the interpreter (no native dispatch)
 * --coop           port: two players (sched_set_players): the game module's
 *                  co-op patches share the game with pad 2, which a
 *                  script's "p2 F1-F2:BUTTONS" lines drive (they reach port
 *                  2 with or without --coop)
 * --coop-chars L / --coop-chars3 L  the party slots (1-3, comma list) player
 *                  2 / player 3 command (default: 2 and 3 to player 2);
 *                  --coop-chars3 also turns on a third player (pad 3)
 * --always-run     port: the always-run preference on (sched_set_pref): a
 *                  game module's patches that honour it invert the run button
 * --log-rumble FILE  port: simulated rumble on (runtime/rumble.h): write
 *                  each event the game module and the detectors raise,
 *                  "rumble frame F player P source tag low high ms", to
 *                  FILE ("-": stdout). It only reads: the run is the same
 * --rumble-sound   with --log-rumble, the sound detector too
 * --coop-split     --coop with the experimental split screen
 *                  (sched_set_views(2))
 * --shot F:PATH    write frame F as a PNG to PATH (repeatable); with
 *                  --coop-split and a second view up, it to PATH.view2.png
 * --ref-quirk NAME reproduce a reference emulator's quirk, for comparisons
 *                  only (tools/ref_compare.py): mesen-dma-count8
 *
 * --min-nmis K     exit 1 unless at least K NMIs were taken
 * --require-render exit 1 if the last frame is all black
 * --wav FILE       write the audio (the DSP output, 32 kHz stereo) to FILE
 * --require-audio  exit 1 if every audio sample of the run is zero
 * --behind         also draw the PPU's behind G-buffer (sched_behind), as
 *                  the HD-2D renderer does, to measure what it costs
 * --prov           also draw the PPU's provenance G-buffer (sched_prov), as
 *                  asset packs do (port)
 * --save-state F:PATH  port: write a save state (runtime/savestate.h) at
 *                  the first frame edge from F on where one can be taken
 *                  (the SPC700 at an instruction boundary), and print
 *                  "ct_boot: state saved at frame N to PATH"
 * --load-state PATH  port: start from a save state instead of from reset;
 *                  --frames still counts from reset
 * --state-roundtrip N  port: every N frames (at the first edge where one can
 *                  be taken), save a state and load it straight back: the
 *                  run goes on from the loaded state (a new CPU coroutine,
 *                  native frames dropped), and must match one without
 *                  (--hash-log); prints how many round trips it made
 *
 * On a fatal error the last 64 instruction addresses are printed.
 *
 * Exit 0 after N frames, 1 on a fatal error, 2 on bad usage. */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus.h"
#include "hwlog.h"
#include "interp.h"
#include "png.h"
#include "replay.h"
#include "sched.h"
#include "overlay.h"
#include "snes_adapter.h"
#include "spc700_host.h"
#include "ppu.h"
#include "apu.h"
#include "dsp.h"
#include "wav.h"
#include "module.h"   /* port: --module */
#include "savestate.h"   /* port: --save-state, --load-state */
#include "tas_resync.h"
#include "rumble.h"   /* port: --log-rumble */

static jmp_buf fatal_jmp;
static char fatal_msg[256];

static void on_fatal(const char *msg)
{
    snprintf(fatal_msg, sizeof fatal_msg, "%s", msg);
    longjmp(fatal_jmp, 1);
}

/* Last executed instruction addresses (with the CPU state at each). */
#define TAIL 64
static struct {
    uint32_t at;
    CPU c;
} tail[TAIL];
static unsigned tail_n;

#define MAX_EXPECT 8
static uint32_t expect_pc[MAX_EXPECT];
static int expect_hit[MAX_EXPECT], n_expect;
static struct {
    long frame;
    uint32_t addr;
    unsigned val;
    int neq, seen, ok;
} expect_wram[MAX_EXPECT];
static int n_expect_wram;
static FILE *ppu_log;
static unsigned ppu_log_lo, ppu_log_hi;
static long ppu_log_from = 1;
static void log_ppu_write(uint16_t reg, uint8_t v)
{
    if (reg >= ppu_log_lo && reg <= ppu_log_hi && sched_frame_count() >= ppu_log_from)
        fprintf(ppu_log, "%ld %d %d %04X %02X\n", sched_frame_count(), sched_line(),
                snes_ppu_dot ? snes_ppu_dot() : -1, reg, v);
}
static long profile_from;
static long max_stack_allowed = -1;
static long poke_frame = -1;
static uint32_t poke_addr;
static uint8_t poke_val;

#define MAX_WATCH 8
static struct {
    uint32_t at, len;
    uint8_t last[16];
} watch[MAX_WATCH];
static int n_watch;
static uint32_t prev_at;   /* the instruction before this one */

static void check_watch(uint32_t at)
{
    const uint8_t *w = bus_wram();
    for (int k = 0; k < n_watch; k++) {
        if (!memcmp(watch[k].last, w + watch[k].at, watch[k].len))
            continue;
        printf("watch $%05X: frame %ld line %d after $%06X:", watch[k].at, sched_frame_count(),
               sched_line(), prev_at);
        for (uint32_t b = 0; b < watch[k].len; b++)
            printf(" %02X", watch[k].last[b]);
        printf(" ->");
        for (uint32_t b = 0; b < watch[k].len; b++)
            printf(" %02X", w[watch[k].at + b]);
        printf("  (now at $%06X)\n", at);
        memcpy(watch[k].last, w + watch[k].at, watch[k].len);
    }
    prev_at = at;
}

#define MAX_AT 8
static uint32_t at_pc[MAX_AT];
static int at_seen[MAX_AT], n_at;

#define MAX_LOG 64
static uint32_t log_pc[MAX_LOG];
static int log_mem[MAX_LOG];   /* bank 0 address to dump, -1: DP */
static int n_log;

static uint32_t snap_pc = ~0u;
static long snap_from;
static const char *snap_dir;
static unsigned n_snaps;

static FILE *wram_entries;
static uint8_t *wram_entry_seen;   /* per WRAM address: bit 1 << (m << 1 | x) */
static uint32_t last_at = ~0u;

static FILE *first_exec;
static FILE *trace_out;
static unsigned long long trace_lo, trace_hi;
static long trace_from;
static uint8_t *seen_pc;   /* one bit per 24-bit address */

static void trace(const CPU *c, uint32_t at)
{
    if (first_exec && !(seen_pc[at >> 3] & 1 << (at & 7))) {
        seen_pc[at >> 3] |= (uint8_t)(1 << (at & 7));
        fprintf(first_exec, "%06X %llu\n", at, (unsigned long long)sched_clock());
    }
    if (trace_out) {
        unsigned long long t = sched_clock();
        if (t >= trace_lo && t <= trace_hi && sched_frame_count() >= trace_from)
            fprintf(trace_out, "%06X %llu %04X %04X\n", at, t, c->X, c->Y);
    }
    if (wram_entries && (at >> 16 == 0x7E || at >> 16 == 0x7F) && last_at != ~0u &&
        last_at >> 16 != 0x7E && last_at >> 16 != 0x7F) {
        uint8_t op = bus_peek(last_at);
        uint8_t bit = (uint8_t)(1u << (c->m << 1 | c->x));
        if (op != 0x60 && op != 0x6B && op != 0x40 && !(wram_entry_seen[at & 0x1FFFF] & bit)) {
            wram_entry_seen[at & 0x1FFFF] |= bit;
            fprintf(wram_entries, "%06X m%dx%d\n", at, c->m, c->x);
        }
    }
    last_at = at;
    if (at == snap_pc && n_snaps < 256 && sched_frame_count() >= snap_from) {
        char path[4096];
        snprintf(path, sizeof path, "%s/snap_%04u.bin", snap_dir, n_snaps++);
        FILE *f = fopen(path, "wb");
        if (!f || fwrite(bus_wram(), 1, 0x20000, f) != 0x20000)
            fprintf(stderr, "ct_boot: cannot write %s\n", path);
        if (f)
            fclose(f);
    }
    for (int k = 0; k < n_log; k++)
        if (at == log_pc[k]) {
            printf("entry %06X frame %ld line %d clock %llu A %04X X %04X Y %04X S %04X DP %04X "
                   "DB %02X PB %02X m %d x %d dp", at, sched_frame_count(), sched_line(),
                   (unsigned long long)sched_clock(), c->A, c->X, c->Y, c->S, c->DP, c->DB, c->PB,
                   c->m, c->x);
            for (unsigned b = 0; b < 16; b++) {
                unsigned a = (uint16_t)((log_mem[k] < 0 ? c->DP : (unsigned)log_mem[k]) + b);
                if (a < 0x2000)
                    printf(" %02X", bus_wram()[a]);
                else
                    printf(" --");
            }
            printf("\n");
        }
    for (int k = 0; k < n_at; k++)
        if (at == at_pc[k] && at_seen[k]++ < 20)
            printf("at $%06X: frame %ld line %d clock %llu\n", at, sched_frame_count(), sched_line(),
                   (unsigned long long)sched_clock());
    if (n_watch)
        check_watch(at);
    for (int k = 0; k < n_expect; k++)
        if (at == expect_pc[k])
            expect_hit[k] = 1;
    tail[tail_n % TAIL].at = at;
    tail[tail_n % TAIL].c = *c;
    tail_n++;
}

static replay input;

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t k = 0; k < n; k++)
        h = (h ^ b[k]) * 0x100000001B3ull;
    return h;
}

static CPU cpu;

#define HASH0 0xCBF29CE484222325ull

/* One --hash-log line: frame, then an FNV-1a 64 hash per component, so a
   divergence names what differs. */
static void write_state_hashes(FILE *f, long frame)
{
    const CPU *c = &cpu;
    uint16_t r[] = {c->A, c->X, c->Y, c->S, c->DP, c->DB, c->PB, c->PC,
                    (uint16_t)(c->m | c->x << 1 | c->e << 2 | c->i << 3 | c->d << 4 | c->c << 5 |
                               c->z << 6 | c->v << 7 | c->n << 8)};
    Ppu *ppu = snes_hw_ppu();
    Apu *apu = snes_hw_apu();
    uint8_t spc[8];
    spc_host_regs(spc);
    uint8_t apu_regs[] = {spc[0], spc[1], spc[2], spc[3], spc[4], spc[5], spc[6],
                          spc[7], apu->dspAdr, apu->romReadable,
                          apu->inPorts[0], apu->inPorts[1], apu->inPorts[2], apu->inPorts[3],
                          apu->outPorts[0], apu->outPorts[1], apu->outPorts[2], apu->outPorts[3]};
    uint64_t h_apu = fnv(fnv(fnv(HASH0, apu_regs, sizeof apu_regs), apu->ram, sizeof apu->ram),
                         apu->dsp->ram, sizeof apu->dsp->ram);
    for (int t = 0; t < 3; t++) {
        const Timer *tm = &apu->timer[t];
        uint8_t v[] = {tm->cycles, tm->divider, tm->target, tm->counter, tm->enabled};
        h_apu = fnv(h_apu, v, sizeof v);
    }
    fprintf(f,
            "%ld cpu=%016llx wram=%016llx sram=%016llx vram=%016llx cgram=%016llx "
            "oam=%016llx frame=%016llx apu=%016llx\n",
            frame, (unsigned long long)fnv(HASH0, r, sizeof r),
            (unsigned long long)fnv(HASH0, bus_wram(), 0x20000),
            (unsigned long long)fnv(HASH0, bus_sram(), CT_SRAM_SIZE),
            (unsigned long long)fnv(HASH0, ppu->vram, sizeof ppu->vram),
            (unsigned long long)fnv(HASH0, ppu->cgram, sizeof ppu->cgram),
            (unsigned long long)fnv(HASH0, ppu->oam, sizeof ppu->oam),
            (unsigned long long)fnv(HASH0, sched_frame(), (size_t)SCHED_WIDTH * SCHED_HEIGHT * 4),
            (unsigned long long)h_apu);
}

/* The frame as tools/mesen_ref.py hashes it: 256x224, each pixel a
   little-endian 15-bit value, 5 bits per channel (B G R x bytes in). */
static uint64_t frame_hash(void)
{
    const uint8_t *p = sched_frame();
    uint64_t h = 0xCBF29CE484222325ull;
    for (int k = 0; k < SCHED_WIDTH * SCHED_HEIGHT; k++, p += 4) {
        uint16_t c = (uint16_t)(p[2] >> 3 | (p[1] >> 3) << 5 | (p[0] >> 3) << 10);
        uint8_t b[2] = {(uint8_t)c, (uint8_t)(c >> 8)};
        h = fnv(h, b, 2);
    }
    return h;
}

static int nonblack(const uint8_t *p, size_t n)
{
    for (size_t k = 0; k < n; k += 4)
        if (p[k] | p[k + 1] | p[k + 2])
            return 1;
    return 0;
}

static const char *dump;
static const char *shot_dir;   /* --shot-every */
static long shot_every;
static FILE *thumbs;   /* --thumbs */

static void write_thumb(FILE *out, const uint8_t *bgrx)
{
    uint8_t t[56][64];
    for (int ty = 0; ty < 56; ty++)
        for (int tx = 0; tx < 64; tx++) {
            unsigned sum = 0;
            for (int y = ty * 4; y < ty * 4 + 4; y++)
                for (int x = tx * 4; x < tx * 4 + 4; x++) {
                    const uint8_t *p = bgrx + ((size_t)y * SCHED_WIDTH + x) * 4;
                    sum += p[0] + 2u * p[1] + p[2];
                }
            t[ty][tx] = (uint8_t)(sum / 64);
        }
    fwrite(t, 1, sizeof t, out);
}
static FILE *wav, *hash_log, *ref_log;
static FILE *rumble_log;   /* port: --log-rumble */
static long rumble_count;
static const char *wram_path, *vram_path, *aram_path, *scene_path;
static long last_frame;
static int dumped_state;

static void dump_scene(void);

/* --wram / --vram: WRAM, then VRAM, CGRAM and OAM, as they are at the
   last frame edge (a native call still running there doesn't move it). */
static void dump_state(void)
{
    dumped_state = 1;
    if (scene_path)
        dump_scene();
    if (vram_path) {
        Ppu *ppu = snes_hw_ppu();
        FILE *f = fopen(vram_path, "wb");
        if (!f || fwrite(ppu->vram, 1, sizeof ppu->vram, f) != sizeof ppu->vram ||
            fwrite(ppu->cgram, 1, sizeof ppu->cgram, f) != sizeof ppu->cgram ||
            fwrite(ppu->oam, 1, sizeof ppu->oam, f) != sizeof ppu->oam)
            fprintf(stderr, "ct_boot: cannot write %s\n", vram_path);
        if (f)
            fclose(f);
    }
    if (aram_path) {
        FILE *f = fopen(aram_path, "wb");
        if (!f || fwrite(snes_hw_apu()->ram, 1, 0x10000, f) != 0x10000)
            fprintf(stderr, "ct_boot: cannot write %s\n", aram_path);
        if (f)
            fclose(f);
    }
    if (wram_path) {
        FILE *f = fopen(wram_path, "wb");
        if (!f || fwrite(bus_wram(), 1, 0x20000, f) != 0x20000)
            fprintf(stderr, "ct_boot: cannot write %s\n", wram_path);
        if (f)
            fclose(f);
    }
}
/* --scene: the scene (module.h) as text, one record per line. */
static void dump_scene(void)
{
    static ct_scene s;
    memset(&s, 0, sizeof s);
    FILE *f = fopen(scene_path, "w");
    if (!f) {
        fprintf(stderr, "ct_boot: cannot write %s\n", scene_path);
        return;
    }
    int ok = !ct_scene_capture(&s);
    fprintf(f, "mode %d\n", ct_scene_available() ? s.mode : -1);   /* -1: no source */
    if (ok && s.mode == CT_SCENE_FIELD) {
        fprintf(f, "map %u %u cell %u id %u gen %u\n", s.map_w, s.map_h, s.cell_px, s.map_id,
                s.map_generation);
        fprintf(f, "camera %d %d\nplayer %d\n", (int)s.camera_x, (int)s.camera_y,
                s.player == 0xFF ? -1 : s.player);
        for (int l = 0; l < CT_SCENE_LAYERS; l++) {
            const ct_scene_layer *L = &s.layer[l];
            fprintf(f, "layer %d present %u bpp %u chr %04X pal %u\n", l, L->present, L->bpp,
                    L->chr_base, L->pal_base);
            for (int y = 0; y < 2 * s.map_h; y++) {
                fprintf(f, "tiles %d %d", l, y);
                for (int x = 0; x < 2 * s.map_w; x++)
                    fprintf(f, " %04X", L->tiles[y * CT_SCENE_STRIDE + x]);
                fputc('\n', f);
            }
        }
        for (int y = 0; y < s.map_h; y++) {
            fprintf(f, "cells %d", y);
            for (int x = 0; x < s.map_w; x++) {
                const ct_scene_cell *c = &s.cells[y * CT_SCENE_MAX_CELLS + x];
                fprintf(f, " %u.%u.%u.%02X", c->cls, c->flags, c->plane, c->raw);
            }
            fputc('\n', f);
        }
        for (int k = 0; k < s.n_objects; k++) {
            const ct_scene_object *o = &s.object[k];
            fprintf(f, "object %d %d %d %d %d", k, (int)o->x, (int)o->y, o->z,
                    o->facing == 0xFF ? -1 : o->facing);
            for (int p = 0; p < o->n_pieces; p++)
                fprintf(f, " %d", o->piece[p]);
            fputc('\n', f);
        }
    } else if (ok && s.mode == CT_SCENE_WORLD) {
        /* The same records, at the world map's strides (cell = tile), and
           the overlay: wrap, blend, size and scroll, then its tiles. */
        fprintf(f, "map %u %u cell %u id %u gen %u\n", s.map_w, s.map_h, s.cell_px, s.map_id,
                s.map_generation);
        fprintf(f, "camera %d %d\nplayer %d\n", (int)s.camera_x, (int)s.camera_y,
                s.player == 0xFF ? -1 : s.player);
        fprintf(f, "overlay wrap %u blend %u size %u %u scroll %d %d\n", s.wrap, s.overlay_blend,
                s.overlay_w, s.overlay_h, (int)s.overlay_x, (int)s.overlay_y);
        for (int l = 0; l < CT_SCENE_LAYERS; l++) {
            const ct_scene_layer *L = &s.layer[l];
            fprintf(f, "layer %d present %u bpp %u chr %04X pal %u\n", l, L->present, L->bpp,
                    L->chr_base, L->pal_base);
            int w = l < 2 ? s.map_w : s.overlay_w, h = l < 2 ? s.map_h : s.overlay_h;
            for (int y = 0; L->tiles && y < h; y++) {
                fprintf(f, "tiles %d %d", l, y);
                for (int x = 0; x < w; x++)
                    fprintf(f, " %04X", L->tiles[y * CT_SCENE_WORLD_STRIDE + x]);
                fputc('\n', f);
            }
        }
        for (int y = 0; y < s.map_h; y++) {
            fprintf(f, "cells %d", y);
            for (int x = 0; x < s.map_w; x++) {
                const ct_scene_cell *c = &s.cells[y * CT_SCENE_WORLD_STRIDE + x];
                fprintf(f, " %u.%u.%u.%02X", c->cls, c->flags, c->plane, c->raw);
            }
            fputc('\n', f);
        }
        for (int k = 0; k < s.n_objects; k++) {
            const ct_scene_object *o = &s.object[k];
            fprintf(f, "object %d %d %d %d %d", k, (int)o->x, (int)o->y, o->z,
                    o->facing == 0xFF ? -1 : o->facing);
            for (int p = 0; p < o->n_pieces; p++)
                fprintf(f, " %d", o->piece[p]);
            fputc('\n', f);
        }
    }
    fclose(f);
}

static long dumped, audible;
#define MAX_SHOTS 64
static struct { long frame; const char *path; } shots[MAX_SHOTS];   /* --shot */
static int n_shots;

/* Frame edge: input for the next frame, then this frame's audio, dump
   and hash. */
static void on_frame(long f)
{
    if (profile_from && f + 1 == profile_from)
        sched_profile_clear();   /* the edge where frame F starts */
    if (f == poke_frame)
        bus_wram()[poke_addr] ^= poke_val;
    for (int k = 0; k < n_expect_wram; k++)
        if (f == expect_wram[k].frame) {
            unsigned v = bus_wram()[expect_wram[k].addr];
            expect_wram[k].seen = 1;
            expect_wram[k].ok = expect_wram[k].neq ? v != expect_wram[k].val : v == expect_wram[k].val;
            if (!expect_wram[k].ok)
                printf("ct_boot: frame %ld: WRAM $%05X is $%02X, expected %s$%02X\n", f,
                       expect_wram[k].addr, v, expect_wram[k].neq ? "not " : "", expect_wram[k].val);
        }
    static int16_t audio[2048 * 2];   /* this frame's DSP output, 32 kHz */
    resync_frame(f, sched_frame());
    sched_set_joypad(0, resync_active() ? resync_buttons(&input, f + 1)
                                        : replay_buttons(&input, f + 1));
    for (int p = 1; p < REPLAY_PORTS; p++)   /* port: "pN" script lines */
        sched_set_joypad(p, replay_port_buttons(&input, p, f + 1));
    for (int k = 0; k < n_shots; k++)
        if (shots[k].frame == f && png_write_bgrx(shots[k].path, sched_frame(), SCHED_WIDTH,
                                                  SCHED_HEIGHT))
            fprintf(stderr, "ct_boot: cannot write %s\n", shots[k].path);
        else if (shots[k].frame == f && sched_views() > 1) {   /* port: split screen */
            static uint8_t view2[SCHED_WIDTH * SCHED_HEIGHT * 4];
            char p2[512];
            snprintf(p2, sizeof p2, "%s.view2.png", shots[k].path);
            if (sched_render_view2(view2) && png_write_bgrx(p2, view2, SCHED_WIDTH, SCHED_HEIGHT))
                fprintf(stderr, "ct_boot: cannot write %s\n", p2);
        }
    int n = sched_audio_take(audio, 2048);
    for (int k = 0; k < n * 2; k++)
        if (audio[k]) {
            audible++;
            break;
        }
    if (wav)
        wav_write(wav, audio, n);
    if (rumble_log) {   /* port: simulated rumble */
        static const char *const src[] = {"game", "shake", "flash", "sound", "test"};
        rumble_frame(f, audio, n, SCHED_AUDIO_HZ);
        rumble_ev ev[16];
        int k, got;
        while ((got = rumble_take(ev, 16)) > 0)
            for (k = 0; k < got; k++, rumble_count++)
                fprintf(rumble_log, "rumble frame %ld player %d %s %s %u %u %u\n", ev[k].frame,
                        ev[k].player, src[ev[k].source % 5], ev[k].tag, ev[k].low, ev[k].high,
                        ev[k].ms);
    }
    if (dump && png_dump_frame(dump, f, sched_frame(), SCHED_WIDTH, SCHED_HEIGHT) > 0)
        dumped++;
    if (shot_dir && f % shot_every == 0) {
        char path[4096];
        snprintf(path, sizeof path, "%s/shot_%07ld.png", shot_dir, f);
        if (png_write_bgrx(path, sched_frame(), SCHED_WIDTH, SCHED_HEIGHT) != 0)
            fprintf(stderr, "ct_boot: cannot write %s\n", path);
    }
    if (thumbs)
        write_thumb(thumbs, sched_frame());
    if (hash_log)
        write_state_hashes(hash_log, f);
    if (f == last_frame && (wram_path || vram_path || aram_path || scene_path))
        dump_state();
    if (ref_log)
        fprintf(ref_log, "%ld %016llx %016llx%s\n", f,
                (unsigned long long)fnv(0xCBF29CE484222325ull, bus_wram(), 0x20000),
                (unsigned long long)frame_hash(), sched_frame_in_dma() ? " dma" : "");
}

/* The script's reset button (replay.h "reset F"). */
static int script_reset(long frame)
{
    return replay_reset(&input, frame);
}

int main(int argc, char **argv)
{
    static const char *module_path;
    static long frames = 60, min_nmis;    /* static: survive the longjmp */
    static int require_render, require_audio;
    static const char *wav_path, *hash_path;
    static int profile_top = -1;
    static double min_native = -1;
    static const char *needed;
    static const char *resync_ref, *resync_out;
    static const char *save_path, *load_path;   /* port: save states */
    static long save_frame = -1, roundtrip, roundtrips;
    static long resync_lag;
    static int resync_jobs;
    for (int k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--frames") && k + 1 < argc)
            frames = atol(argv[++k]);
        else if (!strcmp(argv[k], "--dump") && k + 1 < argc)
            dump = argv[++k];
        else if (!strcmp(argv[k], "--shot-every") && k + 2 < argc && atol(argv[k + 1]) > 0) {
            shot_every = atol(argv[++k]);
            shot_dir = argv[++k];
        } else if (!strcmp(argv[k], "--resync") && k + 3 < argc) {
            resync_ref = argv[++k];
            resync_lag = atol(argv[++k]);
            resync_out = argv[++k];
        } else if (!strcmp(argv[k], "--resync-jobs") && k + 1 < argc) {
            resync_jobs = atoi(argv[++k]);
        } else if (!strcmp(argv[k], "--thumbs") && k + 1 < argc) {
            if (!(thumbs = fopen(argv[++k], "wb"))) {
                fprintf(stderr, "ct_boot: cannot write %s\n", argv[k]);
                return 2;
            }
        } else if (!strcmp(argv[k], "--needed-hw") && k + 1 < argc)
            needed = argv[++k];
        else if (!strcmp(argv[k], "--min-nmis") && k + 1 < argc)
            min_nmis = atol(argv[++k]);
        else if (!strcmp(argv[k], "--require-render"))
            require_render = 1;
        else if (!strcmp(argv[k], "--require-audio"))
            require_audio = 1;
        else if (!strcmp(argv[k], "--wav") && k + 1 < argc)
            wav_path = argv[++k];
        else if (!strcmp(argv[k], "--input") && k + 1 < argc) {
            if (replay_add(&input, argv[++k])) {
                fprintf(stderr, "ct_boot: bad --input %s\n", argv[k]);
                return 2;
            }
        } else if (!strcmp(argv[k], "--script") && k + 1 < argc) {
            if (replay_load(&input, argv[++k])) {
                fprintf(stderr, "ct_boot: bad --script %s\n", argv[k]);
                return 2;
            }
        } else if (!strcmp(argv[k], "--aram") && k + 1 < argc) {
            aram_path = argv[++k];
        } else if (!strcmp(argv[k], "--vram") && k + 1 < argc) {
            vram_path = argv[++k];
        } else if (!strcmp(argv[k], "--scene") && k + 1 < argc) {
            scene_path = argv[++k];
        } else if (!strcmp(argv[k], "--wram") && k + 1 < argc) {
            wram_path = argv[++k];
        } else if (!strcmp(argv[k], "--ref-log") && k + 1 < argc) {
            if (!(ref_log = fopen(argv[++k], "w"))) {
                fprintf(stderr, "ct_boot: cannot write %s\n", argv[k]);
                return 2;
            }
        } else if (!strcmp(argv[k], "--hash-log") && k + 1 < argc) {
            hash_path = argv[++k];
        } else if (!strcmp(argv[k], "--min-native") && k + 1 < argc) {
            min_native = atof(argv[++k]);
        } else if (!strcmp(argv[k], "--profile") && k + 1 < argc) {
            char *end;
            profile_top = (int)strtol(argv[++k], &end, 10);
            profile_from = *end == '@' ? strtol(end + 1, NULL, 10) : 0;
        } else if (!strcmp(argv[k], "--watch") && k + 1 < argc && n_watch < MAX_WATCH) {
            unsigned a = 0, n = 2;
            if (sscanf(argv[++k], "%x:%u", &a, &n) < 1 || a > 0x1FFFF || !n || n > 16 ||
                a + n > 0x20000) {
                fprintf(stderr, "ct_boot: bad --watch %s\n", argv[k]);
                return 2;
            }
            watch[n_watch].at = a;
            watch[n_watch++].len = n;
        } else if (!strcmp(argv[k], "--at") && k + 1 < argc && n_at < MAX_AT) {
            at_pc[n_at++] = (uint32_t)strtoul(argv[++k], NULL, 16);
        } else if (!strcmp(argv[k], "--wram-entries") && k + 1 < argc) {
            if (!(wram_entries = fopen(argv[++k], "w")) || !(wram_entry_seen = calloc(0x20000, 1))) {
                fprintf(stderr, "ct_boot: cannot write %s\n", argv[k]);
                return 2;
            }
        } else if (!strcmp(argv[k], "--overlay-strict")) {
            ct_overlay_strict = 1;
        } else if (!strcmp(argv[k], "--overlay-fallback")) {
            ct_overlay_strict = 0;
        } else if (!strcmp(argv[k], "--snap-at") && k + 2 < argc) {
            char *end;
            snap_pc = (uint32_t)strtoul(argv[++k], &end, 16);
            snap_from = *end == '@' ? strtol(end + 1, NULL, 10) : 0;
            snap_dir = argv[++k];
        } else if (!strcmp(argv[k], "--log-entry") && k + 1 < argc && n_log < MAX_LOG) {
            char *end;
            log_pc[n_log] = (uint32_t)strtoul(argv[++k], &end, 16);
            log_mem[n_log++] = *end == ':' ? (int)strtoul(end + 1, NULL, 16) : -1;
        } else if (!strcmp(argv[k], "--first-exec") && k + 1 < argc) {
            if (!(first_exec = fopen(argv[++k], "w")) || !(seen_pc = calloc(1 << 21, 1))) {
                fprintf(stderr, "ct_boot: cannot write %s\n", argv[k]);
                return 2;
            }
        } else if (!strcmp(argv[k], "--trace") && k + 2 < argc) {
            int got = sscanf(argv[++k], "%llu:%llu@%ld", &trace_lo, &trace_hi, &trace_from);
            if (got < 2 || trace_lo > trace_hi) {
                fprintf(stderr, "ct_boot: bad --trace %s\n", argv[k]);
                return 2;
            }
            if (!(trace_out = fopen(argv[++k], "w"))) {
                fprintf(stderr, "ct_boot: cannot write %s\n", argv[k]);
                return 2;
            }
        } else if (!strcmp(argv[k], "--ref-quirk") && k + 1 < argc) {
            if (!strcmp(argv[++k], "mesen-dma-count8"))
                snes_ref_quirks |= SNES_QUIRK_MESEN_DMA_COUNT8;
            else {
                fprintf(stderr, "ct_boot: unknown --ref-quirk %s\n", argv[k]);
                return 2;
            }
        } else if (!strcmp(argv[k], "--save-state") && k + 1 < argc) {   /* port */
            char *end;
            save_frame = strtol(argv[++k], &end, 10);
            if (*end != ':' || !end[1]) {
                fprintf(stderr, "ct_boot: bad --save-state %s\n", argv[k]);
                return 2;
            }
            save_path = end + 1;
        } else if (!strcmp(argv[k], "--load-state") && k + 1 < argc) {
            load_path = argv[++k];
        } else if (!strcmp(argv[k], "--state-roundtrip") && k + 1 < argc && atol(argv[k + 1]) > 0) {
            roundtrip = atol(argv[++k]);
        } else if (!strcmp(argv[k], "--behind")) {
            sched_set_behind(1);   /* port */
        } else if (!strcmp(argv[k], "--prov")) {
            sched_set_prov(1);   /* port: asset packs */
        } else if (!strcmp(argv[k], "--interp-only")) {
            sched_set_native(0);
        } else if (!strcmp(argv[k], "--coop")) {
            sched_set_players(sched_players() > 2 ? sched_players() : 2);   /* port */
        } else if ((!strcmp(argv[k], "--coop-chars") || !strcmp(argv[k], "--coop-chars3")) &&
                   k + 1 < argc) {
            int player = argv[k][12] == '3' ? 2 : 1;   /* port: party slots 1-3 for P2 / P3 */
            for (int slot = 0; player == 1 && slot < 3; slot++)
                sched_set_slot_player(slot, 0);   /* only the listed ones */
            for (const char *c = argv[++k]; *c; c++)
                if (*c >= '1' && *c <= '3')
                    sched_set_slot_player(*c - '1', player);
                else if (*c != ',') {
                    fprintf(stderr, "ct_boot: bad %s %s\n", argv[k - 1], argv[k]);
                    return 2;
                }
            if (player == 2)
                sched_set_players(3);
        } else if (!strcmp(argv[k], "--log-rumble") && k + 1 < argc) {   /* port */
            const char *path = argv[++k];
            if (!(rumble_log = strcmp(path, "-") ? fopen(path, "w") : stdout)) {
                fprintf(stderr, "ct_boot: cannot write %s\n", path);
                return 2;
            }
            rumble_set_enabled(1);
        } else if (!strcmp(argv[k], "--rumble-sound")) {
            rumble_set_detectors(1, 1);
        } else if (!strcmp(argv[k], "--always-run")) {
            sched_set_pref(SCHED_PREF_ALWAYS_RUN, 1);   /* port */
        } else if (!strcmp(argv[k], "--coop-split")) {
            /* port: --coop and the experimental split screen */
            sched_set_players(sched_players() > 2 ? sched_players() : 2);
            sched_set_views(2);
        } else if (!strcmp(argv[k], "--shot") && k + 1 < argc && n_shots < MAX_SHOTS) {
            char *end;
            shots[n_shots].frame = strtol(argv[++k], &end, 10);
            if (*end != ':' || !end[1]) {
                fprintf(stderr, "ct_boot: bad --shot %s\n", argv[k]);
                return 2;
            }
            shots[n_shots++].path = end + 1;
        } else if (!strcmp(argv[k], "--module") && k + 1 < argc) {
            module_path = argv[++k];   /* port: game code from a module */
        } else if (!strcmp(argv[k], "--expect-wram") && k + 1 < argc && n_expect_wram < MAX_EXPECT) {
            long f;
            unsigned a, v;
            char op[3] = {0};
            if (sscanf(argv[++k], "%ld:%x%2[!=]%x", &f, &a, op, &v) != 4 || a > 0x1FFFF || v > 0xFF ||
                (strcmp(op, "=") && strcmp(op, "!="))) {
                fprintf(stderr, "ct_boot: bad --expect-wram %s\n", argv[k]);
                return 2;
            }
            expect_wram[n_expect_wram].frame = f;
            expect_wram[n_expect_wram].addr = a;
            expect_wram[n_expect_wram].val = v;
            expect_wram[n_expect_wram++].neq = op[0] == '!';
        } else if (!strcmp(argv[k], "--log-ppu") && k + 2 < argc) {
            char *end;
            ppu_log_lo = (unsigned)strtoul(argv[++k], &end, 16);
            ppu_log_hi = *end == ':' ? (unsigned)strtoul(end + 1, &end, 16) : ppu_log_lo;
            if (*end == '@')
                ppu_log_from = strtol(end + 1, &end, 10);
            if (*end || ppu_log_lo < 0x2100 || ppu_log_hi > 0x213F || ppu_log_lo > ppu_log_hi ||
                !(ppu_log = fopen(argv[++k], "w"))) {
                fprintf(stderr, "ct_boot: bad --log-ppu %s\n", argv[k]);
                return 2;
            }
            snes_ppu_write_hook = log_ppu_write;
        } else if (!strcmp(argv[k], "--max-stack") && k + 1 < argc) {
            max_stack_allowed = strtol(argv[++k], NULL, 10);
        } else if (!strcmp(argv[k], "--poke-wram") && k + 1 < argc) {
            unsigned a, v;
            if (sscanf(argv[++k], "%ld:%x^%x", &poke_frame, &a, &v) != 3 || poke_frame < 1 ||
                a > 0x1FFFF || !v || v > 0xFF) {
                fprintf(stderr, "ct_boot: bad --poke-wram %s\n", argv[k]);
                return 2;
            }
            poke_addr = a;
            poke_val = (uint8_t)v;
        } else if (!strcmp(argv[k], "--expect-pc") && k + 1 < argc && n_expect < MAX_EXPECT)
            expect_pc[n_expect++] = (uint32_t)strtoul(argv[++k], NULL, 16);
        else {
            fprintf(stderr, "usage: ct_boot [--frames N] [--dump DIR] [--shot-every N DIR] [--thumbs FILE] [--resync REF LAG OUT] [--needed-hw FILE] "
                            "[--min-nmis K] [--require-render] [--wav FILE] "
                            "[--require-audio] [--input F1-F2:BUTTONS] [--script FILE] "
                            "[--expect-pc ADDR] [--expect-wram F:ADDR=V] [--poke-wram F:ADDR^M] [--log-ppu LO:HI[@F] FILE] [--hash-log FILE] [--ref-log FILE] [--wram FILE] [--vram FILE] [--aram FILE] [--scene FILE] [--profile N[@F]] [--min-native P] [--max-stack N] [--watch A[:N]] [--at ADDR] [--log-entry ADDR[:MEM]] [--snap-at ADDR[@F] DIR] [--wram-entries FILE] [--overlay-strict|--overlay-fallback] [--first-exec FILE] [--trace LO:HI[@N] FILE] [--ref-quirk NAME] [--interp-only] [--module FILE] [--behind] [--coop] [--coop-chars L] [--coop-chars3 L] [--coop-split] [--always-run] [--log-rumble FILE] [--rumble-sound] [--shot F:PATH] [--prov] [--save-state F:PATH] [--load-state PATH] [--state-roundtrip N]\n");
            return 2;
        }
    }

    static char stop[512];
    bus_init(NULL);
    if (module_path) {
        const char *err;
        if (ct_module_load(module_path, &err)) {
            fprintf(stderr, "ct_boot: --module: %s\n", err);
            return 2;
        }
        fprintf(stderr, "ct_boot: game module %s: %u functions\n", module_path, ct_func_count);
    }
    interp_reset(&cpu);
    sched_init(&cpu);
    ct_fatal_hook = on_fatal;
    ct_trace_hook = trace;

    if (wav_path && !(wav = wav_open(wav_path, SCHED_AUDIO_HZ))) {
        fprintf(stderr, "ct_boot: cannot write %s\n", wav_path);
        return 2;
    }
    if (hash_path && !(hash_log = fopen(hash_path, "w"))) {
        fprintf(stderr, "ct_boot: cannot write %s\n", hash_path);
        return 2;
    }
    if (resync_ref && resync_init(resync_ref, resync_lag, resync_out, resync_jobs))
        return 2;
    if (setjmp(fatal_jmp)) {
        long f = sched_frame_count();
        resync_fatal(f);
        resync_finish(f);
        int l = sched_line();
        printf("ct_boot: stopped in frame %ld, line %d, PC $%02X%04X: %s\n", f, l, cpu.PB,
               cpu.PC, fatal_msg);
        printf("last instructions:\n");
        for (unsigned k = tail_n > TAIL ? tail_n - TAIL : 0; k < tail_n; k++) {
            const CPU *c = &tail[k % TAIL].c;
            printf("  $%06X A=%04X X=%04X Y=%04X S=%04X DP=%04X DB=%02X m%dx%d\n",
                   tail[k % TAIL].at, c->A, c->X, c->Y, c->S, c->DP, c->DB, c->m, c->x);
        }
        snprintf(stop, sizeof stop, "frame %ld, line %d, PC $%02X%04X: %s", f, l, cpu.PB,
                 cpu.PC, fatal_msg);
        if (needed && hw_needed_write(needed, "ct_boot", stop))
            fprintf(stderr, "ct_boot: cannot write %s\n", needed);
        return 1;
    }
    last_frame = frames;
    sched_set_frame_hook(on_frame);
    sched_set_joypad(0, replay_buttons(&input, 1));
    for (int p = 1; p < REPLAY_PORTS; p++)
        sched_set_joypad(p, replay_port_buttons(&input, p, 1));
    sched_set_reset_hook(script_reset);
    if (load_path) {   /* port: save states */
        const char *why;
        if (savestate_read_file(load_path, &why)) {
            fprintf(stderr, "ct_boot: --load-state %s: %s\n", load_path, why);
            return 2;
        }
        printf("ct_boot: state loaded from %s at frame %ld\n", load_path, sched_frame_count());
    }
    while (sched_frame_count() < frames) {
        sched_run_frame();
        if (save_path && sched_frame_count() >= save_frame && !savestate_ready(NULL)) {
            const char *why;
            if (savestate_write_file(save_path, &why)) {
                fprintf(stderr, "ct_boot: --save-state %s: %s\n", save_path, why);
                return 1;
            }
            printf("ct_boot: state saved at frame %ld to %s\n", sched_frame_count(), save_path);
            save_path = NULL;
        }
        if (roundtrip && sched_frame_count() >= (roundtrips + 1) * roundtrip &&
            !savestate_ready(NULL)) {
            size_t len;
            const char *why = "?";
            uint8_t *blob = savestate_save(&len, &why);
            if (!blob || savestate_load(blob, len, &why)) {
                fprintf(stderr, "ct_boot: --state-roundtrip at frame %ld: %s\n",
                        sched_frame_count(), why);
                return 1;
            }
            free(blob);
            roundtrips++;
        }
    }
    if (roundtrip)
        printf("ct_boot: %ld state round trips\n", roundtrips);
    if (save_path) {
        printf("ct_boot: --save-state: no frame edge from %ld on could take one\n", save_frame);
        return 1;
    }
    resync_finish(sched_frame_count());
    if (wav)
        wav_close(wav);
    if (rumble_log) {   /* port */
        fprintf(stderr, "ct_boot: %ld rumble events\n", rumble_count);
        if (rumble_log != stdout)
            fclose(rumble_log);
        else
            fflush(stdout);
    }
    if (hash_log)
        fclose(hash_log);
    if (thumbs)
        fclose(thumbs);
    if (first_exec)
        fclose(first_exec);
    if (trace_out)
        fclose(trace_out);
    if (wram_entries)
        fclose(wram_entries);
    if (ref_log)
        fclose(ref_log);
    if (ppu_log)
        fclose(ppu_log);
    if ((wram_path || vram_path || scene_path) && !dumped_state)
        dump_state();   /* stopped before the last frame edge (fatal) */
    printf("ct_boot: %ld frames, %ld NMIs, %ld frames dumped, %ld with audio, PC $%02X%04X\n",
           sched_frame_count(), sched_nmi_count(), (long)dumped, (long)audible, cpu.PB, cpu.PC);
    if (require_audio && !audible)
        printf("ct_boot: no audio\n");
    for (unsigned k = 0; k < hw_note_count(); k++)
        printf("ct_boot: stubbed: %s\n", hw_note_text(k));
    uint64_t nat = sched_native_insns(), all = nat + sched_interp_insns();
    double native_pct = all ? 100.0 * (double)nat / (double)all : 0.0;
    if (profile_top >= 0)
        sched_profile_report(stdout, profile_top);
    int stack_ok = 1;
    if (max_stack_allowed >= 0) {
        size_t used = sched_max_stack();
        printf("ct_boot: deepest C stack %zu bytes (limit %ld)\n", used, max_stack_allowed);
        stack_ok = (long)used <= max_stack_allowed;
    }
    if (min_native >= 0 && native_pct < min_native)
        printf("ct_boot: %.2f%% native, below --min-native %.2f\n", native_pct, min_native);
    if (needed && hw_needed_write(needed, "ct_boot", NULL))
        fprintf(stderr, "ct_boot: cannot write %s\n", needed);
    int reached = 1;
    for (int k = 0; k < n_expect; k++)
        if (!expect_hit[k]) {
            printf("ct_boot: $%06X never ran\n", expect_pc[k]);
            reached = 0;
        }
    for (int k = 0; k < n_expect_wram; k++) {
        if (!expect_wram[k].seen)
            printf("ct_boot: frame %ld never reached for --expect-wram\n", expect_wram[k].frame);
        if (!expect_wram[k].ok)
            reached = 0;
    }
    int rendered = nonblack(sched_frame(), (size_t)SCHED_WIDTH * SCHED_HEIGHT * 4);
    if (require_render && !rendered)
        printf("ct_boot: last frame is black\n");
    return sched_nmi_count() >= min_nmis && (rendered || !require_render) &&
           (min_native < 0 || native_pct >= min_native) &&
           (audible || !require_audio) && reached && stack_ok ? 0 : 1;
}
