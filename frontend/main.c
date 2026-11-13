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
 *   --hd2d [--preset off|subtle|hd2d] [--aspect 4:3|pixel|stretch]
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
 *       engine-only build (ct_sdl_engine) contains none of its own. */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "bus.h"
#include "module.h"   /* port: --module */
#include "hd2d/hd2d.h"
#include "hwlog.h"
#include "input.h"
#include "interp.h"
#include "png.h"
#include "replay.h"
#include "resample.h"
#include "sched.h"

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
        }
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

static SDL_GameController *open_controller(void)
{
    for (int k = 0; k < SDL_NumJoysticks(); k++)
        if (SDL_IsGameController(k))
            return SDL_GameControllerOpen(k);
    return NULL;
}

static replay script;
static replay_rec rec;

/* Host state shared by main and the frame hook. */
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID audio;
static SDL_GameController *pad;
static resampler rs;
static int playing;
static double level_avg = AUDIO_TARGET;
static key_filter keys;
static uint16_t live_buttons;
static uint64_t perf_freq, next_frame;
static int running = 1, fast, log_input, log_audio;
static const char *dump;

/* port: HD-2D presentation and screenshots. */
static SDL_Window *win;
static SDL_GLContext glctx;
static int hd2d_mode, log_gpu;
#define MAX_SHOTS 64
static struct { long frame; const char *path; } shots[MAX_SHOTS];
static int n_shots, shot_w = 1920, shot_h = 1080;

static void take_shots(long f)
{
    for (int k = 0; k < n_shots; k++) {
        if (shots[k].frame != f)
            continue;
        int ok;
        if (hd2d_mode) {
            uint8_t *buf = malloc((size_t)shot_w * shot_h * 4);
            hd2d_render(sched_frame(), sched_layers(), sched_ppu_state_get(), shot_w, shot_h, 1);
            ok = buf && !hd2d_read(buf, shot_w, shot_h) &&
                 !png_write_bgrx(shots[k].path, buf, shot_w, shot_h);
            free(buf);
        } else {
            ok = !png_write_bgrx(shots[k].path, sched_frame(), SCHED_WIDTH, SCHED_HEIGHT);
        }
        fprintf(stderr, "ct_sdl: frame %ld: %s %s\n", f, ok ? "wrote" : "cannot write",
                shots[k].path);
    }
}

static void present_hd2d(void)
{
    int w, h;
    SDL_GL_GetDrawableSize(win, &w, &h);
    hd2d_render(sched_frame(), sched_layers(), sched_ppu_state_get(), w, h, 0);
    SDL_GL_SwapWindow(win);
    if (log_gpu && sched_frame_count() % 60 == 0)
        fprintf(stderr, "ct_sdl: frame %ld hd2d %dx%d %.2f ms\n", sched_frame_count(), w, h,
                hd2d_last_ms());
}

static void settings_save(void);

/* F1-F3, F11: the HD-2D controls. Returns 1 if the key was one of them. */
static int hd2d_key(SDL_Scancode sc)
{
    if (sc == SDL_SCANCODE_F11) {
        int fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        return 1;
    }
    if (!hd2d_mode)
        return 0;
    if (sc == SDL_SCANCODE_F1) {
        hd2d_set_preset((hd2d_preset() + 1) % HD2D_PRESETS);
        fprintf(stderr, "ct_sdl: hd2d preset %s\n", hd2d_preset_name(hd2d_preset()));
        static const char *const msg[HD2D_PRESETS] = {"Look: original", "Look: HD-2D subtle",
                                                      "Look: HD-2D"};
        hd2d_osd(msg[hd2d_preset()]);
        settings_save();
    } else if (sc == SDL_SCANCODE_F2) {
        hd2d_set_debug((hd2d_debug() + 1) % HD2D_DEBUGS);
        fprintf(stderr, "ct_sdl: hd2d debug view %d\n", hd2d_debug());
        static const char *const msg[HD2D_DEBUGS] = {"Debug: off", "Debug: layers", "Debug: depth",
                                                     "Debug: blur", "Debug: compare"};
        hd2d_osd(msg[hd2d_debug()]);
    } else if (sc == SDL_SCANCODE_F3) {
        hd2d_set_aspect((hd2d_aspect() + 1) % HD2D_ASPECTS);
        settings_save();
        static const char *const msg[HD2D_ASPECTS] = {"Aspect: 4:3", "Aspect: pixel 8:7",
                                                      "Aspect: stretch"};
        hd2d_osd(msg[hd2d_aspect()]);
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
        if (sscanf(line, "preset=%63s", val) == 1 && parse_preset(val) >= 0)
            hd2d_set_preset(parse_preset(val));
        else if (sscanf(line, "aspect=%d", &n) == 1)
            hd2d_set_aspect(n);
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
        fprintf(f, "preset=%s\naspect=%d\n", hd2d_preset_name(hd2d_preset()), hd2d_aspect());
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

/* Window, keyboard and controller events; live_buttons for pad 1. */
static void poll_input(long f)
{
    keys.controller = pad != NULL;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (log_input && (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && !ev.key.repeat)
            fprintf(stderr, "ct_sdl: frame %ld key %s %s (scancode %d)\n", f,
                    SDL_GetScancodeName(ev.key.keysym.scancode),
                    ev.type == SDL_KEYDOWN ? "down" : "up", ev.key.keysym.scancode);
        if (log_input && (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP))
            fprintf(stderr, "ct_sdl: frame %ld controller button %s %s\n", f,
                    SDL_GameControllerGetStringForButton(ev.cbutton.button),
                    ev.type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up");
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
        } else if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad)
            pad = open_controller();
        else if (ev.type == SDL_CONTROLLERDEVICEREMOVED && pad &&
                 !SDL_GameControllerGetAttached(pad)) {
            SDL_GameControllerClose(pad);
            pad = open_controller();
        }
    }
    uint16_t buttons = input_keyboard(keys.held);
    if (pad)
        buttons |= input_controller(pad_button, pad_axis, pad);
    live_buttons = buttons;
}

/* Pad 1 for frame f: live input plus the --script, recorded. */
static void set_input(long f)
{
    uint16_t b = live_buttons | replay_buttons(&script, f);
    sched_set_joypad(0, b);
    replay_rec_frame(&rec, f, b);
}

/* This frame's DSP output through the resampler into the SDL queue, with
   rate control toward AUDIO_TARGET. */
static void queue_audio(long f)
{
    static int16_t in[4096 * 2], out[8192 * 2];
    int n_in = sched_audio_take(in, 4096);
    if (!audio)
        return;
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

/* Frame edge (sched_set_frame_hook): frame f is complete. Present it and
   its audio, wait for its time slot, then read input for frame f+1. Runs
   at the same point whether the CPU is interpreted or inside a native
   function, so a native call spanning frames can't bunch them up. */
static void on_frame(long f)
{
    exit_frame = f;
    queue_audio(f);
    const uint8_t *fb = sched_frame();   /* B G R x = ARGB8888 little-endian */
    if (hd2d_mode) {
        present_hd2d();
    } else {
        SDL_UpdateTexture(tex, NULL, fb, SCHED_WIDTH * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    if (n_shots)
        take_shots(f);
    if (dump)
        png_dump_frame(dump, f, fb, SCHED_WIDTH, SCHED_HEIGHT);
    if (!fast) {
        next_frame += perf_freq * FRAME_NS / 1000000000ull;
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < next_frame)
            SDL_Delay((uint32_t)((next_frame - now) * 1000 / perf_freq));
        else if (now - next_frame > perf_freq / 10)
            next_frame = now;   /* more than 100 ms behind: resync, don't sprint */
    }
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

int main(int argc, char **argv)
{
    static long frames = -1;           /* static: survive the longjmp */
    static int scale = 3, require_render, require_no_underrun, fullscreen;
    static const char *needed, *module_path;
    settings_load();   /* port: saved HD-2D settings; options below override */
    for (int k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--scale") && k + 1 < argc)
            scale = atoi(argv[++k]);
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
        else if (!strcmp(argv[k], "--interp-only"))
            sched_set_native(0);
        else if (!strcmp(argv[k], "--record") && k + 1 < argc) {
            if (replay_rec_open(&rec, argv[++k], "ct_sdl --record")) {
                fprintf(stderr, "ct_sdl: cannot write %s\n", argv[k]);
                return 2;
            }
            atexit(close_record);
        } else if (!strcmp(argv[k], "--script") && k + 1 < argc) {
            if (replay_load(&script, argv[++k])) {
                fprintf(stderr, "ct_sdl: bad --script %s\n", argv[k]);
                return 2;
            }
        } else if (!strcmp(argv[k], "--module") && k + 1 < argc) {
            module_path = argv[++k];
        } else if (!strcmp(argv[k], "--hd2d")) {
            hd2d_mode = 1;
        } else if (!strcmp(argv[k], "--preset") && k + 1 < argc) {
            int p = parse_preset(argv[++k]);
            if (p < 0) {
                fprintf(stderr, "ct_sdl: --preset: off, subtle or hd2d\n");
                return 2;
            }
            hd2d_set_preset(p);
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
        } else if (!strcmp(argv[k], "--shot-size") && k + 1 < argc) {
            if (sscanf(argv[++k], "%dx%d", &shot_w, &shot_h) != 2 || shot_w < 16 || shot_h < 16) {
                fprintf(stderr, "ct_sdl: --shot-size WxH\n");
                return 2;
            }
        } else {
            fprintf(stderr, "usage: ct_sdl [--scale N] [--frames N] [--dump DIR] "
                            "[--needed-hw FILE] [--fast] [--require-render] [--log-input] "
                            "[--interp-only] [--record FILE] [--script FILE] [--log-audio] "
                            "[--require-no-underrun] [--hd2d] [--preset off|subtle|hd2d] "
                            "[--aspect 4:3|pixel|stretch] [--debug-view N] [--fullscreen] "
                            "[--log-gpu] [--shot F:PATH]... [--shot-size WxH] [--module FILE]\n");
            return 2;
        }
    }
    if (scale < 1)
        scale = 1;
    atexit(report_exit);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER)) {
        fprintf(stderr, "ct_sdl: SDL_Init: %s\n", SDL_GetError());
        set_exit_reason("SDL_Init failed");
        return 1;
    }
    static CPU cpu;
    bus_init(NULL);
    if (module_path) {   /* port: game code from a module (runtime/module.h) */
        const char *err;
        if (ct_module_load(module_path, &err)) {
            fprintf(stderr, "ct_sdl: --module: %s\n", err);
            set_exit_reason("game module failed to load");
            return 2;
        }
        fprintf(stderr, "ct_sdl: game module %s: %u functions\n", module_path, ct_func_count);
    }
    char title[22];   /* ROM header title, $FFC0, 21 bytes, space padded */
    memcpy(title, bus_rom() + 0xFFC0, 21);
    title[21] = 0;
    for (int k = 20; k >= 0 && (title[k] == ' ' || !title[k]); k--)
        title[k] = 0;
    /* port: under gamescope (Steam Game Mode, handheld PCs) the
       window is the whole screen anyway: start fullscreen. */
    if (getenv("GAMESCOPE_WAYLAND_DISPLAY") || getenv("SteamDeck"))
        fullscreen = 1;
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
            win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   SCHED_WIDTH * scale * 4 / 3, SCHED_HEIGHT * scale,
                                   wflags | SDL_WINDOW_OPENGL);
            glctx = win ? SDL_GL_CreateContext(win) : NULL;
            if (glctx && hd2d_init(want_gl[k].gles)) {
                SDL_GL_DeleteContext(glctx);
                glctx = NULL;
            }
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
        SDL_GL_SetSwapInterval(0);   /* paced by the frame timer below */
        /* port: say which look is on and how to change it. */
        static char hello[48];
        snprintf(hello, sizeof hello, "Look: %s - F1/R3 to change",
                 hd2d_preset() == HD2D_OFF ? "original" : hd2d_preset() == HD2D_SUBTLE ? "subtle"
                                                                                       : "HD-2D");
        hd2d_osd(hello);
    } else {
        win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               SCHED_WIDTH * scale, SCHED_HEIGHT * scale, wflags);
        ren = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
        if (win && !ren)
            ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                                   SDL_TEXTUREACCESS_STREAMING, SCHED_WIDTH,
                                                   SCHED_HEIGHT) : NULL;
        if (!tex) {
            fprintf(stderr, "ct_sdl: window: %s\n", SDL_GetError());
            set_exit_reason("window or renderer creation failed");
            return 1;
        }
        SDL_RenderSetLogicalSize(ren, SCHED_WIDTH, SCHED_HEIGHT);
        SDL_RenderSetIntegerScale(ren, SDL_TRUE);
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

    interp_reset(&cpu);
    sched_init(&cpu);
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

    key_filter_init(&keys);
    perf_freq = SDL_GetPerformanceFrequency();
    next_frame = SDL_GetPerformanceCounter();
    poll_input(0);
    set_input(1);
    /* Everything per frame happens in on_frame, at the frame edge; this
       loop only keeps the scheduler running. */
    while (running && (frames < 0 || sched_frame_count() < frames))
        sched_run_frame();

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
    if (pad)
        SDL_GameControllerClose(pad);
    if (audio)
        SDL_CloseAudioDevice(audio);
    if (glctx) {
        hd2d_shutdown();
        SDL_GL_DeleteContext(glctx);
    }
    SDL_Quit();
    if (require_no_underrun && audio_underruns)
        return 1;
    return require_render && !rendered ? 1 : 0;
}
