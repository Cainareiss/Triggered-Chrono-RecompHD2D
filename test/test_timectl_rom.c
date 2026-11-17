/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: rewind on the real game (frontend/timectl.h, timectl_ring.h):
 * boot, play a script, and capture a save state every 4 frames into the
 * ring as the frontend does. Then:
 *   - the ring's states are byte-identical to the ones captured, going
 *     back through all of them (the delta chain is lossless);
 *   - rewinding k captures and playing forward on the same input gives
 *     the original run's state hashes (every frame: the whole save state
 *     where one can be taken, WRAM and the picture always), twice in a
 *     row (the ring goes on across a rewind);
 * and prints what a capture costs (the go/no-go numbers): the save state,
 * the delta, a rewind step.
 *
 * usage: test_timectl_rom SCRIPT START FRAMES [REWIND_CAPTURES] */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bus.h"
#include "interp.h"
#include "replay.h"
#include "savestate.h"
#include "sched.h"
#include "timectl_ring.h"

static jmp_buf fatal_jmp;
static void on_fatal(const char *msg)
{
    fprintf(stderr, "test_timectl_rom: fatal: %s\n", msg);
    longjmp(fatal_jmp, 1);
}

static replay input;
static CPU cpu;

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static uint64_t fnv(uint64_t h, const uint8_t *p, size_t n)
{
    for (size_t k = 0; k < n; k++)
        h = (h ^ p[k]) * 0x100000001B3ull;
    return h;
}

/* The machine at this edge: WRAM, the picture, and the whole save state
   where one can be taken (the part that is the same whenever it is taken). */
static uint64_t state_hash(void)
{
    uint64_t h = fnv(0xCBF29CE484222325ull, bus_wram(), 0x20000);
    h = fnv(h, sched_frame(), (size_t)SCHED_WIDTH * SCHED_HEIGHT * 4);
    size_t len;
    uint8_t *b = savestate_save(&len, NULL);
    if (b) {
        h = fnv(h, b, len);
        free(b);
    }
    return h;
}

static void on_frame(long f)
{
    sched_set_joypad(0, replay_buttons(&input, f + 1));
    for (int p = 1; p < REPLAY_PORTS; p++)
        sched_set_joypad(p, replay_port_buttons(&input, p, f + 1));
}

static int script_reset(long frame) { return replay_reset(&input, frame); }

#define MAXF 4000
static uint64_t hash_at[MAXF];   /* indexed by frame - start */
static long start;

/* One capture, as the frontend's timectl_after_frame does. */
static double t_save, t_push, t_drop, t_load;
static long n_save, n_push, n_step, n_load;
static size_t blob_len;

static int capture(tcr *r, uint8_t **keep, long *kept_n)
{
    const char *why;
    double t0 = now_ms();
    size_t len;
    uint8_t *b = savestate_save(&len, &why);
    double t1 = now_ms();
    if (!b)
        return -1;
    t_save += t1 - t0;
    n_save++;
    blob_len = len;
    if (tcr_push(r, b, len, sched_frame_count())) {
        free(b);
        return -1;
    }
    t_push += now_ms() - t1;
    n_push++;
    if (keep) {
        keep[(*kept_n)++] = b;
    } else {
        free(b);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: test_timectl_rom SCRIPT START FRAMES [REWIND_CAPTURES]\n");
        return 2;
    }
    if (replay_load(&input, argv[1])) {
        fprintf(stderr, "test_timectl_rom: bad script %s\n", argv[1]);
        return 2;
    }
    start = atol(argv[2]);
    long frames = atol(argv[3]);
    int rewind_k = argc > 4 ? atoi(argv[4]) : 20;
    if (frames >= MAXF - 8 || rewind_k < 2) {
        fprintf(stderr, "test_timectl_rom: bad sizes\n");
        return 2;
    }
    bus_init(NULL);
    interp_reset(&cpu);
    sched_init(&cpu);
    ct_fatal_hook = on_fatal;
    if (setjmp(fatal_jmp))
        return 1;
    sched_set_frame_hook(on_frame);
    sched_set_joypad(0, replay_buttons(&input, 1));
    sched_set_reset_hook(script_reset);
    while (sched_frame_count() < start)
        sched_run_frame();

    int fails = 0;
    /* The original run, with the ring filling as it does in play. */
    tcr *ring = tcr_new(100000, 0);   /* the caps are the unit test's; here, all of it */
    static uint8_t *kept[MAXF / 4 + 8];
    long kept_frame[MAXF / 4 + 8], kept_n = 0, last_cap = -100;
    long first = sched_frame_count(), end = start + frames;
    while (sched_frame_count() < end) {
        sched_run_frame();
        long f = sched_frame_count();
        hash_at[f - first] = state_hash();
        if (f - last_cap >= 4 && !savestate_ready(NULL)) {
            kept_frame[kept_n] = f;
            if (capture(ring, kept, &kept_n)) {
                printf("capture failed at frame %ld\n", f);
                return 1;
            }
            last_cap = f;
        }
    }
    printf("original run: %ld frames, %ld captures; state %zu bytes; ring %d states, %zu bytes "
           "(%.1f%% of full)\n",
           frames, kept_n, blob_len, tcr_count(ring), tcr_bytes(ring),
           100.0 * (double)tcr_bytes(ring) / ((double)blob_len * (double)kept_n));

    /* The delta chain is lossless: back through every capture. */
    long idx = kept_n - 1;
    for (;; idx--) {
        size_t len;
        long fr;
        const uint8_t *b = tcr_newest(ring, &len, &fr);
        int ok = b && fr == kept_frame[idx] && len == blob_len && !memcmp(b, kept[idx], len);
        if (!ok) {
            printf("FAIL: ring state %ld (frame %ld) differs from the capture\n", idx,
                   kept_frame[idx]);
            fails++;
            break;
        }
        double t1 = now_ms();
        if (tcr_drop_newest(ring))
            break;
        t_drop += now_ms() - t1;
        n_step++;
    }
    if (idx != 0 && !fails) {
        printf("FAIL: the chain ended at %ld, not 0\n", idx);
        fails++;
    }
    tcr_free(ring);

    /* Rewind and replay, twice. The ring is rebuilt from the captures (as
       it stood at the end of the run); a rewind loads the newest, then
       drops and loads; resuming goes on capturing. */
    ring = tcr_new(100000, 0);
    for (long k = 0; k < kept_n; k++)
        tcr_push(ring, kept[k], blob_len, kept_frame[k]);
    long cur_idx = kept_n - 1;   /* the newest capture is the state we're at: ring newest */
    for (int round = 0; round < 2; round++) {
        /* back rewind_k captures: load the newest, then step */
        long from = sched_frame_count();
        for (int s = 0; s < rewind_k; s++) {
            if (s > 0 && tcr_drop_newest(ring)) {
                printf("FAIL: ring ran out\n");
                return 1;
            }
            size_t len;
            long fr;
            const uint8_t *b = tcr_newest(ring, &len, &fr);
            const char *why = NULL;
            double t0 = now_ms();
            if (savestate_load(b, len, &why)) {
                printf("FAIL: load: %s\n", why);
                return 1;
            }
            t_load += now_ms() - t0;
            n_load++;
        }
        long at = sched_frame_count();
        printf("round %d: rewound from frame %ld to %ld (%d captures)\n", round + 1, from, at,
               rewind_k);
        if (at >= from) {
            printf("FAIL: did not go back\n");
            fails++;
        }
        /* forward on the same input: hashes equal the original run's, and capturing
           continues into the same ring */
        sched_set_joypad(0, replay_buttons(&input, at + 1));
        for (int p = 1; p < REPLAY_PORTS; p++)
            sched_set_joypad(p, replay_port_buttons(&input, p, at + 1));
        long last = at;
        long bad = 0;
        while (sched_frame_count() < from) {
            sched_run_frame();
            long f = sched_frame_count();
            uint64_t h = state_hash();
            if (h != hash_at[f - first]) {
                if (!bad++)
                    printf("FAIL: frame %ld: hash %016llx, the original run had %016llx\n", f,
                           (unsigned long long)h, (unsigned long long)hash_at[f - first]);
            }
            if (f - last >= 4 && !savestate_ready(NULL)) {
                capture(ring, NULL, NULL);
                last = f;
            }
        }
        if (bad) {
            printf("FAIL: round %d: %ld frames differ\n", round + 1, bad);
            fails++;
        }
        (void)cur_idx;
    }
    printf("cost per capture (this machine): savestate_save %.3f ms, delta push %.3f ms "
           "(per 4 frames: %.3f ms a frame)\n",
           t_save / (double)n_save, t_push / (double)n_push,
           (t_save + t_push) / (double)n_save / 4.0);
    printf("cost per rewind step: drop (delta decode) %.3f ms, savestate_load %.3f ms\n",
           t_drop / (double)(n_step > 0 ? n_step : 1), t_load / (double)(n_load > 0 ? n_load : 1));
    printf("test_timectl_rom: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
