/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The Deck-format hidraw reader (frontend/hid_deck.h): state reports made
 * here from the documented layout (SDL's controller_structs.h,
 * SteamDeckStatePacket_t; no captures from a real device) parsed into SDL
 * buttons and axes; then a fake sysfs class directory and a FIFO standing
 * in for /dev/hidrawN, so the reader finds the device, attaches its SDL
 * virtual game controller, and the frontend's mapping turns the reports
 * into SNES pad bits and back-button actions. Then the device going away
 * and coming back, auto mode giving way to another controller, the
 * glyph set and the no-controller hint. No ROM, no window, Linux only for
 * the FIFO part.
 *
 * usage: test_hid_deck SCRATCH_DIR */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "bindings.h"
#include "device.h"
#include "glyphs.h"
#include "hid_deck.h"
#include "input.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define HAVE_FIFO 1
#endif

static long checks, fails;
#define CHECK(c, ...)                                                    \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            fails++;                                                     \
            fprintf(stderr, "%s:%d: FAIL: ", __FILE__, __LINE__);        \
            fprintf(stderr, __VA_ARGS__);                                \
            fputc('\n', stderr);                                         \
        }                                                                \
    } while (0)

/* The Deck's button bits (ulButtonsL @8, ulButtonsH @12). */
#define L_R2 0x00000001u
#define L_L2 0x00000002u
#define L_R 0x00000004u
#define L_L 0x00000008u
#define L_Y 0x00000010u
#define L_B 0x00000020u
#define L_X 0x00000040u
#define L_A 0x00000080u
#define L_UP 0x00000100u
#define L_RIGHT 0x00000200u
#define L_LEFT 0x00000400u
#define L_DOWN 0x00000800u
#define L_VIEW 0x00001000u
#define L_STEAM 0x00002000u
#define L_MENU 0x00004000u
#define L_L5 0x00008000u
#define L_R5 0x00010000u
#define L_L3 0x00400000u
#define L_R3 0x04000000u
#define H_L4 0x00000200u
#define H_R4 0x00000400u
#define H_QAM 0x00040000u

static void put16(uint8_t *p, int v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int k = 0; k < 4; k++)
        p[k] = (uint8_t)(v >> 8 * k);
}

typedef struct {
    uint32_t l, h;
    int lx, ly, rx, ry;   /* the device's: y up */
    int tl, tr;           /* 0..32767 */
} in;

static uint32_t seq;

static void report(uint8_t r[64], const in *v)
{
    memset(r, 0, 64);
    r[0] = 1;    /* unReportVersion, 16 bits */
    r[2] = 9;    /* ID_CONTROLLER_DECK_STATE */
    r[3] = 64;   /* ucLength */
    put32(r + 4, ++seq);
    put32(r + 8, v->l);
    put32(r + 12, v->h);
    put16(r + 28, -16384);   /* accel z: 1 g, as a device lying flat sends */
    put16(r + 44, v->tl);
    put16(r + 46, v->tr);
    put16(r + 48, v->lx);
    put16(r + 50, v->ly);
    put16(r + 52, v->rx);
    put16(r + 54, v->ry);
}

static void test_parse(void)
{
    static const struct { uint32_t l, h; int button; const char *what; } map[] = {
        {L_A, 0, SDL_CONTROLLER_BUTTON_A, "A"},
        {L_B, 0, SDL_CONTROLLER_BUTTON_B, "B"},
        {L_X, 0, SDL_CONTROLLER_BUTTON_X, "X"},
        {L_Y, 0, SDL_CONTROLLER_BUTTON_Y, "Y"},
        {L_L, 0, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, "L1"},
        {L_R, 0, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "R1"},
        {L_VIEW, 0, SDL_CONTROLLER_BUTTON_BACK, "View"},
        {L_MENU, 0, SDL_CONTROLLER_BUTTON_START, "Menu"},
        {L_STEAM, 0, SDL_CONTROLLER_BUTTON_GUIDE, "Steam"},
        {0, H_QAM, SDL_CONTROLLER_BUTTON_MISC1, "QAM"},
        {L_L3, 0, SDL_CONTROLLER_BUTTON_LEFTSTICK, "L3"},
        {L_R3, 0, SDL_CONTROLLER_BUTTON_RIGHTSTICK, "R3"},
        {0, H_R4, SDL_CONTROLLER_BUTTON_PADDLE1, "R4"},
        {0, H_L4, SDL_CONTROLLER_BUTTON_PADDLE2, "L4"},
        {L_R5, 0, SDL_CONTROLLER_BUTTON_PADDLE3, "R5"},
        {L_L5, 0, SDL_CONTROLLER_BUTTON_PADDLE4, "L5"},
        {L_UP, 0, SDL_CONTROLLER_BUTTON_DPAD_UP, "up"},
        {L_DOWN, 0, SDL_CONTROLLER_BUTTON_DPAD_DOWN, "down"},
        {L_LEFT, 0, SDL_CONTROLLER_BUTTON_DPAD_LEFT, "left"},
        {L_RIGHT, 0, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, "right"},
    };
    uint8_t r[64];
    hid_deck_state s;
    for (size_t k = 0; k < sizeof map / sizeof *map; k++) {
        report(r, &(in){.l = map[k].l, .h = map[k].h});
        CHECK(hid_deck_parse(r, 64, &s), "%s: a state report", map[k].what);
        CHECK(s.buttons == 1u << map[k].button, "%s: buttons %08x, expected button %d",
              map[k].what, s.buttons, map[k].button);
    }
    /* L2 / R2 are triggers (axes), the trackpad clicks nothing. */
    report(r, &(in){.l = L_L2 | L_R2 | 0x00060000u});
    CHECK(hid_deck_parse(r, 64, &s) && s.buttons == 0, "L2/R2/pad bits: no buttons (%08x)",
          s.buttons);
    CHECK(s.axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT] == -32768 &&
              s.axis[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == -32768,
          "triggers released: -32768");
    report(r, &(in){.lx = 1000, .ly = 2000, .rx = -3000, .ry = -32768, .tl = 32767, .tr = 16384});
    CHECK(hid_deck_parse(r, 64, &s), "axes report");
    CHECK(s.axis[SDL_CONTROLLER_AXIS_LEFTX] == 1000 && s.axis[SDL_CONTROLLER_AXIS_LEFTY] == -2000,
          "left stick: x as is, y flipped (%d, %d)", s.axis[0], s.axis[1]);
    CHECK(s.axis[SDL_CONTROLLER_AXIS_RIGHTX] == -3000 &&
              s.axis[SDL_CONTROLLER_AXIS_RIGHTY] == 32767,
          "right stick: y -32768 flips to 32767 (%d, %d)", s.axis[2], s.axis[3]);
    CHECK(s.axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT] == 32766 &&
              s.axis[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == 0,
          "triggers: raw * 2 - 32768 (%d, %d)", s.axis[4], s.axis[5]);

    hid_deck_state keep = s;
    report(r, &(in){.l = L_A});
    r[2] = 1;   /* ID_CONTROLLER_STATE: a Steam Controller's, not the Deck's */
    CHECK(!hid_deck_parse(r, 64, &s) && !memcmp(&s, &keep, sizeof s), "type 1: not a Deck report");
    report(r, &(in){.l = L_A});
    r[0] = 2;
    CHECK(!hid_deck_parse(r, 64, &s), "version 2: not taken");
    report(r, &(in){.l = L_A});
    r[3] = 60;
    CHECK(!hid_deck_parse(r, 64, &s), "length 60: not taken");
    report(r, &(in){.l = L_A});
    CHECK(!hid_deck_parse(r, 63, &s), "63 bytes read: not taken");
    CHECK(!hid_deck_parse(NULL, 64, &s), "NULL: not taken");
}

/* InputPlumber's Deck descriptor (a vendor page 0xFFFF collection of 64
   input and 64 feature bytes), and a plain gamepad's start. */
static const uint8_t vendor_desc[] = {
    0x06, 0xff, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x09, 0x02, 0x09, 0x03, 0x15, 0x00, 0x26,
    0xff, 0x00, 0x75, 0x08, 0x95, 0x40, 0x81, 0x02, 0x09, 0x06, 0x09, 0x07, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x40, 0xb1, 0x02, 0xc0,
};
static const uint8_t gamepad_desc[] = {0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0xc0};
static const uint8_t mouse_desc[] = {0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0xc0};

static void test_matching(void)
{
    CHECK(hid_deck_descriptor_ok(vendor_desc, sizeof vendor_desc), "vendor page: ok");
    CHECK(!hid_deck_descriptor_ok(gamepad_desc, sizeof gamepad_desc), "gamepad page: not ok");
    CHECK(!hid_deck_descriptor_ok(mouse_desc, sizeof mouse_desc), "mouse: not ok");
    CHECK(!hid_deck_descriptor_ok(vendor_desc, 2), "cut short: not ok");
    CHECK(!hid_deck_descriptor_ok(NULL, 0), "empty: not ok");

    CHECK(hid_deck_product_ok(HID_DECK_AUTO, 0x28de, 0x12fd), "auto: InputPlumber's Ally");
    CHECK(hid_deck_product_ok(HID_DECK_AUTO, 0x28de, 0x12fe), "auto: InputPlumber's Legion Go");
    CHECK(!hid_deck_product_ok(HID_DECK_AUTO, 0x28de, 0x1205), "auto: a real Deck is SDL's");
    CHECK(hid_deck_product_ok(HID_DECK_ON, 0x28de, 0x1205), "on: a real Deck too");
    CHECK(!hid_deck_product_ok(HID_DECK_OFF, 0x28de, 0x12fd), "off: nothing");
    CHECK(!hid_deck_product_ok(HID_DECK_ON, 0x045e, 0x028e), "not Valve: no");
    CHECK(!strcmp(hid_deck_mode_value(HID_DECK_AUTO), "auto") &&
              !strcmp(hid_deck_mode_value(HID_DECK_OFF), "off"),
          "settings.ini values");

    CHECK(glyphs_family(SDL_CONTROLLER_TYPE_UNKNOWN, 0x28de, 0x12fd, "ROG Ally Controller", 0) ==
              GLYPHS_STEAM_DECK,
          "28de:12fd: the Deck's glyphs");

    CHECK(device_no_pad_hint(DEVICE_ROG_ALLY, 0) != NULL, "Ally, no pad: the hint");
    CHECK(device_no_pad_hint(DEVICE_STEAM_DECK, 0) != NULL, "Deck, no pad: the hint");
    CHECK(device_no_pad_hint(DEVICE_ROG_ALLY, 1) == NULL, "Ally with a pad: no hint");
    CHECK(device_no_pad_hint(DEVICE_PC, 0) == NULL, "a PC: no hint");
    const char *h = device_no_pad_hint(DEVICE_ROG_ALLY, 0);
    CHECK(h && strstr(h, "HANDHELD.md") && strstr(h, "InputPlumber"), "the hint: %s", h ? h : "");
}

#ifdef HAVE_FIFO
static char root[400];

static void write_file(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL, "write %s: %s", path, strerror(errno));
    if (f) {
        fwrite(data, 1, n, f);
        fclose(f);
    }
}

/* A fake /sys/class/hidraw/<node> and /dev/<node> FIFO; returns the
   FIFO opened read-write (the writer end). */
static int make_device(const char *node, const char *id, const char *name)
{
    char path[600], text[256];
    snprintf(path, sizeof path, "%s/class/%s", root, node);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/class/%s/device", root, node);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/class/%s/device/uevent", root, node);
    snprintf(text, sizeof text,
             "DRIVER=hid-generic\nHID_ID=%s\nHID_NAME=%s\nHID_PHYS=\nMODALIAS=hid:x\n", id, name);
    write_file(path, text, strlen(text));
    snprintf(path, sizeof path, "%s/class/%s/device/report_descriptor", root, node);
    write_file(path, vendor_desc, sizeof vendor_desc);
    snprintf(path, sizeof path, "%s/dev/%s", root, node);
    unlink(path);
    CHECK(mkfifo(path, 0600) == 0, "mkfifo %s: %s", path, strerror(errno));
    int fd = open(path, O_RDWR | O_NONBLOCK);
    CHECK(fd >= 0, "open %s: %s", path, strerror(errno));
    return fd;
}

static void remove_device(const char *node)
{
    char path[600];
    snprintf(path, sizeof path, "%s/class/%s/device/uevent", root, node);
    unlink(path);
    snprintf(path, sizeof path, "%s/class/%s/device/report_descriptor", root, node);
    unlink(path);
    snprintf(path, sizeof path, "%s/class/%s/device", root, node);
    rmdir(path);
    snprintf(path, sizeof path, "%s/class/%s", root, node);
    rmdir(path);
    snprintf(path, sizeof path, "%s/dev/%s", root, node);
    unlink(path);
}

static void send(int fd, const in *v)
{
    uint8_t r[64];
    report(r, v);
    CHECK(write(fd, r, 64) == 64, "write a report: %s", strerror(errno));
}

static int pad_button(void *ctx, SDL_GameControllerButton b)
{
    return SDL_GameControllerGetButton(ctx, b);
}

static int pad_axis(void *ctx, SDL_GameControllerAxis a)
{
    return SDL_GameControllerGetAxis(ctx, a);
}

static SDL_GameController *ours(void)
{
    for (int k = 0; k < SDL_NumJoysticks(); k++)
        if (SDL_JoystickGetDeviceInstanceID(k) == hid_deck_instance())
            return SDL_IsGameController(k) ? SDL_GameControllerOpen(k) : NULL;
    return NULL;
}

/* One frontend frame: the reader, SDL's state, the SNES bits. */
static uint16_t frame(SDL_GameController *c)
{
    hid_deck_update();
    SDL_GameControllerUpdate();
    return c ? input_controller(pad_button, pad_axis, c) : 0;
}

static void test_device(const char *dir)
{
    snprintf(root, sizeof root, "%s", dir);
    char path[600];
    mkdir(root, 0755);
    snprintf(path, sizeof path, "%s/class", root);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/dev", root);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/class", root);
    char dev[600];
    snprintf(dev, sizeof dev, "%s/dev", root);
    hid_deck_set_paths(path, dev);
    hid_deck_set_log(1);

    /* Another Valve device that sends nothing (a Deck's mouse interface,
       here with the vendor descriptor), and a non-Valve one. */
    int quiet = make_device("hidraw3", "0003:000028DE:000012FD", "Quiet");
    int other = make_device("hidraw4", "0003:0000045E:0000028E", "Xbox pad");
    int w = make_device("hidraw7", "0003:000028DE:000012FD", "ROG Ally Controller");
    hid_deck_set_mode(HID_DECK_ON);   /* on: whatever pads this machine has */
    send(w, &(in){.l = L_A});
    hid_deck_update();
    CHECK(hid_deck_instance() >= 0, "the device was found and attached");
    CHECK(strstr(hid_deck_describe(), "hidraw7") && strstr(hid_deck_describe(), "28de:12fd"),
          "described: %s", hid_deck_describe());
    SDL_GameController *c = ours();
    CHECK(c != NULL, "a game controller");
    if (!c)
        return;
    CHECK(!strcmp(SDL_GameControllerName(c), "ROG Ally Controller"), "name: %s",
          SDL_GameControllerName(c));
    CHECK(SDL_GameControllerGetVendor(c) == 0x28de && SDL_GameControllerGetProduct(c) == 0x12fd,
          "vendor:product %04x:%04x", SDL_GameControllerGetVendor(c),
          SDL_GameControllerGetProduct(c));
    CHECK(glyphs_family_of(c) == GLYPHS_STEAM_DECK, "glyphs: %s",
          glyphs_family_name(glyphs_family_of(c)));
    CHECK(SDL_GameControllerHasButton(c, SDL_CONTROLLER_BUTTON_PADDLE1) &&
              SDL_GameControllerHasButton(c, SDL_CONTROLLER_BUTTON_PADDLE4),
          "paddles 1-4");
    uint16_t got = frame(c);
    CHECK(got == PAD_B, "the first report (A): SNES B, $%04X", got);

    /* Report by report: the Deck's buttons -> SNES (input.h's mapping). */
    static const struct { uint32_t l; uint16_t bit; const char *what; } snes[] = {
        {L_A, PAD_B, "A = SNES B"},          {L_B, PAD_A, "B = SNES A"},
        {L_X, PAD_Y, "X = SNES Y"},          {L_Y, PAD_X, "Y = SNES X"},
        {L_L, PAD_L, "L1 = L"},              {L_R, PAD_R, "R1 = R"},
        {L_MENU, PAD_START, "Menu = Start"}, {L_VIEW, PAD_SELECT, "View = Select"},
        {L_UP, PAD_UP, "up"},                {L_DOWN, PAD_DOWN, "down"},
        {L_LEFT, PAD_LEFT, "left"},          {L_RIGHT, PAD_RIGHT, "right"},
        {L_L5 | L_R5, 0, "L5 + R5: no SNES button"},
    };
    for (size_t k = 0; k < sizeof snes / sizeof *snes; k++) {
        send(w, &(in){.l = snes[k].l});
        got = frame(c);
        CHECK(got == snes[k].bit, "%s: $%04X, expected $%04X", snes[k].what, got, snes[k].bit);
    }
    send(w, &(in){.lx = -30000});
    CHECK(frame(c) == PAD_LEFT, "left stick left: SNES left");
    send(w, &(in){.ly = 30000});
    CHECK(frame(c) == PAD_UP, "left stick up (device y up): SNES up");
    send(w, &(in){.tl = 32767});
    frame(c);
    CHECK(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 30000, "L2 fully: %d",
          SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    send(w, &(in){0});
    frame(c);
    CHECK(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 0, "L2 released: %d",
          SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT));

    /* Several reports in one frame: the last one counts. */
    send(w, &(in){.l = L_A});
    send(w, &(in){.l = L_B});
    CHECK(frame(c) == PAD_A, "two reports in a frame: the last (B)");

    /* Back buttons -> the frontend's default actions (bindings.h). */
    bindings b;
    bindings_defaults(&b);
    binder s;
    binder_init(&s);
    send(w, &(in){0});
    frame(c);
    CHECK(binder_update(&s, &b, pad_button, c) == 0, "idle: no actions");
    static const struct { uint32_t l, h; ct_action a; const char *what; } back[] = {
        {0, H_L4, ACTION_QUICKSAVE, "L4 quick save"},
        {0, H_R4, ACTION_QUICKLOAD, "R4 quick load"},
        {L_L5, 0, ACTION_FFWD, "L5 fast-forward"},
        {L_R5, 0, ACTION_MENU, "R5 menu"},
    };
    for (size_t k = 0; k < sizeof back / sizeof *back; k++) {
        send(w, &(in){.l = back[k].l, .h = back[k].h});
        frame(c);
        SDL_GameControllerButton btn = bindings_button_of(&b, back[k].a);
        CHECK(btn != SDL_CONTROLLER_BUTTON_INVALID && SDL_GameControllerGetButton(c, btn),
              "%s: its bound button is down", back[k].what);
        CHECK(binder_update(&s, &b, pad_button, c) == 1, "%s: one action", back[k].what);
        send(w, &(in){0});
        frame(c);
        binder_update(&s, &b, pad_button, c);
    }
    SDL_GameControllerClose(c);

    /* The device goes (InputPlumber recreates it): dropped, then found
       again under a new node. */
    close(w);
    remove_device("hidraw7");
    hid_deck_update();
    CHECK(hid_deck_instance() < 0, "the device went: dropped");
    w = make_device("hidraw8", "0003:000028DE:000012FD", "ROG Ally Controller");
    send(w, &(in){.l = L_Y});
    SDL_Delay(300);   /* the rescan after a drop: 250 ms */
    hid_deck_update();
    CHECK(hid_deck_instance() >= 0 && strstr(hid_deck_describe(), "hidraw8"),
          "found again: %s", hid_deck_describe());
    c = ours();
    CHECK(c && frame(c) == PAD_X, "the new one's report: Y = SNES X");
    if (c)
        SDL_GameControllerClose(c);

    /* Auto mode: another game controller appears (Steam's virtual pad):
       the reader gives way within a second, and stays away. */
    hid_deck_set_mode(HID_DECK_AUTO);
    SDL_VirtualJoystickDesc d;
    memset(&d, 0, sizeof d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    d.vendor_id = 0x28de;
    d.product_id = 0x11ff;
    d.name = "Steam Virtual Gamepad (test)";
    int vidx = SDL_JoystickAttachVirtualEx(&d);
    CHECK(vidx >= 0, "attach the other pad: %s", SDL_GetError());
    SDL_JoystickID vid = SDL_JoystickGetDeviceInstanceID(vidx);
    SDL_Delay(1100);
    send(w, &(in){0});
    hid_deck_update();
    CHECK(hid_deck_instance() < 0, "auto: dropped for the other pad");
    SDL_Delay(2100);
    hid_deck_update();
    CHECK(hid_deck_instance() < 0, "auto: not found again while the other pad is there");
    for (int k = 0; k < SDL_NumJoysticks(); k++)
        if (SDL_JoystickGetDeviceInstanceID(k) == vid)
            SDL_JoystickDetachVirtual(k);

    /* Off: never. */
    hid_deck_set_mode(HID_DECK_OFF);
    SDL_Delay(2100);
    hid_deck_update();
    CHECK(hid_deck_instance() < 0, "off: not used");
    hid_deck_close();

    close(w);
    close(quiet);
    close(other);
    remove_device("hidraw3");
    remove_device("hidraw4");
    remove_device("hidraw8");
    snprintf(path, sizeof path, "%s/class", root);
    rmdir(path);
    rmdir(dev);
    rmdir(root);
}
#endif

int main(int argc, char **argv)
{
    test_parse();
    test_matching();
#ifdef HAVE_FIFO
    if (SDL_Init(SDL_INIT_GAMECONTROLLER)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    test_device(argc > 1 ? argv[1] : "test_hid_deck_tmp");
    SDL_Quit();
#else
    (void)argc;
    (void)argv;
#endif
    fprintf(stderr, "test_hid_deck: %ld checks, %ld failed\n", checks, fails);
    return fails ? 1 : 0;
}
