/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: simulated rumble (docs/USER-GUIDE.md, "Rumble"). The console had
 * no rumble, so pulses are made from what the game does: a game module's
 * patches and frame hook call rumble_event (they know the game: a party
 * member hit, an enemy dying, an earthquake), and generic detectors watch
 * the console's output for any game (rumble_detect: the screen shaking, a
 * full-screen flash, a low boom in the sound).
 *
 * Nothing here touches the machine: events go into a queue that the
 * frontend drains at the frame edge (rumble_take) and plays on the
 * players' controllers. A run with rumble on is the same run, frame for
 * frame, as one with it off. */
#ifndef CT_RUMBLE_H
#define CT_RUMBLE_H

#include <stdint.h>

#define RUMBLE_ALL (-1)   /* player: every player's controller */

enum {                    /* rumble_ev.source */
    RUMBLE_SRC_GAME,      /* the game module (its patches or frame hook) */
    RUMBLE_SRC_SHAKE,     /* detector: the picture shaking */
    RUMBLE_SRC_FLASH,     /* detector: a full-screen flash */
    RUMBLE_SRC_SOUND,     /* detector: a low-frequency burst in the sound */
    RUMBLE_SRC_TEST,      /* the frontend's test pulse */
};

typedef struct {
    long frame;           /* the frame it was raised in */
    int8_t player;        /* 0-3, or RUMBLE_ALL */
    uint8_t source;       /* RUMBLE_SRC_* */
    uint16_t low, high;   /* motor strengths, SDL's scale: 0-65535 */
    uint16_t ms;          /* how long; the frontend fades it out over that */
    char tag[14];         /* what it was ("hurt", "shake"), for --log-rumble */
} rumble_ev;

/* Off by default: with it off rumble_event returns at once, so a game's
   patches cost nothing. */
void rumble_set_enabled(int on);
int rumble_enabled(void);

/* The API: a pulse of low (the heavy motor) and high (the light one)
   strength for ms, on every player's controller. */
void rumble_event(uint16_t low, uint16_t high, uint32_t ms);
/* The same, for one player (0-3: sched_slot_player gives a party member's;
   RUMBLE_ALL every one), with a tag saying what it is. */
void rumble_event_for(int player, uint16_t low, uint16_t high, uint32_t ms, const char *tag);
/* Events raised since the last call, oldest first (at most max; the queue
   holds 64 and drops the oldest beyond that). Returns how many. */
int rumble_take(rumble_ev *out, int max);

/* ---- the module's side ----
   A game module may export a frame hook (module.h ct_module_info.rumble):
   called once per frame at the frame edge while rumble is on, it may raise
   events of its own and says what the game is showing, for the detectors. */
enum {
    RUMBLE_CTX_QUIET = 0,   /* menus, text, title, maps: the detectors stay still */
    RUMBLE_CTX_PLAY = 1,    /* the game proper (fields, battles): they may pulse */
    RUMBLE_CTX_UNKNOWN = 2, /* no hook: the detectors decide alone */
};
typedef int (*ct_module_rumble_fn)(long frame);
void ct_rumble_set_source(ct_module_rumble_fn fn);

/* ---- the generic detectors ---- */
#define RUMBLE_HISTORY 16

/* What the console shows at a frame edge (rumble_capture fills it). */
typedef struct {
    uint16_t hofs[3], vofs[3];   /* BG1-3 scroll */
    int has_camera;              /* camera_x/y valid (the module's scene) */
    int32_t camera_x, camera_y;  /* where the game's camera is, px */
    uint8_t brightness;          /* 0-15, 0 also in forced blank */
    uint16_t luma;               /* mean CGRAM luminance, 0-1023 */
    uint16_t fixed_add;          /* the fixed colour added over the picture, 0-1023 (0: none) */
    int has_sound;               /* bass valid */
    float bass;                  /* low-frequency energy this frame (rumble_sound_feed) */
} rumble_frame_in;

typedef struct {
    int n;                                        /* frames seen (saturates) */
    int16_t dx[RUMBLE_HISTORY], dy[RUMBLE_HISTORY];   /* BG1 scroll steps, camera removed */
    uint16_t px, py;
    int32_t pcx, pcy;
    float level[RUMBLE_HISTORY];                  /* the picture's light, 0-2 */
    uint8_t bright[RUMBLE_HISTORY];
    int shake_cool, flash_cool, sound_cool;
    float bass_avg;                               /* the sound's running bass level */
    float lp1, lp2;                               /* rumble_sound_feed's filter state */
    double acc;
    int acc_n;
    int sound;                                    /* the sound detector is on */
} rumble_detect;

void rumble_detect_reset(rumble_detect *d);
/* One frame. ctx RUMBLE_CTX_QUIET keeps every detector still (and
   forgets the history). Writes up to max events (source SHAKE, FLASH or
   SOUND, player RUMBLE_ALL) to out and returns how many. */
int rumble_detect_step(rumble_detect *d, const rumble_frame_in *in, int ctx, long frame,
                       rumble_ev *out, int max);
/* The sound detector's input: this frame's DSP output, interleaved
   stereo at rate Hz. */
void rumble_sound_feed(rumble_detect *d, const int16_t *stereo, int frames, int rate);
/* The console's state now (call at the frame edge). */
void rumble_capture(rumble_frame_in *in);

/* At the frame edge, while rumble is on: the module's hook, then the
   detectors on the console's state and this frame's sound (NULL: none),
   their events queued with the game's. The engine programs call it from
   their frame hook. */
void rumble_frame(long frame, const int16_t *stereo, int frames, int rate);
/* The detectors rumble_frame runs (the sound one is off by default). */
void rumble_set_detectors(int on, int sound);

#endif
