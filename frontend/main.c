/* SDL2 frontend: the game in $CT_ROM from reset, in the system-mode
 * interpreter under the frame scheduler, in a window titled from the ROM
 * header.
 *
 * usage: ct_sdl [--scale N] [--frames N] [--dump DIR] [--needed-hw FILE] [--fast]
 *               [--require-render] [--log-input] [--interp-only]
 *               [--record FILE] [--script FILE] [--log-audio]
 *               [--require-no-underrun]
 *
 * 256x224, integer scaled (--scale, default 3); paced to 60.0988 Hz
 * (NTSC) unless --fast; 48 kHz stereo audio (below); keyboard (arrows, Z=B X=A
 * A=Y S=X Q=L W=R, Enter=Start, Right Shift=Select) and the first game
 * controller on pad 1. Ctrl+Q or closing the window quits (not Esc: Steam
 * Input's desktop layout sends Esc for controller B). While a controller
 * is active, key presses within 2 frames of a controller button press are
 * ignored (key_filter in input.h). --frames, --dump
 * and --needed-hw work as in ct_boot.
 *
 * --record FILE writes pad 1's buttons per frame as an input script
 * (replay.h), on every exit including Ctrl+Q and fatal errors: play to any
 * point, then `ct_boot --script FILE --frames N` reproduces the run.
 * --script FILE plays a script's buttons, combined with live input.
 * Input is applied at the frame edge, as in ct_boot.
 *
 * Audio: the DSP's 32 kHz output, band-limited resampling to 48 kHz
 * (resample.h), queued to SDL. Playback starts once AUDIO_TARGET samples
 * are queued; after that the resampling rate moves within +-0.5% to hold
 * the queue there (dynamic rate control), so emulation pacing and the
 * sound card's clock can't drift apart. An empty queue at a frame is an
 * underrun; each exit prints the count. --log-audio prints the queue
 * level and rate factor every frame; --require-no-underrun exits 1 if any
 * happened.
 *
 * Every exit prints its reason ("ct_sdl: exit: ..."); startup prints each
 * joystick and the button mapping. --log-input prints every key and
 * controller button event.
 *
 * port additions (GPL-3.0-or-later):
 *   --hd2d [--preset off|subtle|hd2d|deck|hd2d3d] [--aspect 4:3|pixel|stretch]
 *          [--debug-view N] [--fullscreen] [--log-gpu]
 *       draw through the HD-2D renderer (frontend/hd2d) in an OpenGL 3.3
 *       (or ES 3.0) window. F1 cycles the preset, F2 the debug view
 *       (layer classes, depth, blur), F3 the aspect, F11 fullscreen; on a
 *       controller, R3 cycles the preset and L3 the aspect. Preset and
 *       aspect are saved (SDL pref path ct-recomp/hd2d/settings.ini).
 *   --shot F:PATH (repeatable) [--shot-size WxH]
 *       at the end of frame F, render the HD-2D picture offscreen at
 *       WxH (default 1920x1080) and write it to PATH as PNG; with
 *       --hd2d unset, writes the plain 256x224 frame.
 *   --module FILE
 *       take the game code from a game module (runtime/module.h): the
 *       engine-only build (ct_sdl_engine) contains none of its own.
 *   --rom FILE
 *       the ROM to run (instead of $CT_ROM).
 *   engine-only build, without --module: the first-run flow (firstrun.h)
 *       finds the ROM (--rom, $CT_ROM, the last one used, or a file dropped
 *       on the window), builds the game module from it if needed, with a
 *       progress screen, and loads it. --interp-only skips the module.
 *       --data-dir DIR (default: the source tree) holds game/ and tools/;
 *       --game ID picks DIR/game/ID (default: the game it was configured
 *       with). --shot status:PATH writes the setup screen each time it
 *       changes (a %d in PATH numbers the shots). --build-only stops once
 *       the module is built and loads, without a window (the launcher's
 *       --build-module).
 *   --touch [--touch-hide SECS] [--touch-test FILE]
 *       the on-screen touch controller (touch.h): shown on touchscreens
 *       once touched (from the start on Android); --touch shows it from
 *       the start and lets the left mouse button act as one finger. It
 *       hides when a controller is used and after SECS (default 15, 0:
 *       never) without a touch. --touch-test FILE injects scripted
 *       touches (format in touch.h). While it is shown, plain-path --shot
 *       PNGs are --shot-size with the overlay, as HD-2D ones are.
 *   --save FILE | --no-save, --dump-sram FILE, --poke-sram F:ADDR=VAL
 *       the save RAM file (frontend/saves.h; default GAME_ID.srm in the
 *       per-game preferences folder), or none. For testing, --dump-sram
 *       writes the save RAM as it is at exit, and --poke-sram changes a
 *       byte of it after frame F.
 *   handhelds (docs/HANDHELD.md, device.h): under Steam's Game Mode
 *       (gamescope) the window is fullscreen, the mouse cursor hidden and
 *       HD-2D on unless --no-hd2d; on a Steam Deck with no saved look the
 *       "deck" preset is the default. Holding View+Menu (Back+Start) for
 *       2 s asks whether to quit (A quits, B goes back).
 *   controls (docs/HANDHELD.md): SDL's own Steam Deck / Steam Controller
 *       drivers when Steam Input isn't in between (input_sdl_hints); the
 *       back buttons and grips, and F6 F9 F8 F10 Tab, emit frontend
 *       actions (actions.h, bindings.h; settings.ini bind_*, key_*);
 *       messages, in both pictures, show the buttons of the device last
 *       used (glyphs.h, osd_text.h); settings.ini gyro=on aims the 3D
 *       diorama's camera with pad 1's gyro.
 *   --coop | --no-coop
 *       two players (docs/USER-GUIDE.md, co-op): a second controller plays
 *       SNES pad 2, and the game module's co-op patches share the game
 *       with it (sched_set_players). Default: on whenever two controllers
 *       are connected; F4 turns it on or off; saved (settings.ini
 *       coop=auto|on|off). Pad 1 is the keyboard plus the first
 *       controller, pad 2 the second, pad 3 a third. settings.ini
 *       coop_chars=2,3 lists the party slots player 2 has (default 2 and
 *       3), coop_chars3= player 3's (none by default; a third player only
 *       plays with one). A controller that goes hands its characters back
 *       to player 1 at once. --record writes pads 2 and 3 as "p2"/"p3"
 *       lines and --script plays them (with --coop, a script's pad 2 is a
 *       second player without a controller).
 *   --coop-split
 *       experimental: when player 2's party member is about 3/4 of a
 *       screen from the leader, the window shows a second view centred on
 *       it (side by side on a wide window, else one above the other), and
 *       one view again once they are close. Plain (not HD-2D) picture
 *       only. The game module draws it (docs/USER-GUIDE.md, co-op).
 *   --pack NAME (repeatable) | --pack none, --pack-dump NAME
 *       asset packs (docs/ASSET-PACKS.md, frontend/packs): load replacement
 *       art from <pref path>/packs/NAME (stacked, the first wins; saved as
 *       packs= in settings.ini; F5 reloads), or dump every new tile and
 *       sprite drawn into a pack. A pack needs the GL renderer: without
 *       --hd2d it is used with the off preset.
 *   --pacing auto|timer|vsync [--log-pacing]
 *       how frames are paced: a sleep to each 60.0988 Hz frame time
 *       (timer; auto, the default, is the timer for now), or the
 *       display's vertical sync (vsync, for 60 Hz displays: the audio
 *       rate control absorbs the 0.16% difference, and while the sound
 *       queue runs low vsync is off until it refills). A vsync that
 *       doesn't give ~60 Hz falls back to the timer. --log-pacing prints frame-time statistics every 60 frames;
 *       every exit prints them for the whole run. */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__) || defined(__APPLE__)
#include <time.h>
#endif

#include <SDL.h>

#include "actions.h"  /* port: back buttons, quick save / load, fast-forward */
#include "bindings.h"
#include "bus.h"
#include "device.h"   /* port: Steam Game Mode, Steam Deck */
#include "glyphs.h"   /* port: button glyphs in messages */
#include "osd_text.h"
#include "module.h"   /* port: --module */
#ifdef CT_FIRSTRUN
#include "firstrun.h" /* port: engine-only first run */
#endif
#include "hd2d/hd2d.h"
#include "hwlog.h"
#include "input.h"
#include "interp.h"
#include "packs.h"    /* port: asset packs (docs/ASSET-PACKS.md) */
#include "png.h"
#include "replay.h"
#include "resample.h"
#include "saves.h"    /* port: save RAM on disk */
#include "sched.h"
#include "touch.h"      /* port: on-screen touch controller */
#include "hd2d/osd_font.h" /* port: the co-op turn tag */
#include "touch_gl.h"
#include "qol.h"        /* port: quality of life (save states, fast-forward, ...) */

#define RATE 48000
/* Device buffer: 960 samples, exactly 20 ms at 48 kHz. Queue target: two
   device buffers plus one frame (800), ~57 ms. */
#define AUDIO_BUFFER 960
#define AUDIO_TARGET (2 * AUDIO_BUFFER + 800)
#define AUDIO_MAX_DEV 0.005
#define FRAME_NS 16639267ull   /* 1 / 60.0988 Hz */

static jmp_buf fatal_jmp;
static char fatal_msg[256];

static void on_fatal(const char *msg)
{
    snprintf(fatal_msg, sizeof fatal_msg, "%s", msg);
    longjmp(fatal_jmp, 1);
}

/* Why the process is ending; printed by an atexit handler so no exit path
   goes unexplained. */
static char exit_reason[256] =
    "exit() before the frame loop: see the message above (a ct: line is a fatal error)";
static long exit_frame = -1;
static long audio_underruns, audio_dropped;

/* port: frame pacing (--pacing) and its statistics: the time between
   successive frame edges, after the wait. */
enum { PACE_AUTO, PACE_TIMER, PACE_VSYNC };
static int pace_opt = PACE_AUTO, pace_vsync, log_pacing;
static int pace_catchup;     /* vsync off for now: the sound queue ran low */
static long pace_catchups;   /* times that happened */
static struct {
    uint64_t last;
    long n, late;               /* late: over 25 ms, a missed display refresh at 60 Hz */
    double sum, min, max;       /* ms */
    long wn, wlate;             /* the current 60-frame window */
    double wsum, wmin, wmax;
} pace;

static void pace_print(const char *what, long n, double sum, double mn, double mx, long late)
{
    if (n && sum > 0)
        fprintf(stderr, "ct_sdl: pacing %s (%s): %ld frames, mean %.3f ms (%.3f Hz), min %.3f, "
                        "max %.3f, %ld over 25 ms, %ld vsync catch-ups\n", what,
                pace_vsync ? "vsync" : "timer", n, sum / n, 1000.0 * n / sum, mn, mx, late,
                pace_catchups);
}

static void set_exit_reason(const char *fmt, ...) CT_PRINTF(1, 2);
static void set_exit_reason(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(exit_reason, sizeof exit_reason, fmt, ap);
    va_end(ap);
}

static void report_exit(void)
{
    fprintf(stderr, "ct_sdl: exit: %s (frame %ld)\n", exit_reason, exit_frame);
    fprintf(stderr, "ct_sdl: audio: %ld underruns, %ld frames dropped\n", audio_underruns,
            audio_dropped);
    pace_print("run", pace.n, pace.sum, pace.min, pace.max, pace.late);   /* port */
}

static void print_controllers(void)
{
    int n = SDL_NumJoysticks();
    fprintf(stderr, "ct_sdl: %d joystick(s)\n", n);
    for (int k = 0; k < n; k++) {
        const char *name = SDL_JoystickNameForIndex(k);
        fprintf(stderr, "ct_sdl:   #%d \"%s\" %s\n", k, name ? name : "?",
                SDL_IsGameController(k) ? "(game controller)" : "(joystick only: not used)");
        if (SDL_IsGameController(k)) {
            char *m = SDL_GameControllerMappingForDeviceIndex(k);
            fprintf(stderr, "ct_sdl:     SDL mapping: %s\n", m ? m : "?");
            SDL_free(m);
            /* port: what it has for the frontend's actions and glyphs */
            int type = SDL_GameControllerTypeForIndex(k);
            uint16_t vid = SDL_JoystickGetDeviceVendor(k), pid = SDL_JoystickGetDeviceProduct(k);
            int fam = glyphs_family(type, vid, pid, name, device_kind() == DEVICE_STEAM_DECK);
            fprintf(stderr, "ct_sdl:     %04x:%04x, SDL type %d, glyphs %s\n", vid, pid, type,
                    glyphs_family_name(fam));
        }
    }
    {
        const char *why;
        if (input_under_steam_input(&why))
            fprintf(stderr, "ct_sdl: Steam Input is on (%s): Steam's virtual pad, the back "
                            "buttons as Steam's layout sends them\n", why);
    }
    fprintf(stderr, "ct_sdl: pad 1: keyboard arrows, Z=B X=A A=Y S=X Q=L W=R, Enter=Start, "
                    "Right Shift=Select; controller south=B east=A west=Y north=X, "
                    "shoulders=L/R, Start, Back=Select, d-pad/left stick. Ctrl+Q or closing "
                    "the window quits.\n");
}

static int pad_button(void *ctx, SDL_GameControllerButton b)
{
    return SDL_GameControllerGetButton(ctx, b);
}

static int pad_axis(void *ctx, SDL_GameControllerAxis a)
{
    return SDL_GameControllerGetAxis(ctx, a);
}

static SDL_JoystickID pad_id(SDL_GameController *c)
{
    return c ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c)) : -1;
}

/* The first game controller other than a and b (port: co-op's pads skip
   the ones already open). */
static SDL_GameController *open_controller_except2(SDL_GameController *a, SDL_GameController *b)
{
    for (int k = 0; k < SDL_NumJoysticks(); k++) {
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(k);
        if (SDL_IsGameController(k) && id != pad_id(a) && id != pad_id(b))
            return SDL_GameControllerOpen(k);
    }
    return NULL;
}

static SDL_GameController *open_controller(void)
{
    return open_controller_except2(NULL, NULL);
}

static replay script;
static replay_rec rec;

/* Host state shared by main and the frame hook. */
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID audio;
static SDL_GameController *pad;
static SDL_GameController *pad2, *pad3;   /* port: co-op, the second and third controllers */
static resampler rs;
static int playing;
static double level_avg = AUDIO_TARGET;
static key_filter keys;
static uint16_t live_buttons;
static uint64_t perf_freq, next_frame;
static int running = 1, fast, log_input, log_audio;

/* port: asset packs (frontend/packs, docs/ASSET-PACKS.md). They live in
   the per-game data folder, <pref path>/packs/NAME; a NAME with a slash
   in it is a directory of its own. */
static const char *pack_dump;   /* --pack-dump NAME */
#define MAX_PACK_NAMES 16
static char pack_names[MAX_PACK_NAMES][256];   /* loaded, highest priority first */
static int n_pack_names, packs_given;          /* --pack seen: beats settings.ini */
static int preset_forced = -1;   /* the saved look, while packs force the GL path */
static uint16_t pack_map[SCHED_WIDTH * SCHED_HEIGHT * 4];
static double pack_map_ms;   /* CPU time building the maps, and how many */
static long pack_map_frames, pack_map_pixels;

static void pack_map_report(void)
{
    if (pack_map_frames)
        fprintf(stderr, "ct_sdl: asset packs: %ld frames, %.1f%% of pixels replaced, "
                        "map %.3f ms per frame (CPU)\n", pack_map_frames,
                100.0 * pack_map_pixels / ((double)pack_map_frames * SCHED_WIDTH * SCHED_HEIGHT),
                pack_map_ms / pack_map_frames);
}

/* "a,b": the names, in order. */
static void pack_names_parse(const char *list)
{
    n_pack_names = 0;
    while (*list && n_pack_names < MAX_PACK_NAMES) {
        size_t n = strcspn(list, ",");
        if (n && n < sizeof pack_names[0] && strncmp(list, "none", n))
            snprintf(pack_names[n_pack_names++], sizeof pack_names[0], "%.*s", (int)n, list);
        list += n + (list[n] == ',');
    }
}

static char *pack_dir(const char *game_id, const char *name)
{
    char *p = malloc(4096);
    if (!p)
        return NULL;
    if (strchr(name, '/') || strchr(name, '\\')) {
        snprintf(p, 4096, "%s", name);
        return p;
    }
    char *dir = SDL_GetPrefPath("ct-recomp", game_id);
    snprintf(p, 4096, "%spacks/%s", dir ? dir : "./", name);
    SDL_free(dir);
    return p;
}

/* Load the named packs; returns how many replacements. */
static int packs_setup(const char *game_id)
{
    const char *dirs[MAX_PACK_NAMES];
    char *owned[MAX_PACK_NAMES];
    int n = 0;
    for (int k = 0; k < n_pack_names; k++)
        if ((owned[n] = pack_dir(game_id, pack_names[k])))
            dirs[n] = owned[n], n++;
    int r = n ? packs_load(dirs, n) : 0;
    for (int k = 0; k < n; k++)
        free(owned[k]);
    return r;
}

/* The 3D diorama's tiles -> atlas entries (hd2d_pack_tiles). */
static int pack_tile_lookup(void *user, unsigned chr, int pal, int bpp, const uint16_t *vram,
                            const uint16_t *cgram)
{
    (void)user;
    const packs_rect *r = packs_lookup_tile(packs_key_tile(vram, cgram, chr, pal, bpp));
    int n;
    const packs_rect *e = packs_entries(&n);
    return r ? (int)(r - e) + 1 : 0;
}

/* The loaded packs' atlas to the renderer. */
static void packs_upload(void)
{
    int size, pages, n;
    const uint8_t *atlas = packs_atlas(&size, &pages);
    const packs_rect *e = packs_entries(&n);
    int *rects = n ? malloc((size_t)n * 8 * sizeof *rects) : NULL;
    for (int k = 0; k < n && rects; k++) {
        int v[8] = {e[k].page, e[k].x, e[k].y, e[k].w, e[k].h, e[k].src_w, e[k].src_h, 0};
        memcpy(rects + k * 8, v, sizeof v);
    }
    hd2d_pack_atlas(rects ? atlas : NULL, size, pages, rects, rects ? n : 0);
    hd2d_pack_tiles(rects ? pack_tile_lookup : NULL, NULL);
    free(rects);
}

static void pack_dump_report(void)
{
    long pieces, objects;
    packs_dump_counts(&pieces, &objects);
    fprintf(stderr, "ct_sdl: pack dump: %ld new pieces, %ld new objects\n", pieces, objects);
    packs_dump_close();
}
static const char *dump;

/* port: HD-2D presentation and screenshots. */
static SDL_Window *win;
static SDL_GLContext glctx;
static int hd2d_mode, log_gpu;
static int game_mode, preset_saved, preset_given;   /* port: device.h defaults */
static quit_combo quitc;                            /* port: View+Menu quits */
/* port: co-op (--coop): COOP_AUTO is on while two controllers are open. */
enum { COOP_AUTO, COOP_ON, COOP_OFF };
static int coop_pref = COOP_AUTO, coop_players = 1;
static uint16_t live_buttons2, live_buttons3;       /* pads 2 and 3, this frame */
/* port: which player (1-3) has party slot 1-3 (settings.ini coop_chars,
   coop_chars3: the slots players 2 and 3 have). */
static uint8_t slot_player[3] = {1, 2, 2};
/* port: the back buttons / grips and their keys (bindings.h, actions.h),
   per pad (pad, pad2, pad3); the glyphs follow the device last used
   (glyphs.h); gyro aiming (settings.ini gyro=on, off by default). */
static binder pad_binder[3];
static long last_pad_press = -1000;   /* frame of the last controller button */
static int gyro_on, steam_hidapi = 1;
static float gyro_strength = 1.0f;
static gyro_cam gyro;
static SDL_GameController *gyro_pad;  /* the pad whose gyro is on */
static long quit_asked = -1;                        /* frame the question went up */
#define MAX_SHOTS 64
static struct { long frame; const char *path; } shots[MAX_SHOTS];
static int n_shots, shot_w = 1920, shot_h = 1080;

/* port: the touch overlay (touch.h); its GL drawer for the HD-2D path. */
static int touch_gl_ok;
static touch_opts touch_o = {0, 15};

/* port: where the picture is: HD-2D's aspect, or the plain integer scale. */
static touch_rect picture_rect(int w, int h)
{
    if (!hd2d_mode) {   /* port: QOL integer scale or fit */
        touch_rect r;
        qol_picture_rect(w, h, &r.x, &r.y, &r.w, &r.h);
        return r;
    }
    int a = hd2d_aspect();
    return touch_picture_aspect(w, h, a == HD2D_ASPECT_PIXEL ? (SCHED_WIDTH * 8.0 / 7.0) / SCHED_HEIGHT
                                      : a == HD2D_ASPECT_STRETCH ? 0.0 : 4.0 / 3.0);
}

/* port: the plain path: the frame integer-scaled and centered, the touch
   overlay over it. No logical size: SDL would clamp touches to it. */
/* port: co-op: "P2" / "P3" over the picture while the game waits on that
   player (sched_turn_player: the battle menu that player has is open),
   by the command windows at the bottom left, in the 5x7 font at 2x. */
static const uint8_t *turn_tag(const uint8_t *fb)
{
    static uint8_t out[SCHED_WIDTH * SCHED_HEIGHT * 4];
    int p = coop_players > 1 ? sched_turn_player() : -1;
    if (p < 1)
        return fb;
    memcpy(out, fb, sizeof out);
    char text[3] = {'P', (char)('1' + p), 0};
    static const uint8_t col[4][3] = {{0, 0, 0}, {80, 200, 255}, {255, 190, 60}, {120, 255, 120}};
    int x0 = 6, y0 = 136;
    for (int y = y0 - 3; y < y0 + 17; y++)   /* a dark box */
        for (int x = x0 - 3; x < x0 + 26; x++) {
            uint8_t *q = out + ((size_t)y * SCHED_WIDTH + x) * 4;
            q[0] = q[0] / 4, q[1] = q[1] / 4, q[2] = q[2] / 4;
        }
    for (int k = 0; text[k]; k++) {
        const uint8_t *rows = osd_glyph(text[k]);
        for (int r = 0; r < 7; r++)
            for (int b = 0; b < 5; b++)
                if (rows[r] & (0x10 >> b))
                    for (int d = 0; d < 4; d++) {
                        int x = x0 + (k * 6 + b) * 2 + (d & 1), y = y0 + r * 2 + (d >> 1);
                        uint8_t *q = out + ((size_t)y * SCHED_WIDTH + x) * 4;
                        q[0] = col[p][2], q[1] = col[p][1], q[2] = col[p][0];
                    }
    }
    return out;
}

/* port: split screen (--coop-split): this frame's second view, if any. */
static int split_on;
static uint8_t view2_px[SCHED_WIDTH * SCHED_HEIGHT * 4];
static SDL_Texture *tex2;

static void present_plain(const uint8_t *bgrx)
{
    int w, h;
    SDL_GetRendererOutputSize(ren, &w, &h);
    SDL_UpdateTexture(tex, NULL, bgrx, SCHED_WIDTH * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (split_on && tex2) {
        /* port: two views, side by side on a wide window (player 2's on
           the side it is on), else one above the other */
        int side = (long)w * SCHED_HEIGHT >= (long)h * SCHED_WIDTH;
        int hw = side ? w / 2 : w, hh = side ? h : h / 2;
        touch_rect p = touch_picture_aspect(hw, hh, (double)SCHED_WIDTH / SCHED_HEIGHT);
        int second_first = side ? sched_view2()->dx < 0 : sched_view2()->dy < 0;
        SDL_UpdateTexture(tex2, NULL, view2_px, SCHED_WIDTH * 4);
        for (int k = 0; k < 2; k++) {
            int slot = k ^ second_first;
            SDL_Rect dst = {p.x + (side ? slot * hw : 0), p.y + (side ? 0 : slot * hh), p.w, p.h};
            SDL_RenderCopy(ren, k ? tex2 : tex, NULL, &dst);
        }
        SDL_RenderPresent(ren);
        return;
    }
    qol_plain_draw(ren, tex, w, h);   /* port: QOL integer scale or fit, CRT look */
    touch_draw_sdl(ren);
    {   /* port: the message, placed and sized as HD-2D's */
        int px, py, pw, ph;
        qol_picture_rect(w, h, &px, &py, &pw, &ph);
        int s = ph / 180 > 2 ? ph / 180 : 2;
        osd_sdl_draw(ren, px + s * 4, py + s * 4, s, 1);
    }
    SDL_RenderPresent(ren);
}

/* port: the scene the 3D diorama look draws (module.h), captured once per
   frame edge and only while the look uses it. */
static const ct_scene *scene_now(void)
{
    static ct_scene sc;
    static long at = -1;
    if (!hd2d_wants_scene() || !ct_scene_available())
        return NULL;
    if (sched_frame_count() != at) {
        at = sched_frame_count();
        ct_scene_capture(&sc);
    }
    return &sc;
}

/* One screenshot of a frame: HD-2D at shot_w x shot_h, else as is. */
static int write_shot(const char *path, const uint8_t *bgrx, const uint8_t *layers,
                      const uint8_t *behind, const uint8_t *behind_layers,
                      const sched_ppu_state *st)
{
    int ok;
    if (hd2d_mode) {
        uint8_t *buf = malloc((size_t)shot_w * shot_h * 4);
        hd2d_set_scene(st ? scene_now() : NULL);   /* none on the setup screens */
        hd2d_render(bgrx, layers, behind, behind_layers, st, shot_w, shot_h, 1);
        ok = buf && !hd2d_read(buf, shot_w, shot_h) &&
             (!touch_gl_ok || !touch_gl_shot(buf, shot_w, shot_h, picture_rect(shot_w, shot_h))) &&
             !png_write_bgrx(path, buf, shot_w, shot_h);
        free(buf);
    } else if (touch_visible()) {   /* port: with the touch overlay */
        uint8_t *buf = malloc((size_t)shot_w * shot_h * 4);
        SDL_UpdateTexture(tex, NULL, bgrx, SCHED_WIDTH * 4);
        ok = buf && !touch_shot_sdl(ren, tex, shot_w, shot_h, buf) &&
             !png_write_bgrx(path, buf, shot_w, shot_h);
        free(buf);
    } else {
        ok = !png_write_bgrx(path, bgrx, SCHED_WIDTH, SCHED_HEIGHT);
    }
    return ok;
}

static void take_shots(long f)
{
    for (int k = 0; k < n_shots; k++) {
        if (shots[k].frame != f)
            continue;
        int ok = write_shot(shots[k].path, sched_frame(), sched_layers(), sched_behind(),
                            sched_behind_layers(), sched_ppu_state_get());
        fprintf(stderr, "ct_sdl: frame %ld: %s %s\n", f, ok ? "wrote" : "cannot write",
                shots[k].path);
        if (split_on) {   /* port: the second view beside it */
            char p2[512];
            snprintf(p2, sizeof p2, "%s.view2.png", shots[k].path);
            ok = !png_write_bgrx(p2, view2_px, SCHED_WIDTH, SCHED_HEIGHT);
            fprintf(stderr, "ct_sdl: frame %ld: %s %s\n", f, ok ? "wrote" : "cannot write", p2);
        }
    }
}

static void present_hd2d(void)
{
    int w, h;
    SDL_GL_GetDrawableSize(win, &w, &h);
    hd2d_set_scene(scene_now());
    hd2d_render(sched_frame(), sched_layers(), sched_behind(), sched_behind_layers(),
                sched_ppu_state_get(), w, h, 0);
    if (touch_gl_ok)
        touch_gl_draw(w, h);   /* port: touch overlay */
    SDL_GL_SwapWindow(win);
    if (log_gpu && sched_frame_count() % 60 == 0)
        fprintf(stderr, "ct_sdl: frame %ld hd2d %dx%d %.2f ms\n", sched_frame_count(), w, h,
                hd2d_last_ms());
}

static void settings_save(void);

/* port: the window's remembered state (settings.ini: window=WxH,
   position=X,Y, fullscreen=0|1). The windowed size and place are kept
   apart from fullscreen, so leaving fullscreen goes back to them. */
static struct {
    int w, h, x, y, fs;      /* current */
    int have_size, have_pos; /* known: loaded, or seen */
    int w0, h0, x0, y0, fs0; /* as the window was created */
    int scale_given;         /* --scale beats the saved size */
} wst;

static int wst_places_work(void)   /* a driver that reports real window places */
{
    const char *d = SDL_GetCurrentVideoDriver();
    return d && strcmp(d, "wayland") && strcmp(d, "offscreen") && strcmp(d, "dummy") &&
           strcmp(d, "KMSDRM") && strcmp(d, "kmsdrm");
}

/* Take the window's windowed geometry (not while fullscreen: that is the
   display's). */
static void wst_track(void)
{
    if (!win || (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN))
        return;
    SDL_GetWindowSize(win, &wst.w, &wst.h);
    wst.have_size = 1;
    if (wst_places_work()) {
        SDL_GetWindowPosition(win, &wst.x, &wst.y);
        wst.have_pos = 1;
    }
}

static int wst_dirty(void)
{
    int fs = win && (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) ? 1 : 0;
    wst.fs = fs;
    return fs != wst.fs0 || wst.w != wst.w0 || wst.h != wst.h0 || wst.x != wst.x0 ||
           wst.y != wst.y0;
}

/* Is a place on a display (so a monitor that is gone doesn't strand the window)? */
static int wst_on_screen(int x, int y, int w)
{
    for (int k = 0, n = SDL_GetNumVideoDisplays(); k < n; k++) {
        SDL_Rect r;
        if (!SDL_GetDisplayBounds(k, &r) && x + w / 2 >= r.x && x + w / 2 < r.x + r.w &&
            y + 16 >= r.y && y + 16 < r.y + r.h)
            return 1;
    }
    return 0;
}

/* port: a message on screen (HD-2D or the plain picture), [token]s as
   button glyphs of the device in use (glyphs.h). */
static void osd_show(const char *msg)
{
    if (hd2d_mode)
        hd2d_osd(msg);
    else
        osd_sdl_show(msg);
}

/* ... and on stderr, the tokens as their labels. */
static void osd(const char *msg)
{
    char plain[256];
    osd_text_plain(msg, plain, sizeof plain);
    fprintf(stderr, "ct_sdl: %s\n", plain);
    osd_show(msg);
}

/* port: co-op is on: never when turned off; otherwise while a second
   controller is open, or a --script plays pad 2 with co-op turned on.
   When the second controller goes, its characters go back to player 1
   at once (one player: the game's co-op patches stand aside). */
static int coop_on(void)
{
    if (coop_pref == COOP_OFF)
        return 0;
    return pad2 || (coop_pref == COOP_ON && replay_multi(&script));
}

/* port: players on: 1, 2, or 3 with a third controller and a party slot
   given to player 3. */
static int coop_count(void)
{
    if (!coop_on())
        return 1;
    int p3 = 0;
    for (int k = 0; k < 3; k++)
        p3 |= slot_player[k] == 3;
    return pad3 && p3 ? 3 : 2;
}

/* port: tell the game how many players there are, and who has which
   party slot; say so when it changes. */
static void coop_update(void)
{
    static int said;
    int n = coop_count();
    for (int k = 0; k < 3; k++)
        sched_set_slot_player(k, slot_player[k] - 1);
    if (said && n == coop_players)
        return;
    if (said || n > 1) {
        char msg[200];
        const char *name = pad2 ? SDL_GameControllerName(pad2) : "the script";
        if (n > 1)
            snprintf(msg, sizeof msg, "Co-op: on, player 2 is %s%s%s", name ? name : "?",
                     n > 2 ? ", player 3 is " : "",
                     n > 2 && SDL_GameControllerName(pad3) ? SDL_GameControllerName(pad3) : "");
        else
            snprintf(msg, sizeof msg, "Co-op: off");
        osd(msg);
    }
    said = 1;
    coop_players = n;
    sched_set_players(n);
}

/* port: settings.ini coop_chars / coop_chars3: party slots, "2,3". */
static void parse_chars(const char *v, int player)
{
    if (player == 2)
        for (int k = 0; k < 3; k++)
            if (slot_player[k] == 2)
                slot_player[k] = 1;
    for (; *v; v++)
        if (*v >= '1' && *v <= '3')
            slot_player[*v - '1'] = (uint8_t)player;
}

static void print_chars(FILE *f, const char *key, int player)
{
    fprintf(f, "%s=", key);
    const char *sep = "";
    for (int k = 0; k < 3; k++)
        if (slot_player[k] == player) {
            fprintf(f, "%s%d", sep, k + 1);
            sep = ",";
        }
    fputc('\n', f);
}

/* F1-F3, F11: the HD-2D controls. Returns 1 if the key was one of them. */
static int hd2d_key(SDL_Scancode sc)
{
    if (sc == SDL_SCANCODE_F4) {   /* port: co-op on / off */
        coop_pref = coop_on() ? COOP_OFF : COOP_ON;
        coop_update();
        if (coop_pref == COOP_ON && coop_players < 2)
            osd("Co-op: on once a second controller is connected");
        settings_save();
        return 1;
    }
    if (sc == SDL_SCANCODE_F11) {
        int fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        wst.fs = !fs;
        settings_save();
        return 1;
    }
    if (!hd2d_mode)
        return 0;
    if (sc == SDL_SCANCODE_F5 && n_pack_names) {   /* port: reload the packs */
        int n = packs_reload();
        packs_upload();
        sched_set_prov(packs_active() || pack_dump);
        static char msg[48];
        snprintf(msg, sizeof msg, n < 0 ? "Packs: none readable" : "Packs reloaded: %d", n);
        fprintf(stderr, "ct_sdl: %s\n", msg);
        hd2d_osd(msg);
        return 1;
    }
    if (sc == SDL_SCANCODE_F1) {
        preset_forced = -1;   /* port: the user's choice now */
        hd2d_set_preset((hd2d_preset() + 1) % HD2D_PRESETS);
        fprintf(stderr, "ct_sdl: hd2d preset %s\n", hd2d_preset_name(hd2d_preset()));
        static const char *const msg[HD2D_PRESETS] = {"Look: original", "Look: HD-2D subtle",
                                                      "Look: HD-2D", "Look: HD-2D Steam Deck",
                                                      "Look: HD-2D 3D diorama"};
        osd_show(msg[hd2d_preset()]);
        settings_save();
    } else if (sc == SDL_SCANCODE_F2) {
        hd2d_set_debug((hd2d_debug() + 1) % HD2D_DEBUGS);
        fprintf(stderr, "ct_sdl: hd2d debug view %d\n", hd2d_debug());
        static const char *const msg[HD2D_DEBUGS] = {"Debug: off", "Debug: layers", "Debug: depth",
                                                     "Debug: blur", "Debug: compare",
                                                     "Debug: behind"};
        osd_show(msg[hd2d_debug()]);
    } else if (sc == SDL_SCANCODE_F3) {
        hd2d_set_aspect((hd2d_aspect() + 1) % HD2D_ASPECTS);
        settings_save();
        static const char *const msg[HD2D_ASPECTS] = {"Aspect: 4:3", "Aspect: pixel 8:7",
                                                      "Aspect: stretch"};
        osd_show(msg[hd2d_aspect()]);
    } else {
        return 0;
    }
    return 1;
}

/* port: HD-2D settings persist in SDL's per-user preferences folder
   (settings.ini: preset=NAME, aspect=N). */
static char *settings_path(void)
{
    char *dir = SDL_GetPrefPath("ct-recomp", "hd2d");
    if (!dir)
        return NULL;
    size_t n = strlen(dir) + sizeof "settings.ini";
    char *p = malloc(n);
    if (p)
        snprintf(p, n, "%ssettings.ini", dir);
    SDL_free(dir);
    return p;
}

static int parse_preset(const char *v);

static void settings_load(void)
{
    char *path = settings_path(), line[128], val[64];
    FILE *f = path ? fopen(path, "r") : NULL;
    while (f && fgets(line, sizeof line, f)) {
        int n;
        if (sscanf(line, "preset=%63s", val) == 1 && parse_preset(val) >= 0) {
            hd2d_set_preset(parse_preset(val));
            preset_saved = 1;   /* port: the user's choice beats the device default */
        }
        else if (sscanf(line, "aspect=%d", &n) == 1)
            hd2d_set_aspect(n);
        else if (!strncmp(line, "packs=", 6) && !packs_given) {   /* port: asset packs */
            line[strcspn(line, "\r\n")] = 0;
            pack_names_parse(line + 6);
        }
        else if (sscanf(line, "window=%dx%d", &wst.w, &wst.h) == 2 && wst.w >= 256 &&
                 wst.h >= 224 && wst.w <= 16384 && wst.h <= 16384)
            wst.have_size = 1;
        else if (sscanf(line, "position=%d,%d", &wst.x, &wst.y) == 2)
            wst.have_pos = 1;
        else if (sscanf(line, "fullscreen=%d", &n) == 1)
            wst.fs = n != 0;
        else if (sscanf(line, "coop_chars=%63s", val) == 1)   /* port: co-op */
            parse_chars(val, 2);
        else if (sscanf(line, "coop_chars3=%63s", val) == 1)
            parse_chars(val, 3);
        else if (sscanf(line, "coop=%63s", val) == 1)
            coop_pref = !strcmp(val, "on") ? COOP_ON : !strcmp(val, "off") ? COOP_OFF : COOP_AUTO;
        else if (bindings_parse_line(bindings_current(), line))   /* port: bind_*, key_* */
            ;
        else if (sscanf(line, "gyro=%63s", val) == 1)   /* port: gyro aiming */
            gyro_on = !strcmp(val, "on") || !strcmp(val, "1");
        else if (sscanf(line, "gyro_strength=%63s", val) == 1)
            gyro_strength = (float)atof(val);
        else if (sscanf(line, "steam_hidapi=%63s", val) == 1)   /* port: input_sdl_hints */
            steam_hidapi = !(!strcmp(val, "off") || !strcmp(val, "0"));
        else {   /* port: QOL settings (qol.h) */
            char key[32];
            if (sscanf(line, "%31[^=]=%63s", key, val) == 2)
                qol_setting(key, val);
        }
    }
    if (f)
        fclose(f);
    free(path);
}

static void settings_save(void)
{
    char *path = settings_path();
    FILE *f = path ? fopen(path, "w") : NULL;
    if (f) {
        fprintf(f, "preset=%s\naspect=%d\n",
                hd2d_preset_name(preset_forced >= 0 ? preset_forced
                                                    : qol_user_preset(hd2d_preset())), hd2d_aspect());
        if (n_pack_names) {   /* port: asset packs, in priority order */
            fputs("packs=", f);
            for (int k = 0; k < n_pack_names; k++)
                fprintf(f, "%s%s", k ? "," : "", pack_names[k]);
            fputc('\n', f);
        }
        if (wst.have_size)
            fprintf(f, "window=%dx%d\n", wst.w, wst.h);
        if (wst.have_pos)
            fprintf(f, "position=%d,%d\n", wst.x, wst.y);
        fprintf(f, "fullscreen=%d\n", wst.fs);
        static const char *const coop_name[] = {"auto", "on", "off"};
        fprintf(f, "coop=%s\n", coop_name[coop_pref]);
        print_chars(f, "coop_chars", 2);
        print_chars(f, "coop_chars3", 3);
        bindings_write(bindings_current(), f);   /* port: back buttons, keys */
        fprintf(f, "gyro=%s\ngyro_strength=%.2f\nsteam_hidapi=%s\n", gyro_on ? "on" : "off",
                gyro_strength, steam_hidapi ? "on" : "off");
        qol_settings_write(f);   /* port: QOL */
        fclose(f);
    }
    free(path);
}

static int parse_preset(const char *v)
{
    for (int k = 0; k < HD2D_PRESETS; k++)
        if (!strcmp(v, hd2d_preset_name(k)))
            return k;
    return -1;
}

/* port: the glyphs of a controller, when it is the one used. */
static void use_glyphs_of(SDL_GameController *c)
{
    int set = c ? glyphs_family_of(c) : GLYPHS_KEYBOARD;
    if (set != glyphs_in_use() && log_input)
        fprintf(stderr, "ct_sdl: glyphs: %s\n", glyphs_family_name(set));
    glyphs_use(set);
}

/* port: the menu action until there is a menu: what the buttons do. */
static void menu_osd(ct_action a, int down, void *user)
{
    (void)a;
    (void)user;
    if (down)
        osd("Menu: [@quicksave] save  [@quickload] load  [@ffwd] fast-forward");
}

static void log_action(ct_action a, int down)
{
    if (log_input)
        fprintf(stderr, "ct_sdl: frame %ld action %s %s\n", sched_frame_count(), action_name(a),
                down ? "down" : "up");
}

/* port: a pad was added or removed: let go of what the pads held (the
   binders see the buttons still down as new presses next frame). */
static void pads_changed(void)
{
    for (int k = 0; k < 3; k++) {
        binder_release(&pad_binder[k], bindings_current());
        binder_init(&pad_binder[k]);
    }
}

/* port: gyro aiming, once a frame (settings.ini gyro=on). The 3D
   diorama's camera follows pad 1's gyro, if it has one. */
static void gyro_frame(void)
{
    if (gyro_pad != pad) {
        gyro_pad = NULL;
        gyro.tilt = gyro.yaw = 0.0f;
        if (pad && SDL_GameControllerHasSensor(pad, SDL_SENSOR_GYRO) &&
            !SDL_GameControllerSetSensorEnabled(pad, SDL_SENSOR_GYRO, SDL_TRUE)) {
            gyro_pad = pad;
            fprintf(stderr, "ct_sdl: gyro aiming with \"%s\"\n", SDL_GameControllerName(pad));
        }
        if (!gyro_pad)
            hd2d_set_camera_offset(0.0f, 0.0f);
    }
    float rate[3] = {0};
    if (!gyro_pad || SDL_GameControllerGetSensorData(gyro_pad, SDL_SENSOR_GYRO, rate, 3))
        return;
    gyro_cam_step(&gyro, rate, (float)(FRAME_NS / 1e9), gyro_strength);
    hd2d_set_camera_offset(gyro.tilt, gyro.yaw);
}

/* Window, keyboard and controller events; live_buttons for pad 1. */
static void poll_input(long f)
{
    keys.controller = pad != NULL;
    {   /* port: lay the touch overlay out; scripted touches for frame f+1 */
        int w = 0, h = 0;
        if (hd2d_mode)
            SDL_GL_GetDrawableSize(win, &w, &h);
        else
            SDL_GetRendererOutputSize(ren, &w, &h);
        touch_set_screen(win, w, h, picture_rect(w, h));
        touch_test_inject(f + 1);
    }
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (touch_event(&ev, f))
            continue;   /* port: a touch, used by the overlay */
        if (qol_event(&ev, f))
            continue;   /* port: QOL keys, the pause and the resume prompt */
        if (log_input && (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && !ev.key.repeat)
            fprintf(stderr, "ct_sdl: frame %ld key %s %s (scancode %d)\n", f,
                    SDL_GetScancodeName(ev.key.keysym.scancode),
                    ev.type == SDL_KEYDOWN ? "down" : "up", ev.key.keysym.scancode);
        if (log_input && (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP))
            fprintf(stderr, "ct_sdl: frame %ld controller button %s %s\n", f,
                    SDL_GameControllerGetStringForButton(ev.cbutton.button),
                    ev.type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up");
        if (ev.type == SDL_WINDOWEVENT && (ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                                           ev.window.event == SDL_WINDOWEVENT_MOVED ||
                                           ev.window.event == SDL_WINDOWEVENT_RESTORED))
            wst_track();
        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            bindings_keys_release();   /* port: no fast-forward stuck on */
        if (ev.type == SDL_CONTROLLERBUTTONDOWN) {   /* port: glyphs of the pad used */
            SDL_GameController *c = SDL_GameControllerFromInstanceID(ev.cbutton.which);
            last_pad_press = f;
            if (c)
                use_glyphs_of(c);
        } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat && f - last_pad_press > 10) {
            use_glyphs_of(NULL);   /* not a key Steam Input sent with a button */
        }
        if (ev.type == SDL_QUIT) {
            set_exit_reason("SDL_QUIT event (window closed, or quit requested by the "
                            "desktop or another program)");
            running = 0;
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_Q &&
                   (ev.key.keysym.mod & KMOD_CTRL)) {
            set_exit_reason("Ctrl+Q");
            running = 0;
        } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat && hd2d_key(ev.key.keysym.scancode)) {
            /* port: renderer controls, not game input */
        } else if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) &&
                   bindings_key_event(bindings_current(), ev.key.keysym.scancode,
                                      ev.type == SDL_KEYDOWN, ev.key.repeat)) {
            /* port: a frontend action's key (bindings.h), not game input */
        } else if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && !ev.key.repeat) {
            key_filter_key(&keys, ev.key.keysym.scancode, ev.type == SDL_KEYDOWN, f);
        } else if (ev.type == SDL_CONTROLLERBUTTONDOWN && hd2d_mode &&
                   (ev.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK ||
                    ev.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK)) {
            /* port: stick clicks (no SNES button) drive the renderer on
               handhelds without function keys: R3 preset, L3 aspect. */
            hd2d_key(ev.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK ? SDL_SCANCODE_F1
                                                                            : SDL_SCANCODE_F3);
        } else if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
            key_filter_button(&keys, f);
        } else if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad) {
            pad = open_controller();
            pads_changed();   /* port */
            if (pad)
                use_glyphs_of(pad);
        } else if (ev.type == SDL_CONTROLLERDEVICEADDED) {   /* port: pads 2 and 3 */
            if (!pad2)
                pad2 = open_controller_except2(pad, pad3);
            else if (!pad3)
                pad3 = open_controller_except2(pad, pad2);
            pads_changed();
        } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
            pads_changed();   /* port: before the pads move up */
            if (gyro_pad && !SDL_GameControllerGetAttached(gyro_pad))
                gyro_pad = NULL;
            SDL_GameController **all[3] = {&pad, &pad2, &pad3};
            for (int k = 0; k < 3; k++)
                if (*all[k] && !SDL_GameControllerGetAttached(*all[k])) {
                    SDL_GameControllerClose(*all[k]);
                    *all[k] = NULL;
                }
            if (!pad) {   /* port: the next one takes over pad 1 */
                pad = pad2;
                pad2 = pad3;
                pad3 = NULL;
            }
            if (!pad)
                pad = open_controller();
        }
    }
    {   /* port: back buttons / grips -> frontend actions, on any pad */
        SDL_GameController *all[3] = {pad, pad2, pad3};
        for (int k = 0; k < 3; k++)
            if (all[k])
                binder_update(&pad_binder[k], bindings_current(), pad_button, all[k]);
    }
    if (gyro_on && hd2d_mode)   /* port: off by default */
        gyro_frame();
    coop_update();   /* port */
    SDL_GameController *p1 = pad, *p2 = coop_players > 1 ? pad2 : NULL;
    SDL_GameController *p3 = coop_players > 2 ? pad3 : NULL;
    uint16_t buttons = input_keyboard(keys.held);
    live_buttons2 = p2 ? input_controller(pad_button, pad_axis, p2) : 0;   /* port: co-op */
    live_buttons3 = p3 ? input_controller(pad_button, pad_axis, p3) : 0;
    if (p1) {   /* port: the quit combo first: it can keep the pad from the game */
        int q = quit_combo_update(&quitc, f, pad_button, p1);
        if (q == QUIT_ASK) {   /* port: both pictures show the question (osd_text.h) */
            fprintf(stderr, "ct_sdl: View+Menu held 2 s: asking to quit\n");
            quit_asked = f;
        } else if (q == QUIT_YES) {
            fprintf(stderr, "ct_sdl: quit confirmed with A\n");
            set_exit_reason("View+Menu held 2 s, then A");
            running = 0;
        } else if (q == QUIT_CANCEL) {
            fprintf(stderr, "ct_sdl: quit cancelled\n");
            quit_asked = -1;
            osd_show("Back to the game");
        }
        /* The question stays up (a message lasts about 2 s) until answered. */
        if (quit_asked >= 0 && running && (f - quit_asked) % 90 == 0)
            osd_show("Quit? [a] quit  [b] back to the game");
        if (!quit_combo_masks(&quitc))
            buttons |= input_controller(pad_button, pad_axis, p1);
    }
    buttons |= touch_buttons(f);   /* port */
    live_buttons = buttons;
}

/* Pad 1 for frame f: live input plus the --script, recorded. port: pad 2
   (co-op) and the script's "pN" pads likewise. */
static void set_input(long f)
{
    uint16_t b = qol_buttons(0, live_buttons, f) | replay_buttons(&script, f);   /* port: QOL turbo */
    sched_set_joypad(0, b);
    replay_rec_frame(&rec, f, b);
    for (int p = 1; p < REPLAY_PORTS; p++) {
        uint16_t bp = (uint16_t)(replay_port_buttons(&script, p, f) |
                                 qol_buttons(p, p == 1 ? live_buttons2 : p == 2 ? live_buttons3 : 0, f));
        sched_set_joypad(p, bp);
        if (bp || p == 1)
            replay_rec_port(&rec, p, f, bp);
    }
}

/* This frame's DSP output through the resampler into the SDL queue, with
   rate control toward AUDIO_TARGET. */
static void queue_audio(long f)
{
    static int16_t in[4096 * 2], out[8192 * 2];
    int n_in = sched_audio_take(in, 4096);
    if (!audio || qol_audio_muted())
        return;   /* port: QOL fast-forward drops the sound */
    unsigned level = SDL_GetQueuedAudioSize(audio) / 4;
    if (playing && !level)
        audio_underruns++;
    level_avg += 0.05 * (level - level_avg);
    double adjust = rs_rate_control(level_avg, AUDIO_TARGET, AUDIO_MAX_DEV);
    rs_set_adjust(&rs, adjust);
    int n_out = rs_process(&rs, in, n_in, out, 8192);
    if (log_audio)
        fprintf(stderr, "ct_sdl: frame %ld audio queued %u avg %.0f rate x%.4f in %d out %d%s\n",
                f, level, level_avg, adjust, n_in, n_out, playing && !level ? " UNDERRUN" : "");
    if (level > 4 * AUDIO_TARGET)
        audio_dropped++;   /* far ahead (--fast, or the device stalled) */
    else if (n_out > 0)
        SDL_QueueAudio(audio, out, (uint32_t)n_out * 4);
    if (!playing && SDL_GetQueuedAudioSize(audio) / 4 >= AUDIO_TARGET) {
        SDL_PauseAudioDevice(audio, 0);
        playing = 1;
    }
}

/* port: sleep until the performance counter reaches t. SDL_Delay counts
   whole milliseconds, and up to a millisecond of jitter shows on a
   variable-refresh display; nanosleep wakes within tens of microseconds. */
static void sleep_until(uint64_t t)
{
    uint64_t now = SDL_GetPerformanceCounter();
    if (now >= t)
        return;
    uint64_t ns = (t - now) * 1000000000ull / perf_freq;
#if defined(__unix__) || defined(__APPLE__)
    struct timespec ts = {(time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull)};
    while (nanosleep(&ts, &ts))
        ;
#else
    SDL_Delay((uint32_t)(ns / 1000000));
#endif
}

static void set_vsync(int on)
{
    if (hd2d_mode)
        SDL_GL_SetSwapInterval(on);
    else if (ren)
        SDL_RenderSetVSync(ren, on);
}

/* port: one frame edge's interval into the statistics; a vsync that
   doesn't hold the frame rate near 60 Hz gives way to the timer. */
static void pace_frame(void)
{
    uint64_t now = SDL_GetPerformanceCounter();
    if (pace.last) {
        double ms = (double)(now - pace.last) * 1000.0 / (double)perf_freq;
        if (!pace.n || ms < pace.min)
            pace.min = ms;
        if (!pace.n || ms > pace.max)
            pace.max = ms;
        if (!pace.wn || ms < pace.wmin)
            pace.wmin = ms;
        if (!pace.wn || ms > pace.wmax)
            pace.wmax = ms;
        pace.n++, pace.wn++;
        pace.sum += ms, pace.wsum += ms;
        if (ms > 25.0)
            pace.late++, pace.wlate++;
        if (pace.wn == 60) {
            if (log_pacing)
                pace_print("60", pace.wn, pace.wsum, pace.wmin, pace.wmax, pace.wlate);
            double mean = pace.wsum / pace.wn;
            static long catchups_seen;
            /* Not on the first window (start-up) or one with catch-up
               frames, which run fast. */
            int caught_up = pace_catchups != catchups_seen || pace.n == 60;
            catchups_seen = pace_catchups;
            if (pace_vsync && !caught_up && (mean < 16.2 || mean > 17.4)) {
                fprintf(stderr, "ct_sdl: pacing: vsync gives %.2f ms frames, not ~16.67: "
                                "using the timer\n", mean);
                set_vsync(0);
                pace_vsync = pace_catchup = 0;
            }
            pace.wn = pace.wlate = 0;
            pace.wsum = 0;
        }
    }
    pace.last = now;
}

/* port: vsync or the timer (--pacing), once the window is open. */
static void choose_pacing(void)
{
    SDL_DisplayMode m = {0};
    int idx = win ? SDL_GetWindowDisplayIndex(win) : -1;
    if (idx < 0 || SDL_GetCurrentDisplayMode(idx, &m))
        m.refresh_rate = 0;
    /* auto is the timer for now: vsync (--pacing vsync) loses a frame for
       every missed refresh and catches up only through the sound queue
       check in on_frame, and it is not yet measured on a real Game Mode
       display. */
    int want = !fast && pace_opt == PACE_VSYNC;
    int ok = 0;
    if (want)
        ok = hd2d_mode ? !SDL_GL_SetSwapInterval(1) : ren && !SDL_RenderSetVSync(ren, 1);
    pace_vsync = ok;
    if (!ok)
        set_vsync(0);
    fprintf(stderr, "ct_sdl: display %dx%d at %d Hz: pacing by %s%s\n", m.w, m.h,
            m.refresh_rate, ok ? "vsync" : "the 60.0988 Hz timer",
            want && !ok ? " (vsync unavailable)" : "");
}

/* Frame edge (sched_set_frame_hook): frame f is complete. Present it and
   its audio, wait for its time slot, then read input for frame f+1. Runs
   at the same point whether the CPU is interpreted or inside a native
   function, so a native call spanning frames can't bunch them up. */
/* port: draw this frame (also while paused: QOL). */
static void present_now(void)
{
    if (hd2d_mode)
        present_hd2d();
    else
        present_plain(turn_tag(sched_frame()));   /* port: with the touch overlay, the co-op tag */
}

static void on_frame(long f)
{
    exit_frame = f;
    int show = qol_frame(f);   /* port: QOL: fast-forward and the battery's 30 Hz skip some */
    queue_audio(f);
    if (pace_vsync && playing && audio && !fast) {
        /* port: a frame that missed its refresh is lost under vsync, and the
           sound queue drains by a frame each time: when it runs low, stop
           waiting for vsync until the queue is back near its target. */
        unsigned lvl = SDL_GetQueuedAudioSize(audio) / 4;
        if (!pace_catchup && lvl < 2 * AUDIO_BUFFER) {   /* two device buffers from empty */
            pace_catchup = 1;
            pace_catchups++;
            set_vsync(0);
        } else if (pace_catchup && lvl >= AUDIO_TARGET) {
            pace_catchup = 0;
            set_vsync(1);
        }
    }
    const uint8_t *fb = sched_frame();   /* B G R x = ARGB8888 little-endian */
    {   /* port: split screen, the plain path only */
        int was = split_on;
        split_on = !hd2d_mode && sched_views() > 1 && sched_render_view2(view2_px);
        if (split_on != was)
            fprintf(stderr, "ct_sdl: frame %ld: split screen %s\n", f, split_on ? "on" : "off");
    }
    {   /* port: co-op: whose turn, on screen in HD-2D (the plain picture has the tag) */
        static int last_turn = -1;
        int t = coop_players > 1 ? sched_turn_player() : -1;
        if (t != last_turn && t > 0 && hd2d_mode) {
            char msg[32];
            snprintf(msg, sizeof msg, "P%d's turn", t + 1);
            hd2d_osd(msg);
        }
        last_turn = t;
    }
    if (hd2d_mode && show) {
        /* port: this frame's asset pack replacements */
        if (packs_active() && sched_prov()) {
            uint64_t t0 = SDL_GetPerformanceCounter();
            pack_map_pixels += packs_map_frame(sched_prov(), sched_prov_mem(), pack_map);
            pack_map_ms += (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 /
                           (double)SDL_GetPerformanceFrequency();
            pack_map_frames++;
            hd2d_pack_map(pack_map);
        } else {
            hd2d_pack_map(NULL);
        }
    }
    if (show)
        present_now();
    if (hd2d_mode)   /* The PPU's behind G-buffer only while the look uses it. */
        sched_set_behind(hd2d_wants_behind());
    if (n_shots)
        take_shots(f);
    if (pack_dump)   /* port: new tiles and sprites into the pack */
        packs_dump_frame(f, sched_prov(), sched_prov_mem());
    if (dump)
        png_dump_frame(dump, f, fb, SCHED_WIDTH, SCHED_HEIGHT);
    if (!fast) {   /* port: QOL fast-forward: qol_speed() frames per frame time */
        next_frame += perf_freq * FRAME_NS / 1000000000ull / (unsigned)qol_speed();
        uint64_t now = SDL_GetPerformanceCounter();
        if (pace_vsync) {
            /* port: the swap above waited for the display; the timer only
               keeps a vsync that stops waiting (a minimized window, a
               compositor change) from running the game fast. */
            /* Behind (a 60.000 Hz display, or a late frame): no sleep, and
               a resync only past 100 ms, as for the timer, so a vsync that
               doesn't wait still catches up after a slow frame. */
            if (now + perf_freq / 1000 < next_frame - perf_freq * FRAME_NS / 1000000000ull)
                sleep_until(next_frame - perf_freq * FRAME_NS / 1000000000ull);
            else if (now > next_frame + perf_freq / 10)
                next_frame = now;
        } else if (now < next_frame) {
            sleep_until(next_frame);
        } else if (now - next_frame > perf_freq / 10) {
            next_frame = now;   /* more than 100 ms behind: resync, don't sprint */
        }
        pace_frame();
    }
    saves_frame(f);   /* port */
    poll_input(f);
    set_input(f + 1);
}

static void close_record(void)
{
    replay_rec_close(&rec);
}

/* The script's reset button (replay.h "reset F"). */
static int script_reset(long frame)
{
    return replay_reset(&script, frame);
}

/* port: the window, plain or HD-2D (moved out of main so the first-run
   screens can open it before the ROM is known). Returns 0, or 1 with the
   reason set. */
static int open_window(const char *title, int scale, int fullscreen)
{
    /* port: under gamescope (Steam Game Mode, handheld PCs) the
       window is the whole screen anyway: start fullscreen. */
    if (game_mode)
        fullscreen = 1;
    else if (!fullscreen && wst.fs)
        fullscreen = 1;   /* port: as it was left */
    int ww = hd2d_mode ? SCHED_WIDTH * scale * 4 / 3 : SCHED_WIDTH * scale, wh = SCHED_HEIGHT * scale;
    int wx = SDL_WINDOWPOS_CENTERED, wy = SDL_WINDOWPOS_CENTERED;
    if (!game_mode && !wst.scale_given && wst.have_size) {
        ww = wst.w;
        wh = wst.h;
        if (wst.have_pos && wst_on_screen(wst.x, wst.y, ww)) {
            wx = wst.x;
            wy = wst.y;
        }
    }
    Uint32 wflags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                    (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (hd2d_mode) {
        /* OpenGL 3.3 core, else OpenGL ES 3.0. */
        static const struct { int profile, major, minor, gles; } want_gl[] = {
            {SDL_GL_CONTEXT_PROFILE_CORE, 3, 3, 0},
            {SDL_GL_CONTEXT_PROFILE_ES, 3, 0, 1},
        };
        for (size_t k = 0; k < sizeof want_gl / sizeof *want_gl && !glctx; k++) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, want_gl[k].profile);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, want_gl[k].major);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, want_gl[k].minor);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            win = SDL_CreateWindow(title, wx, wy, ww, wh,
                                   wflags | SDL_WINDOW_OPENGL);
            glctx = win ? SDL_GL_CreateContext(win) : NULL;
            if (glctx && hd2d_init(want_gl[k].gles)) {
                SDL_GL_DeleteContext(glctx);
                glctx = NULL;
            }
            if (glctx)   /* port: the touch overlay's GL drawer */
                touch_gl_ok = !touch_gl_init(want_gl[k].gles);
            if (!glctx && win) {
                SDL_DestroyWindow(win);
                win = NULL;
            }
        }
        if (!glctx) {
            fprintf(stderr, "ct_sdl: no usable OpenGL 3.3 / ES 3.0 context: %s\n", SDL_GetError());
            set_exit_reason("HD-2D renderer setup failed");
            return 1;
        }
        SDL_GL_SetSwapInterval(0);   /* paced by the frame timer; choose_pacing may change it */
        /* port: the Steam Deck's APU gets the lighter look, unless the
           user saved or asked for one. */
        const char *gl = NULL;
        int deck = device_gl_is_deck(&gl) || device_kind() == DEVICE_STEAM_DECK;
        fprintf(stderr, "ct_sdl: GL renderer \"%s\", device \"%s\"%s\n", gl,
                device_product(), deck ? ": Steam Deck" : "");
        if (deck && !preset_saved && !preset_given) {
            hd2d_set_preset(HD2D_DECK);
            fprintf(stderr, "ct_sdl: hd2d preset deck (Steam Deck default)\n");
        }
    } else {
        win = SDL_CreateWindow(title, wx, wy, ww, wh, wflags);
        ren = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
        if (win && !ren)
            ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                                   SDL_TEXTUREACCESS_STREAMING, SCHED_WIDTH,
                                                   SCHED_HEIGHT) : NULL;
        tex2 = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,   /* port: split */
                                       SDL_TEXTUREACCESS_STREAMING, SCHED_WIDTH, SCHED_HEIGHT)
                   : NULL;
        if (!tex) {
            fprintf(stderr, "ct_sdl: window: %s\n", SDL_GetError());
            set_exit_reason("window or renderer creation failed");
            return 1;
        }
        /* port: integer scaling is done by present_plain (touch.h) */
    }
    if (game_mode)   /* port: nothing to point at on a handheld */
        SDL_ShowCursor(SDL_DISABLE);
    wst_track();
    wst.w0 = wst.w, wst.h0 = wst.h, wst.x0 = wst.x, wst.y0 = wst.y;
    wst.fs0 = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN ? 1 : 0;
    if (game_mode)
        wst.fs0 = 1;
    {
        int w = 0, h = 0;
        if (hd2d_mode)
            SDL_GL_GetDrawableSize(win, &w, &h);
        else
            SDL_GetRendererOutputSize(ren, &w, &h);
        fprintf(stderr, "ct_sdl: window %dx%d%s\n", w, h,
                SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN ? ", fullscreen" : "");
    }
    choose_pacing();
    return 0;
}

#ifdef CT_FIRSTRUN
/* port: the first-run screens: frames of text, no layer information. */
static uint8_t status_layers[SCHED_WIDTH * SCHED_HEIGHT];

static void present_status(const uint8_t *bgrx)
{
    if (hd2d_mode) {
        int w, h;
        SDL_GL_GetDrawableSize(win, &w, &h);
        hd2d_set_scene(NULL);
        hd2d_render(bgrx, status_layers, NULL, NULL, NULL, w, h, 0);
        SDL_GL_SwapWindow(win);
    } else {
        present_plain(bgrx);
    }
}

/* PATH may hold one %d: numbered shots, one per change, from 0. */
static void shot_status(const uint8_t *bgrx, const char *path)
{
    static int seq;
    char name[4096];
    const char *pct = strchr(path, '%');
    if (pct && pct[1] == 'd' && !strchr(pct + 1, '%'))
        snprintf(name, sizeof name, "%.*s%d%s", (int)(pct - path), path, seq++, pct + 2);
    else
        snprintf(name, sizeof name, "%s", path);
    path = name;
    int ok = write_shot(path, bgrx, status_layers, NULL, NULL, NULL);
    fprintf(stderr, "ct_sdl: setup screen: %s %s\n", ok ? "wrote" : "cannot write", path);
}
#endif

/* port: the options, and the keys (--help prints both). */
static void usage(FILE *out)
{
    fprintf(out, "usage: ct_sdl [--scale N] [--frames N] [--dump DIR]\n         "
            "[--needed-hw FILE] [--fast] [--require-render] [--log-input]\n         "
            "[--interp-only] [--record FILE] [--script FILE] [--log-audio]\n         "
            "[--require-no-underrun] [--hd2d] [--no-hd2d] [--preset off|subtle|hd2d|deck|hd2d3d]\n         "
            "[--aspect 4:3|pixel|stretch] [--debug-view N] [--fullscreen]\n         "
            "[--log-gpu] [--shot F:PATH]... [--shot-size WxH] [--module FILE]\n         "
            "[--rom FILE] [--touch] [--touch-hide SECS] [--touch-test FILE]\n         "
            "[--save FILE] [--no-save] [--dump-sram FILE]\n         "
            "[--poke-sram F:ADDR=VAL]... [--pacing auto|timer|vsync] [--log-pacing]\n         "
            "[--coop] [--no-coop] [--coop-split]"
            " "
            "[--pack-dump NAME] [--pack NAME|none]..."
#ifdef CT_FIRSTRUN
            " [--data-dir DIR] [--game ID] [--shot status:PATH] [--build-only]"
#endif
            "\n");
    fputs("\n"
          "Keyboard (window)\n"
          "  Arrow keys        d-pad            Enter       Start\n"
          "  X                 A                Right Shift Select\n"
          "  Z                 B                Q / W       L / R\n"
          "  A / S             Y / X\n"
          "  F11               fullscreen on / off\n"
          "  F1                HD-2D look: original, subtle, HD-2D, Steam Deck, 3D\n"
          "  F2                HD-2D debug view      F3  aspect: 4:3, pixel 8:7, stretch\n"
          "  F4                co-op on / off (two players: a second controller is pad 2)\n"
          "  F5                reload the asset packs (--pack)\n"
          "  F6 / F9           quick save / quick load   F8   screenshot\n"
          "  Tab (hold)        fast-forward              F10  menu\n"
          "  Ctrl+Q            quit\n"
          "Back buttons and grips (Steam Deck L4 R4 L5 R5, Steam Controller grips, Elite\n"
          "  paddles): L4 quick save, R4 quick load, L5 fast-forward (hold), R5 menu.\n"
          "  settings.ini: bind_paddle1..4=ACTION, key_ACTION=KEY (docs/HANDHELD.md).\n"
          "Controllers: south = B, east = A, west = Y, north = X, shoulders = L / R,\n"
          "  Start, Back = Select, d-pad or left stick. The window's size, place and\n"
          "  fullscreen state, the look and the aspect are remembered between runs.\n"
          "Co-op: on by itself when two controllers are connected (pad 1: keyboard and\n"
          "  the first; pad 2: the second, for party members 2 and 3); F4, --coop or\n"
          "  --no-coop change it. --coop-split (experimental): a second view when\n"
          "  player 2 walks away.\n",
          out);
    qol_usage(out);   /* port: QOL */
}

/* ---- port: what QOL (qol.h) borrows ---- */

static void qol_audio_restart(void)
{
    if (!audio)
        return;
    SDL_PauseAudioDevice(audio, 1);
    SDL_ClearQueuedAudio(audio);
    playing = 0;   /* playback starts again once the queue is back at its target */
    level_avg = AUDIO_TARGET;
    rs_init(&rs, SCHED_AUDIO_HZ, RATE);
}

static void qol_pacing_reset(void)
{
    next_frame = SDL_GetPerformanceCounter();
    pace.last = 0;
}

static void qol_poll(void)
{
    poll_input(sched_frame_count());
}

/* What is on screen, at the window's size. */
static int qol_shot(const char *path)
{
    int w, h;
    if (!hd2d_mode) {
        SDL_GetRendererOutputSize(ren, &w, &h);
        SDL_UpdateTexture(tex, NULL, turn_tag(sched_frame()), SCHED_WIDTH * 4);
        return qol_plain_shot(ren, tex, w, h, path);
    }
    SDL_GL_GetDrawableSize(win, &w, &h);
    int sw = shot_w, sh = shot_h;
    shot_w = w, shot_h = h;
    int ok = write_shot(path, sched_frame(), sched_layers(), sched_behind(),
                        sched_behind_layers(), sched_ppu_state_get());
    shot_w = sw, shot_h = sh;
    return ok ? 0 : -1;
}

int main(int argc, char **argv)
{
    static long frames = -1;           /* static: survive the longjmp */
    static int scale = 3, require_render, require_no_underrun, fullscreen;
    static const char *needed, *module_path, *rom_arg;
#ifdef CT_FIRSTRUN
    static const char *data_dir = CT_FIRSTRUN_DATA_DIR, *game_id = CT_FIRSTRUN_GAME,
                      *status_shot;
    static int interp_only, build_only;
#else
    static const char *game_id = CT_GAME_ID;   /* port: names the saves folder */
#endif
    static const char *save_path;   /* port: --save */
    static int no_save;
    static const char *import_srm, *export_srm;   /* port: QOL */
    static int recording, scripted;
    settings_load();   /* port: saved HD-2D settings; options below override */
    for (int k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--scale") && k + 1 < argc)
            scale = atoi(argv[++k]), wst.scale_given = 1;
        else if (!strcmp(argv[k], "--frames") && k + 1 < argc)
            frames = atol(argv[++k]);
        else if (!strcmp(argv[k], "--dump") && k + 1 < argc)
            dump = argv[++k];
        else if (!strcmp(argv[k], "--needed-hw") && k + 1 < argc)
            needed = argv[++k];
        else if (!strcmp(argv[k], "--fast"))
            fast = 1;
        else if (!strcmp(argv[k], "--require-render"))
            require_render = 1;
        else if (!strcmp(argv[k], "--log-input"))
            log_input = 1;
        else if (!strcmp(argv[k], "--log-audio"))
            log_audio = 1;
        else if (!strcmp(argv[k], "--require-no-underrun"))
            require_no_underrun = 1;
        else if (!strcmp(argv[k], "--interp-only")) {
            sched_set_native(0);
#ifdef CT_FIRSTRUN
            interp_only = 1;
#endif
        }
        else if (!strcmp(argv[k], "--record") && k + 1 < argc) {
            if (replay_rec_open(&rec, argv[++k], "ct_sdl --record")) {
                fprintf(stderr, "ct_sdl: cannot write %s\n", argv[k]);
                return 2;
            }
            atexit(close_record);
            recording = 1;
        } else if (!strcmp(argv[k], "--script") && k + 1 < argc) {
            if (replay_load(&script, argv[++k])) {
                fprintf(stderr, "ct_sdl: bad --script %s\n", argv[k]);
                return 2;
            }
            scripted = 1;
        } else if (!strcmp(argv[k], "--module") && k + 1 < argc) {
            module_path = argv[++k];
        } else if (!strcmp(argv[k], "--rom") && k + 1 < argc) {
            rom_arg = argv[++k];
#ifdef CT_FIRSTRUN
        } else if (!strcmp(argv[k], "--data-dir") && k + 1 < argc) {
            data_dir = argv[++k];
        } else if (!strcmp(argv[k], "--game") && k + 1 < argc) {
            game_id = argv[++k];
        } else if (!strcmp(argv[k], "--shot") && k + 1 < argc &&
                   !strncmp(argv[k + 1], "status:", 7)) {
            status_shot = argv[++k] + 7;
        } else if (!strcmp(argv[k], "--build-only")) {
            build_only = 1;
#endif
        } else if (!strcmp(argv[k], "--save") && k + 1 < argc) {
            save_path = argv[++k];
        } else if (!strcmp(argv[k], "--no-save")) {
            no_save = 1;
        } else if (!strcmp(argv[k], "--dump-sram") && k + 1 < argc) {
            saves_dump_at_exit(argv[++k]);
        } else if (!strcmp(argv[k], "--poke-sram") && k + 1 < argc) {
            if (saves_poke(argv[++k])) {
                fprintf(stderr, "ct_sdl: --poke-sram FRAME:ADDR=VAL (ADDR, VAL hex)\n");
                return 2;
            }
        } else if (!strcmp(argv[k], "--coop")) {   /* port: co-op */
            coop_pref = COOP_ON;
        } else if (!strcmp(argv[k], "--no-coop")) {
            coop_pref = COOP_OFF;
        } else if (!strcmp(argv[k], "--coop-split")) {   /* port: experimental */
            coop_pref = COOP_ON;
            sched_set_views(2);
        } else if (!strcmp(argv[k], "--pack-dump") && k + 1 < argc) {   /* port */
            pack_dump = argv[++k];
        } else if (!strcmp(argv[k], "--pack") && k + 1 < argc) {   /* port: asset packs */
            if (!packs_given)
                n_pack_names = 0;   /* the command line replaces the saved list */
            packs_given = 1;
            const char *v = argv[++k];
            if (!strcmp(v, "none"))
                n_pack_names = 0;
            else if (n_pack_names < MAX_PACK_NAMES && strlen(v) < sizeof pack_names[0])
                snprintf(pack_names[n_pack_names++], sizeof pack_names[0], "%s", v);
        } else if (!strcmp(argv[k], "--hd2d")) {
            hd2d_mode = 1;
        } else if (!strcmp(argv[k], "--no-hd2d")) {   /* port: beats the Game Mode default */
            hd2d_mode = -1;
        } else if (!strcmp(argv[k], "--pacing") && k + 1 < argc) {
            const char *v = argv[++k];
            pace_opt = !strcmp(v, "timer") ? PACE_TIMER : !strcmp(v, "vsync") ? PACE_VSYNC
                     : !strcmp(v, "auto") ? PACE_AUTO : -1;
            if (pace_opt < 0) {
                fprintf(stderr, "ct_sdl: --pacing: auto, timer or vsync\n");
                return 2;
            }
        } else if (!strcmp(argv[k], "--log-pacing")) {
            log_pacing = 1;
        } else if (!strcmp(argv[k], "--preset") && k + 1 < argc) {
            int p = parse_preset(argv[++k]);
            if (p < 0) {
                fprintf(stderr, "ct_sdl: --preset: off, subtle, hd2d, deck or hd2d3d\n");
                return 2;
            }
            hd2d_set_preset(p);
            preset_given = 1;
        } else if (!strcmp(argv[k], "--aspect") && k + 1 < argc) {
            const char *v = argv[++k];
            hd2d_set_aspect(!strcmp(v, "pixel") ? HD2D_ASPECT_PIXEL
                            : !strcmp(v, "stretch") ? HD2D_ASPECT_STRETCH
                                                    : HD2D_ASPECT_4_3);
        } else if (!strcmp(argv[k], "--debug-view") && k + 1 < argc) {
            hd2d_set_debug(atoi(argv[++k]));
        } else if (!strcmp(argv[k], "--fullscreen")) {
            fullscreen = 1;
        } else if (!strcmp(argv[k], "--log-gpu")) {
            log_gpu = 1;
            hd2d_set_timing(1);
        } else if (!strcmp(argv[k], "--shot") && k + 1 < argc) {
            char *v = argv[++k], *colon = strchr(v, ':');
            if (!colon || n_shots == MAX_SHOTS) {
                fprintf(stderr, "ct_sdl: --shot FRAME:PATH\n");
                return 2;
            }
            shots[n_shots].frame = atol(v);
            shots[n_shots].path = colon + 1;
            n_shots++;
        } else if (!strcmp(argv[k], "--touch")) {   /* port: touch overlay */
            touch_o.force = 1;
        } else if (!strcmp(argv[k], "--touch-hide") && k + 1 < argc) {
            touch_o.hide_seconds = atoi(argv[++k]);
        } else if (!strcmp(argv[k], "--touch-test") && k + 1 < argc) {
            if (touch_test_load(argv[++k]))
                return 2;
        } else if (!strcmp(argv[k], "--shot-size") && k + 1 < argc) {
            if (sscanf(argv[++k], "%dx%d", &shot_w, &shot_h) != 2 || shot_w < 16 || shot_h < 16) {
                fprintf(stderr, "ct_sdl: --shot-size WxH\n");
                return 2;
            }
        } else if (!strcmp(argv[k], "--help") || !strcmp(argv[k], "-h")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(argv[k], "--import-srm") && k + 1 < argc) {   /* port: QOL */
            import_srm = argv[++k];
        } else if (!strcmp(argv[k], "--export-srm") && k + 1 < argc) {
            export_srm = argv[++k];
        } else if (qol_option(argc, argv, &k) > 0) {
            /* port: QOL options (qol.h); a bad one is a usage error below */
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (import_srm || export_srm) {   /* port: QOL: a save RAM file in or out, then stop */
        if (import_srm && export_srm) {
            fprintf(stderr, "ct_sdl: --import-srm or --export-srm, not both\n");
            return 2;
        }
        return import_srm ? saves_import(import_srm, save_path, game_id)
                          : saves_export(export_srm, save_path, game_id);
    }
    if (scale < 1)
        scale = 1;
    {   /* port: Steam Game Mode (device.h): fullscreen, HD-2D unless --no-hd2d */
        const char *why;
        game_mode = device_game_mode(&why);
        if (game_mode)
            fprintf(stderr, "ct_sdl: Steam Game Mode (%s): fullscreen, no cursor%s\n", why,
                    hd2d_mode >= 0 ? ", HD-2D" : "");
        if (game_mode && !hd2d_mode)
            hd2d_mode = 1;
        int no_hd2d = hd2d_mode < 0;
        if (hd2d_mode < 0)
            hd2d_mode = 0;
        /* port: asset packs replace art in the GL renderer: with packs and
           the plain look, draw through it with the original look. */
        if (n_pack_names && no_hd2d) {
            fprintf(stderr, "ct_sdl: --no-hd2d: asset packs not loaded (they need the GL renderer)\n");
        } else if (n_pack_names) {
            int r = packs_setup(game_id);
            if (r < 0)
                fprintf(stderr, "ct_sdl: no asset pack could be read\n");
            else if (packs_active() && !hd2d_mode) {
                hd2d_mode = 1;
                if (!preset_given) {
                    preset_forced = hd2d_preset();
                    hd2d_set_preset(HD2D_OFF);
                }
                fprintf(stderr, "ct_sdl: asset packs: drawing through the GL renderer\n");
            }
        }
    }
    quit_combo_init(&quitc);
    touch_init(&touch_o);   /* port */
    atexit(report_exit);

#ifdef CT_FIRSTRUN
    if (build_only) {   /* port: --build-only: no window, no audio */
        if (module_path || interp_only) {
            fprintf(stderr, "ct_sdl: --build-only builds the module: not with --module or "
                            "--interp-only\n");
            return 2;
        }
        if (SDL_Init(SDL_INIT_EVENTS)) {
            fprintf(stderr, "ct_sdl: SDL_Init: %s\n", SDL_GetError());
            set_exit_reason("SDL_Init failed");
            return 1;
        }
        firstrun_opts fo = {rom_arg, data_dir, game_id, 1, NULL, NULL, NULL, 1};
        int r = firstrun(&fo);
        set_exit_reason(r == FIRSTRUN_MODULE ? "--build-only: module ready"
                                             : "--build-only: no module");
        SDL_Quit();
        return r == FIRSTRUN_MODULE ? 0 : 1;
    }
#endif
    input_sdl_hints(steam_hidapi);   /* port: Deck / Steam Controller back buttons */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER)) {
        fprintf(stderr, "ct_sdl: SDL_Init: %s\n", SDL_GetError());
        set_exit_reason("SDL_Init failed");
        return 1;
    }
    static CPU cpu;
    int first_run = 0;
#ifdef CT_FIRSTRUN
    if (!module_path) {   /* port: engine-only first run (firstrun.h) */
        if (open_window("ct-recomp", scale, fullscreen))
            return 1;
        int preset = hd2d_preset(), view = hd2d_debug();
        hd2d_set_preset(HD2D_OFF);   /* the setup screens in the original look */
        hd2d_set_debug(HD2D_DEBUG_NONE);
        firstrun_opts fo = {rom_arg, data_dir, game_id, !interp_only, present_status,
                            shot_status, status_shot, 0};
        int r = firstrun(&fo);
        hd2d_set_preset(preset);
        hd2d_set_debug(view);
        if (r == FIRSTRUN_QUIT) {
            set_exit_reason("quit during setup");
            return 0;
        }
        if (r == FIRSTRUN_ERROR) {
            set_exit_reason("setup failed");
            return 2;
        }
        if (r == FIRSTRUN_INTERP)
            fprintf(stderr, "ct_sdl: no game module: running in the interpreter only\n");
        first_run = 1;
    }
#endif
    if (!first_run)
        bus_init(rom_arg);
    if (module_path) {   /* port: game code from a module (runtime/module.h) */
        const char *err;
        if (ct_module_load(module_path, &err)) {
            fprintf(stderr, "ct_sdl: --module: %s\n", err);
            set_exit_reason("game module failed to load");
            return 2;
        }
        fprintf(stderr, "ct_sdl: game module %s: %u functions\n", module_path, ct_func_count);
    }
    if (!no_save)   /* port: the save RAM from disk (saves.h) */
        saves_open(save_path, game_id);
    char title[22];   /* ROM header title, $FFC0, 21 bytes, space padded */
    memcpy(title, bus_rom() + 0xFFC0, 21);
    title[21] = 0;
    for (int k = 20; k >= 0 && (title[k] == ' ' || !title[k]); k--)
        title[k] = 0;
    if (win)
        SDL_SetWindowTitle(win, title);
    else if (open_window(title, scale, fullscreen))
        return 1;
    if (hd2d_mode) {
        /* port: say which look is on and how to change it. */
        static char hello[64];
        snprintf(hello, sizeof hello, "Look: %s - [r3] to change",   /* F1 on a keyboard */
                 hd2d_preset() == HD2D_OFF ? "original" : hd2d_preset() == HD2D_SUBTLE ? "subtle"
                 : hd2d_preset() == HD2D_DECK ? "HD-2D Steam Deck" : "HD-2D");
        hd2d_osd(hello);
        /* port: the driver's first-draw work now, not on frame 1 with
           audio playing. */
        int ww, wh;
        SDL_GL_GetDrawableSize(win, &ww, &wh);
        uint64_t tw = SDL_GetPerformanceCounter();
        hd2d_warmup(ww, wh);
        fprintf(stderr, "ct_sdl: hd2d warm-up %dx%d: %.1f ms\n", ww, wh,
                (double)(SDL_GetPerformanceCounter() - tw) * 1000.0 / SDL_GetPerformanceFrequency());
        if (packs_active()) {   /* port: asset packs: the atlas to the GPU */
            packs_upload();
            atexit(pack_map_report);
            static char msg[48];
            int n;
            packs_entries(&n);
            snprintf(msg, sizeof msg, "Packs: %d replacements - F5 reloads", n);
            hd2d_osd(msg);
        }
        if (packs_given)
            settings_save();   /* port: remember the packs */
    }

    SDL_AudioSpec want = {0}, have;
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = AUDIO_BUFFER;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audio)
        fprintf(stderr, "ct_sdl: no audio: %s\n", SDL_GetError());
    rs_init(&rs, SCHED_AUDIO_HZ, RATE);
    print_controllers();
    pad = open_controller();
    if (pad)
        fprintf(stderr, "ct_sdl: using \"%s\" on pad 1\n", SDL_GameControllerName(pad));
    if (sched_views() > 1 && hd2d_mode)
        fprintf(stderr, "ct_sdl: --coop-split: the split screen is drawn by the plain "
                        "picture only (--no-hd2d); HD-2D keeps one view\n");
    pad2 = open_controller_except2(pad, NULL);   /* port: co-op */
    pad3 = open_controller_except2(pad, pad2);
    if (pad2)
        fprintf(stderr, "ct_sdl: second controller \"%s\"\n", SDL_GameControllerName(pad2));
    if (pad3)
        fprintf(stderr, "ct_sdl: third controller \"%s\"\n", SDL_GameControllerName(pad3));
    coop_update();
    /* port: frontend actions (actions.h): stubs say so on screen until
       something implements them; the menu shows what the buttons do. */
    actions_set_message(osd);
    actions_set_trace(log_action);
    actions_set_handler(ACTION_MENU, menu_osd, NULL);
    use_glyphs_of(pad);
    fprintf(stderr, "ct_sdl: glyphs: %s (%d pictures built in)\n",
            glyphs_family_name(glyphs_in_use()), glyphs_built_in());
    {
        char plain[200];
        osd_text_plain("actions: [@quicksave] quick save, [@quickload] quick load, [@ffwd] "
                       "fast-forward (hold), [@menu] menu, [@screenshot] screenshot",
                       plain, sizeof plain);
        fprintf(stderr, "ct_sdl: %s\n", plain);
    }

    interp_reset(&cpu);
    sched_init(&cpu);
    sched_set_behind(hd2d_mode && hd2d_wants_behind());   /* port: see on_frame */
    sched_set_prov(packs_active());   /* port: asset packs read it each frame */
    if (pack_dump) {   /* port: asset pack dump (docs/ASSET-PACKS.md) */
        char *dir = pack_dir(game_id, pack_dump);
        const char *base = strrchr(pack_dump, '/') ? strrchr(pack_dump, '/') + 1 : pack_dump;
        if (!dir || packs_dump_open(dir, base)) {
            set_exit_reason("cannot write the pack dump");
            return 2;
        }
        free(dir);
        atexit(pack_dump_report);
        sched_set_prov(1);
    }
    sched_set_frame_hook(on_frame);
    sched_set_reset_hook(script_reset);
    ct_fatal_hook = on_fatal;
    if (setjmp(fatal_jmp)) {
        static char stop[512];
        snprintf(stop, sizeof stop, "frame %ld, line %d, PC $%02X%04X: %s", sched_frame_count(),
                 sched_line(), cpu.PB, cpu.PC, fatal_msg);
        fprintf(stderr, "ct_sdl: stopped in %s\n", stop);
        set_exit_reason("fatal error: %s", stop);
        if (needed && hw_needed_write(needed, "ct_sdl", stop))
            fprintf(stderr, "ct_sdl: cannot write %s\n", needed);
        SDL_Quit();
        return 1;
    }

    {   /* port: QOL (qol.h): save states, resume, fast-forward, ... */
        static const qol_host qh = {qol_shot, qol_audio_restart, present_now, qol_poll,
                                    qol_pacing_reset, settings_save};
        qol_init(&qh, game_id, game_mode, hd2d_mode, !no_save, recording, scripted);
    }
    key_filter_init(&keys);
    perf_freq = SDL_GetPerformanceFrequency();
    pace.last = 0;   /* port: the setup screens' time is not a frame */
    next_frame = SDL_GetPerformanceCounter();
    poll_input(sched_frame_count());   /* port: a loaded state's frame (QOL), else 0 */
    set_input(sched_frame_count() + 1);
    /* Everything per frame happens in on_frame, at the frame edge; this
       loop only keeps the scheduler running. port: QOL pauses it (qol_idle)
       and saves and loads states between frames. */
    while (running && (frames < 0 || sched_frame_count() < frames)) {
        if (qol_paused()) {
            qol_idle();
            continue;
        }
        sched_run_frame();
        qol_after_frame();
    }
    qol_quit(!running);   /* port: QOL: the resume point, on a quit */

    if (running)
        set_exit_reason("--frames %ld reached", frames);
    exit_frame = sched_frame_count();
    int rendered = 0;
    const uint8_t *fb = sched_frame();
    for (size_t k = 0; k < (size_t)SCHED_WIDTH * SCHED_HEIGHT * 4 && !rendered; k += 4)
        rendered = fb[k] | fb[k + 1] | fb[k + 2];
    printf("ct_sdl: %ld frames, %ld NMIs, PC $%02X%04X\n", sched_frame_count(), sched_nmi_count(),
           cpu.PB, cpu.PC);
    for (unsigned k = 0; k < hw_note_count(); k++)
        printf("ct_sdl: stubbed: %s\n", hw_note_text(k));
    if (needed && hw_needed_write(needed, "ct_sdl", NULL))
        fprintf(stderr, "ct_sdl: cannot write %s\n", needed);
    if (win && !game_mode) {   /* port: remember the window, if it changed */
        wst_track();
        if (wst_dirty())
            settings_save();
    }
    if (pad)
        SDL_GameControllerClose(pad);
    if (pad2)
        SDL_GameControllerClose(pad2);
    if (pad3)
        SDL_GameControllerClose(pad3);
    if (tex2)
        SDL_DestroyTexture(tex2);
    osd_sdl_shutdown();   /* port */
    if (audio)
        SDL_CloseAudioDevice(audio);
    if (glctx) {
        touch_gl_shutdown();   /* port */
        hd2d_shutdown();
        SDL_GL_DeleteContext(glctx);
    }
    SDL_Quit();
    if (require_no_underrun && audio_underruns)
        return 1;
    return require_render && !rendered ? 1 : 0;
}
