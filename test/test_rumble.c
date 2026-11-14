/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: simulated rumble (runtime/rumble.c): the event queue, and the
 * generic detectors on synthetic frame sequences: a screen shake is
 * found and a camera pan is not, a shake over a pan is found when the
 * camera is known, a flash on a lit screen is found and a fade-in (or a
 * screen switched on from black) is not, a quiet context mutes them, and
 * the sound detector hears a low boom but not a high tone. */
#include <math.h>

#include "harness.h"
#include "rumble.h"

static rumble_frame_in lit(void)
{
    rumble_frame_in in;
    memset(&in, 0, sizeof in);
    in.brightness = 15;
    in.luma = 300;
    return in;
}

/* Count the events of one source a sequence raises. */
typedef void (*frame_fn)(int f, rumble_frame_in *in);

static int run(frame_fn fn, int frames, int ctx, int src, int *first, uint16_t *peak)
{
    rumble_detect d;
    memset(&d, 0, sizeof d);
    rumble_detect_reset(&d);
    int count = 0;
    if (first)
        *first = -1;
    if (peak)
        *peak = 0;
    for (int f = 0; f < frames; f++) {
        rumble_frame_in in = lit();
        fn(f, &in);
        rumble_ev ev[4];
        int n = rumble_detect_step(&d, &in, ctx, f, ev, 4);
        for (int k = 0; k < n; k++) {
            CHECK(ev[k].player == RUMBLE_ALL && ev[k].ms > 0 && ev[k].ms <= 200 &&
                      ev[k].frame == f, "event %s at %d", ev[k].tag, f);
            if (ev[k].source != src)
                continue;
            if (first && *first < 0)
                *first = f;
            if (peak && ev[k].low > *peak)
                *peak = ev[k].low;
            count++;
        }
    }
    return count;
}

/* BG1 steps back and forth 3 px (frames 20-50), else still. */
static void shake(int f, rumble_frame_in *in)
{
    in->hofs[0] = (uint16_t)(100 + (f >= 20 && f < 50 ? (f & 1 ? 3 : -3) : 0));
    in->vofs[0] = 40;
}

static void shake_big(int f, rumble_frame_in *in)
{
    in->vofs[0] = (uint16_t)(f >= 20 && f < 50 ? (f & 1 ? 8 : 0) : 0);
}

/* A camera walking right 2 px a frame, and stopping, and back left. */
static void pan(int f, rumble_frame_in *in)
{
    int x = f < 40 ? 2 * f : f < 60 ? 80 : 80 - (f - 60);
    in->hofs[0] = (uint16_t)(x & 0x3FF);
    in->vofs[0] = (uint16_t)(f / 3);   /* and a slow drift down */
}

/* A walk that starts and stops every few frames (no reversal). */
static void stop_go(int f, rumble_frame_in *in)
{
    in->hofs[0] = (uint16_t)((f / 4) * 3 + ((f & 3) == 3 ? 1 : 0));
}

/* A pan with a shake over it, and the camera known. */
static void pan_shake(int f, rumble_frame_in *in)
{
    int cam = 2 * f;
    in->has_camera = 1;
    in->camera_x = cam;
    in->hofs[0] = (uint16_t)((cam + (f & 1 ? 2 : -2)) & 0x3FF);
}

/* The same without the camera: a shake of 2 px over a 2 px/frame pan is
   steps of 6 and -2: no reversal is lost, but the net movement is too
   big for a shake, and nothing may fire. */
static void pan_shake_blind(int f, rumble_frame_in *in)
{
    pan_shake(f, in);
    in->has_camera = 0;
}

/* A map change: the scroll jumps once. */
static void cut(int f, rumble_frame_in *in)
{
    in->hofs[0] = (uint16_t)(f < 30 ? 10 : 500);
    in->vofs[0] = (uint16_t)(f < 30 ? 0 : 300);
}

/* The palette goes to white for 2 frames at 30. */
static void flash(int f, rumble_frame_in *in)
{
    if (f == 30 || f == 31)
        in->luma = 950;
}

/* The fixed colour added at 40 (colour math), brightness steady. */
static void flash_fixed(int f, rumble_frame_in *in)
{
    if (f >= 40 && f < 44)
        in->fixed_add = 600;
}

/* A fade in from black: brightness 0 to 15 over 15 frames. */
static void fade_in(int f, rumble_frame_in *in)
{
    in->brightness = (uint8_t)(f < 15 ? f : 15);
    in->luma = 600;
}

/* Forced blank until 20, then the whole picture at once. */
static void switch_on(int f, rumble_frame_in *in)
{
    in->brightness = (uint8_t)(f < 20 ? 0 : 15);
    in->luma = 800;
}

/* The palette brightening slowly (a sunrise). */
static void slow(int f, rumble_frame_in *in)
{
    in->luma = (uint16_t)(200 + 10 * f);
}

/* The sound: quiet, a 50 Hz boom at frame 60, or a 2 kHz beep. */
static int sound_run(double hz, double amp)
{
    rumble_detect d;
    memset(&d, 0, sizeof d);
    d.sound = 1;
    rumble_detect_reset(&d);
    CHECK(d.sound == 1, "reset keeps the sound detector on");
    int16_t buf[534 * 2];
    int count = 0;
    double phase = 0;
    for (int f = 0; f < 120; f++) {
        double a = f >= 60 && f < 66 ? amp : 0.01;
        for (int k = 0; k < 534; k++) {
            int16_t v = (int16_t)(32767.0 * a * sin(phase));
            phase += 2 * 3.14159265358979 * (f >= 60 && f < 66 ? hz : 50.0) / 32040.0;
            buf[2 * k] = buf[2 * k + 1] = v;
        }
        rumble_sound_feed(&d, buf, 534, 32040);
        rumble_frame_in in = lit();
        in.has_sound = 1;
        in.bass = d.acc_n ? (float)sqrt(d.acc / d.acc_n) : 0;
        d.acc = 0;
        d.acc_n = 0;
        rumble_ev ev[4];
        int n = rumble_detect_step(&d, &in, RUMBLE_CTX_PLAY, f, ev, 4);
        for (int k = 0; k < n; k++)
            if (ev[k].source == RUMBLE_SRC_SOUND) {
                CHECK(f >= 60 && f < 70 && ev[k].high == 0, "boom at %d", f);
                count++;
            }
    }
    return count;
}

static void queue_tests(void)
{
    rumble_ev ev[80];
    rumble_set_enabled(0);
    rumble_event(1000, 1000, 100);
    CHECK(rumble_take(ev, 80) == 0, "nothing queued while off");
    rumble_set_enabled(1);
    CHECK(rumble_enabled(), "on");
    rumble_event(1000, 2000, 100);
    rumble_event_for(2, 3000, 0, 50, "hurt");
    rumble_event_for(1, 0, 0, 50, "zero");   /* no strength: dropped */
    rumble_event_for(9, 10, 10, 50, NULL);   /* out of range: everyone */
    int n = rumble_take(ev, 80);
    CHECK(n == 3, "three queued, got %d", n);
    CHECK(ev[0].player == RUMBLE_ALL && ev[0].low == 1000 && ev[0].high == 2000 && ev[0].ms == 100 &&
              !strcmp(ev[0].tag, "event"), "rumble_event");
    CHECK(ev[1].player == 2 && ev[1].low == 3000 && !strcmp(ev[1].tag, "hurt"), "for a player");
    CHECK(ev[2].player == RUMBLE_ALL, "player 9 is everyone");
    CHECK(rumble_take(ev, 80) == 0, "drained");
    for (int k = 0; k < 70; k++)
        rumble_event_for(0, (uint16_t)(k + 1), 0, 10, "n");
    n = rumble_take(ev, 80);
    CHECK(n == 64 && ev[0].low == 7 && ev[63].low == 70, "the oldest dropped: %d, %u", n,
          ev[0].low);
    rumble_event(5, 5, 10);
    rumble_set_enabled(0);
    CHECK(rumble_take(ev, 80) == 0, "turning it off empties the queue");
}

int main(void)
{
    queue_tests();
    int first;
    uint16_t peak, peak_big;
    CHECK(run(shake, 80, RUMBLE_CTX_PLAY, RUMBLE_SRC_SHAKE, &first, &peak) >= 3,
          "shake found");
    CHECK(first >= 24 && first <= 32, "shake found early: frame %d", first);
    CHECK(run(shake_big, 80, RUMBLE_CTX_UNKNOWN, RUMBLE_SRC_SHAKE, NULL, &peak_big) >= 3,
          "big vertical shake found");
    CHECK(peak_big > peak, "a bigger shake is stronger: %u vs %u", peak_big, peak);
    {   /* it stops soon after the shake does */
        rumble_detect d;
        memset(&d, 0, sizeof d);
        int last = -1;
        for (int f = 0; f < 120; f++) {
            rumble_frame_in in = lit();
            shake(f, &in);
            rumble_ev ev[4];
            if (rumble_detect_step(&d, &in, RUMBLE_CTX_PLAY, f, ev, 4))
                last = f;
        }
        CHECK(last >= 45 && last < 58, "shake ends: last pulse %d", last);
    }
    CHECK(run(pan, 120, RUMBLE_CTX_PLAY, RUMBLE_SRC_SHAKE, NULL, NULL) == 0, "a pan isn't a shake");
    CHECK(run(stop_go, 120, RUMBLE_CTX_PLAY, RUMBLE_SRC_SHAKE, NULL, NULL) == 0,
          "stop and go isn't a shake");
    CHECK(run(pan_shake, 80, RUMBLE_CTX_PLAY, RUMBLE_SRC_SHAKE, NULL, NULL) >= 3,
          "a shake over a pan, camera known");
    CHECK(run(pan_shake_blind, 80, RUMBLE_CTX_PLAY, RUMBLE_SRC_SHAKE, NULL, NULL) == 0,
          "a shaking pan without the camera: not taken for a shake");
    CHECK(run(cut, 80, RUMBLE_CTX_PLAY, RUMBLE_SRC_SHAKE, NULL, NULL) == 0, "a cut isn't a shake");
    CHECK(run(shake, 80, RUMBLE_CTX_QUIET, RUMBLE_SRC_SHAKE, NULL, NULL) == 0, "quiet: no shake");

    CHECK(run(flash, 80, RUMBLE_CTX_PLAY, RUMBLE_SRC_FLASH, &first, NULL) == 1 && first == 30,
          "palette flash at 30 (%d)", first);
    CHECK(run(flash_fixed, 80, RUMBLE_CTX_PLAY, RUMBLE_SRC_FLASH, &first, NULL) == 1 && first == 40,
          "fixed colour flash at 40 (%d)", first);
    CHECK(run(fade_in, 80, RUMBLE_CTX_PLAY, RUMBLE_SRC_FLASH, NULL, NULL) == 0, "a fade-in isn't");
    CHECK(run(switch_on, 80, RUMBLE_CTX_PLAY, RUMBLE_SRC_FLASH, NULL, NULL) == 0,
          "the screen switched on isn't");
    CHECK(run(slow, 80, RUMBLE_CTX_PLAY, RUMBLE_SRC_FLASH, NULL, NULL) == 0, "a slow sunrise isn't");
    CHECK(run(flash, 80, RUMBLE_CTX_QUIET, RUMBLE_SRC_FLASH, NULL, NULL) == 0, "quiet: no flash");
    CHECK(run(shake, 80, RUMBLE_CTX_PLAY, RUMBLE_SRC_FLASH, NULL, NULL) == 0, "a shake isn't a flash");

    CHECK(sound_run(50.0, 0.7) == 1, "a low boom");
    CHECK(sound_run(2000.0, 0.7) == 0, "a high beep isn't");
    CHECK(sound_run(50.0, 0.02) == 0, "a quiet hum isn't");
    {   /* off by default: no sound events however loud */
        rumble_detect d;
        memset(&d, 0, sizeof d);
        int16_t buf[1000] = {0};
        rumble_sound_feed(&d, buf, 500, 32040);
        CHECK(d.acc_n == 0, "the sound detector is off by default");
    }
    return th_report("rumble");
}
