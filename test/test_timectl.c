/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: time controls (frontend/timectl.h, timectl_ring.h) without a ROM:
 * the delta on synthetic blobs, the ring's capacity and memory cap, the
 * speed steps and the pacing schedule (which frames run in a given wall
 * time), the settings, the actions and their keys, the refusal to rewind
 * while recording, and the frame step. The real game is
 * test_timectl_rom.c (LABELS rom). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actions.h"
#include "bindings.h"
#include "settings.h"
#include "timectl.h"
#include "timectl_ring.h"

static long checks, fails;
#define CHECK(c, ...)                                             \
    do {                                                          \
        checks++;                                                 \
        if (!(c)) {                                               \
            fails++;                                              \
            fprintf(stderr, "%s:%d: FAIL: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                         \
            fputc('\n', stderr);                                  \
        }                                                         \
    } while (0)

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return (uint32_t)((rng * 0x2545F4914F6CDD1Dull) >> 32);
}

/* A blob like a save state: mostly the same from one to the next, with a
   few changed spans. */
static uint8_t *make(size_t n, int seed_changes, const uint8_t *base)
{
    uint8_t *b = malloc(n ? n : 1);
    if (base)
        memcpy(b, base, n);
    else
        for (size_t k = 0; k < n; k++)
            b[k] = (uint8_t)(k % 7 ? 0 : rnd());
    for (int c = 0; c < seed_changes && n; c++) {
        size_t at = rnd() % n, len = 1 + rnd() % 300;
        for (size_t k = at; k < at + len && k < n; k++)
            b[k] = (uint8_t)rnd();
    }
    return b;
}

static void delta_roundtrip(size_t nl, size_t ol, int changes)
{
    size_t m = nl > ol ? nl : ol;
    uint8_t *nw = make(nl, 0, NULL);
    uint8_t *older = make(ol, changes, ol <= nl ? nw : NULL);
    if (ol > nl) {   /* older is longer: its extra tail is its own */
        free(older);
        older = make(ol, 0, NULL);
        memcpy(older, nw, nl);
        for (size_t k = nl; k < ol; k++)
            older[k] = (uint8_t)rnd();
    }
    size_t cap = tcr_delta_max(nl, ol);
    uint8_t *d = malloc(cap);
    size_t dn = tcr_delta_enc(nw, nl, older, ol, d, cap);
    CHECK(dn > 0 || (!nl && !ol), "encode %zu/%zu gave nothing", nl, ol);
    uint8_t *buf = malloc(m ? m : 1);
    memcpy(buf, nw, nl);
    size_t got = tcr_delta_dec(buf, nl, ol, d, dn);
    CHECK(got == ol && !memcmp(buf, older, ol), "round trip %zu -> %zu, %d changes", nl, ol,
          changes);
    if (changes <= 3 && nl == ol && nl > 10000)
        CHECK(dn < nl / 20, "a delta with %d changes is small (%zu of %zu)", changes, dn, nl);
    /* a damaged delta is refused, not run off the end of the buffer */
    if (dn > 4) {
        d[1] = 0xFF;
        d[2] = 0xFF;
        d[3] = 0x7F;
        memcpy(buf, nw, nl);
        (void)tcr_delta_dec(buf, nl, ol, d, dn);   /* must not crash (ASan would say) */
    }
    free(nw);
    free(older);
    free(d);
    free(buf);
}

static void test_delta(void)
{
    static const size_t sizes[] = {1, 7, 8, 9, 100, 4096, 60000, 600000};
    for (size_t a = 0; a < sizeof sizes / sizeof *sizes; a++)
        for (int ch = 0; ch < 6; ch += 2)
            delta_roundtrip(sizes[a], sizes[a], ch);
    delta_roundtrip(1000, 1200, 2);   /* another size: the tail is the longer one's own */
    delta_roundtrip(1200, 1000, 2);
    delta_roundtrip(100000, 99990, 5);
    /* identical: an empty-ish delta */
    uint8_t a[5000];
    memset(a, 0x5A, sizeof a);
    uint8_t d[tcr_delta_max(5000, 5000)];
    size_t dn = tcr_delta_enc(a, sizeof a, a, sizeof a, d, sizeof d);
    CHECK(dn <= 4, "identical blobs: %zu bytes", dn);
    /* nothing in common */
    uint8_t b[5000];
    for (size_t k = 0; k < sizeof b; k++)
        b[k] = (uint8_t)(a[k] ^ (1 + rnd() % 255));
    dn = tcr_delta_enc(a, sizeof a, b, sizeof b, d, sizeof d);
    CHECK(dn > 0 && dn <= tcr_delta_max(5000, 5000), "all different fits (%zu)", dn);
    uint8_t buf[5000];
    memcpy(buf, a, sizeof a);
    CHECK(tcr_delta_dec(buf, sizeof a, sizeof b, d, dn) == sizeof b && !memcmp(buf, b, sizeof b),
          "all different round trip");
}

static void test_ring(void)
{
    enum { N = 40, SZ = 20000 };
    uint8_t *blob[N];
    blob[0] = make(SZ, 0, NULL);
    for (int k = 1; k < N; k++)
        blob[k] = make(SZ, 3, blob[k - 1]);

    /* all of them, then back: each comes out exactly as it went in */
    tcr *r = tcr_new(100, 0);
    for (int k = 0; k < N; k++)
        CHECK(tcr_push(r, blob[k], SZ, 100 + 4L * k) == 0, "push %d", k);
    CHECK(tcr_count(r) == N, "count %d", tcr_count(r));
    CHECK(tcr_bytes(r) < (size_t)SZ * 4, "40 near-equal states take %zu bytes", tcr_bytes(r));
    for (int k = N - 1; k >= 0; k--) {
        size_t len;
        long fr;
        const uint8_t *b = tcr_newest(r, &len, &fr);
        CHECK(b && len == SZ && fr == 100 + 4L * k && !memcmp(b, blob[k], SZ), "state %d back", k);
        if (k)
            CHECK(tcr_drop_newest(r) == 0, "drop %d", k);
    }
    CHECK(tcr_count(r) == 1, "one left");
    CHECK(tcr_drop_newest(r) == -1 && tcr_count(r) == 1, "the last one stays");
    tcr_free(r);

    /* the entry cap: the oldest go, the newest stay, and they still decode */
    r = tcr_new(10, 0);
    for (int k = 0; k < N; k++)
        tcr_push(r, blob[k], SZ, k);
    CHECK(tcr_count(r) == 10, "capped at 10 entries: %d", tcr_count(r));
    for (int k = N - 1; k >= N - 10; k--) {
        size_t len;
        long fr;
        const uint8_t *b = tcr_newest(r, &len, &fr);
        CHECK(b && fr == k && !memcmp(b, blob[k], SZ), "capped ring: state %d", k);
        if (k > N - 10)
            tcr_drop_newest(r);
    }
    for (int k = 0; k < N; k++)
        tcr_push(r, blob[k], SZ, k);
    tcr_limits(r, 4, 0);   /* the setting went down */
    CHECK(tcr_count(r) == 4, "limits shrinks: %d", tcr_count(r));
    tcr_clear(r);
    CHECK(tcr_count(r) == 0 && tcr_bytes(r) == 0 && !tcr_newest(r, NULL, NULL), "clear");
    tcr_free(r);

    /* the memory cap: random states (nothing in common) cost a full copy each */
    enum { BIG = 100000 };
    size_t cap = 32u << 20;   /* the frontend's cap */
    r = tcr_new(100000, cap);
    uint8_t *rb = malloc(BIG);
    for (int k = 0; k < 2000; k++) {
        for (size_t j = 0; j < BIG; j++)
            rb[j] = (uint8_t)rnd();
        CHECK(tcr_push(r, rb, BIG, k) == 0, "push random %d", k);
        if (tcr_bytes(r) > cap) {
            CHECK(0, "ring is %zu bytes, over the cap", tcr_bytes(r));
            break;
        }
    }
    int held = tcr_count(r);
    CHECK(held > 100 && held < 400, "under the cap the ring holds %d states of %d bytes", held, BIG);
    CHECK(tcr_newest(r, NULL, NULL) != NULL, "the newest is always kept");
    long fr = -1;
    tcr_newest(r, NULL, &fr);
    CHECK(fr == 1999, "the newest is the last pushed (%ld)", fr);
    /* a cap smaller than one state: the newest stays alone */
    tcr_limits(r, 100, 10);
    CHECK(tcr_count(r) == 1, "a cap under one state keeps the newest: %d", tcr_count(r));
    free(rb);
    tcr_free(r);
    for (int k = 0; k < N; k++)
        free(blob[k]);

    /* rewind_seconds to entries: 5 s is about 76 captures at 4 frames, off is none */
    CHECK(timectl_ring_entries(0) == 0, "off");
    int e5 = timectl_ring_entries(5);
    CHECK(e5 >= 5 * 60 / 4 && e5 <= 5 * 62 / 4 + 2, "5 s: %d entries", e5);
}

static void test_schedule(void)
{
    static const int pcts[] = {25, 50, 100, 200, 300, 400};
    const uint64_t freq = 1000000000ull;   /* a ns counter */
    for (size_t k = 0; k < sizeof pcts / sizeof *pcts; k++) {
        /* frames that run in 10 s of wall time, pacing the way main.c does:
           next_frame += one frame's ticks, a frame runs while it is due */
        uint64_t next = 0, tick = timectl_frame_ticks(freq, pcts[k]);
        long run = 0;
        while (next + tick <= 10 * freq) {
            next += tick;
            run++;
        }
        double want = 10.0 * 60.0988 * pcts[k] / 100.0;
        CHECK(run >= (long)want - 1 && run <= (long)want + 1, "%d%%: %ld frames in 10 s, want %.1f",
              pcts[k], run, want);
        /* the picture: every frame under 200%, one in pct/100 from there */
        long shown = 0;
        for (long f = 0; f < 1200; f++)
            shown += timectl_show_frame(pcts[k], f);
        long want_shown = pcts[k] < 200 ? 1200 : 1200 / (pcts[k] / 100);
        CHECK(shown == want_shown, "%d%%: %ld of 1200 frames drawn, want %ld", pcts[k], shown,
              want_shown);
    }
    /* a 10 MHz counter (Windows) gives the same */
    CHECK(timectl_frame_ticks(10000000ull, 100) == 166393, "10 MHz at 100%%: %llu",
          (unsigned long long)timectl_frame_ticks(10000000ull, 100));
    CHECK(timectl_frame_ticks(freq, 50) == 2 * timectl_frame_ticks(freq, 100), "half speed, twice the ticks");
    /* which frames run in the first 100 ms at 25%: frame 1 at 66.5 ms, none else */
    uint64_t t = timectl_frame_ticks(freq, 25), n = 0;
    for (uint64_t at = t; at <= 100000000ull; at += t)
        n++;
    CHECK(n == 1, "25%%: %llu frames in 100 ms", (unsigned long long)n);
    /* fast-forward multiplies the base */
    CHECK(timectl_pace_pct(1) == timectl_speed_pct(), "no fast-forward: the base");
}

static int osd_n, paused_now, toggled, loaded, restarts, pace_resets;
static char osd_last[96];
static void h_osd(const char *m) { osd_n++; snprintf(osd_last, sizeof osd_last, "%s", m); }
static int h_paused(void) { return paused_now; }
static void h_toggle(void) { toggled++; paused_now = !paused_now; }
static void h_load(void) { loaded++; }
static void h_audio(void) { restarts++; }
static void h_pace(void) { pace_resets++; }
static int saves;
static void h_save(void) { saves++; }

static void test_controls(void)
{
    timectl_set_host(&(timectl_host){h_audio, h_pace, h_osd, h_paused, h_toggle, h_load, 0});
    settings_on_change(h_save);
    timectl_init();

    CHECK(settings_find("slow_speed") && settings_find("rewind_seconds"), "settings registered");
    CHECK(timectl_speed_pct() == 100 && !timectl_audio_muted(), "starts at 100%%, with sound");
    CHECK(settings_get_value(settings_find("rewind_seconds")) == 5, "rewind: 5 s by default");

    timectl_speed_step(-1);
    CHECK(timectl_speed_pct() == 50 && timectl_audio_muted(), "step down: 50%%, muted");
    CHECK(saves == 1 && restarts == 1 && pace_resets == 1, "saved, sound flushed, timer reset");
    timectl_speed_step(-1);
    timectl_speed_step(-1);
    CHECK(timectl_speed_pct() == 25, "stops at 25%%");
    timectl_speed_step(1);
    timectl_speed_step(1);
    timectl_speed_step(1);
    timectl_speed_step(1);
    timectl_speed_step(1);
    timectl_speed_step(1);
    CHECK(timectl_speed_pct() == 400, "stops at 400%%");
    timectl_speed_set(130);
    CHECK(timectl_speed_pct() == 100 && !timectl_audio_muted(), "130 snaps to 100");
    timectl_speed_set(60);
    CHECK(timectl_speed_pct() == 50, "60 snaps to 50");
    CHECK(timectl_pace_pct(3) == 150 && timectl_pace_pct(1) == 50, "fast-forward 3x on 50%% is 150%%");
    CHECK(settings_parse_line("slow_speed=25\n") && timectl_speed_pct() == 25, "slow_speed=25 reads");
    CHECK(settings_parse_line("slow_speed=400\n") && timectl_speed_pct() == 400, "slow_speed=400 reads");
    timectl_speed_set(100);
    CHECK(settings_parse_line("rewind_seconds=12\n") &&
              settings_get_value(settings_find("rewind_seconds")) == 12, "rewind_seconds=12 reads");

    /* no states yet: nothing to rewind (and it says so) */
    osd_n = 0;
    timectl_rewind(1);
    CHECK(!timectl_rewinding() && osd_n == 1 && strstr(osd_last, "Nothing"), "empty ring: '%s'", osd_last);

    /* off: says so */
    settings_parse_line("rewind_seconds=0\n");
    osd_n = 0;
    timectl_rewind(1);
    CHECK(!timectl_rewinding() && osd_n == 1 && strstr(osd_last, "off"), "rewind off: '%s'", osd_last);
    settings_parse_line("rewind_seconds=5\n");

    /* recording: refused, with the message */
    timectl_set_host(&(timectl_host){h_audio, h_pace, h_osd, h_paused, h_toggle, h_load, 1});
    osd_n = 0;
    timectl_rewind(1);
    CHECK(!timectl_rewinding() && osd_n == 1 && strstr(osd_last, "recording"),
          "refused while recording: '%s'", osd_last);
    actions_emit(ACTION_REWIND, 1);   /* the action does the same */
    actions_emit(ACTION_REWIND, 0);
    CHECK(!timectl_rewinding() && osd_n == 2, "the action is refused too");
    timectl_set_host(&(timectl_host){h_audio, h_pace, h_osd, h_paused, h_toggle, h_load, 0});

    /* frame step: needs a pause; not paused, it pauses and the next press steps */
    paused_now = 0;
    CHECK(!timectl_paused_step_pending(), "no step pending");
    actions_emit(ACTION_FRAMESTEP, 1);
    CHECK(toggled == 1 && paused_now && !timectl_paused_step_pending(), "unpaused: pauses first");
    actions_emit(ACTION_FRAMESTEP, 1);
    CHECK(timectl_paused_step_pending(), "paused: one step pending");
    actions_emit(ACTION_FRAMESTEP, 1);   /* a second press before the frame ran: still just one */
    CHECK(timectl_paused_step_pending(), "still one");
    timectl_step_taken();
    CHECK(!timectl_paused_step_pending() && timectl_audio_muted(), "taken: none pending, the frame is silent");

    /* the keys and the hold */
    bindings b;
    bindings_defaults(&b);
    CHECK(bindings_for_key(&b, SDL_SCANCODE_PERIOD) == ACTION_FRAMESTEP, "period is the step");
    CHECK(bindings_for_key(&b, SDL_SCANCODE_BACKSPACE) == ACTION_REWIND, "Backspace is the rewind");
    CHECK(actions_is_hold(ACTION_REWIND) && !actions_is_hold(ACTION_FRAMESTEP), "rewind holds");
    CHECK(action_parse("rewind") == ACTION_REWIND && action_parse("framestep") == ACTION_FRAMESTEP,
          "names parse");
    CHECK(bindings_parse_line(&b, "bind_touchpad=rewind\n") &&
              bindings_for_button(&b, SDL_CONTROLLER_BUTTON_TOUCHPAD) == ACTION_REWIND,
          "a button can be bound to it");
}

int main(void)
{
    test_delta();
    test_ring();
    test_schedule();
    test_controls();
    printf("test_timectl: %ld checks, %ld failed\n", checks, fails);
    return fails ? 1 : 0;
}
