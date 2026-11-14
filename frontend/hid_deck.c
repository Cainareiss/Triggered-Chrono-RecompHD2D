/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-FileCopyrightText: 2023 Max Maisel <max.maisel@posteo.de>
 * SPDX-FileCopyrightText: 2020 Valve Corporation
 * SPDX-License-Identifier: GPL-3.0-or-later AND Zlib
 *
 * port: a read-only Steam Deck-format hidraw reader feeding an SDL
 * virtual game controller (hid_deck.h).
 *
 * The report layout (hid_deck_parse's offsets, below) and the button bits
 * and their SDL buttons and axes are ported from Simple DirectMedia
 * Layer's src/joystick/hidapi/SDL_hidapi_steamdeck.c (Copyright (C) 2023
 * Max Maisel) and src/joystick/hidapi/steam/controller_structs.h
 * (Copyright (C) 2020 Valve Corporation), release 2.32.10, under this
 * license:
 *
 *   This software is provided 'as-is', without any express or implied
 *   warranty.  In no event will the authors be held liable for any damages
 *   arising from the use of this software.
 *
 *   Permission is granted to anyone to use this software for any purpose,
 *   including commercial applications, and to alter it and redistribute it
 *   freely, subject to the following restrictions:
 *
 *   1. The origin of this software must not be misrepresented; you must not
 *      claim that you wrote the original software. If you use this software
 *      in a product, an acknowledgment in the product documentation would be
 *      appreciated but is not required.
 *   2. Altered source versions must be plainly marked as such, and must not be
 *      misrepresented as being the original software.
 *   3. This notice may not be removed or altered from any source distribution.
 *
 * Altered: rewritten as a byte-offset parser over a plain buffer, without
 * SDL's hidapi layer, the lizard-mode feature reports, rumble or the
 * motion sensors. The scanning, the virtual controller and everything
 * else here are ours (GPL-3.0-or-later). */
#include "hid_deck.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

/* ---- the report (from SDL's driver and controller_structs.h) ---- */

/* SteamDeckButtons, SDL_hidapi_steamdeck.c: ulButtonsL (bytes 8-11) and
   ulButtonsH (bytes 12-15) of SteamDeckStatePacket_t. */
enum {
    L_R2 = 0x00000001, L_L2 = 0x00000002, L_R = 0x00000004, L_L = 0x00000008,
    L_Y = 0x00000010, L_B = 0x00000020, L_X = 0x00000040, L_A = 0x00000080,
    L_DPAD_UP = 0x00000100, L_DPAD_RIGHT = 0x00000200, L_DPAD_LEFT = 0x00000400,
    L_DPAD_DOWN = 0x00000800, L_VIEW = 0x00001000, L_STEAM = 0x00002000,
    L_MENU = 0x00004000, L_L5 = 0x00008000, L_R5 = 0x00010000,
    L_L3 = 0x00400000, L_R3 = 0x04000000,
    H_L4 = 0x00000200, H_R4 = 0x00000400, H_QAM = 0x00040000,
};

/* ValveInReport_t: header {uint16 unReportVersion = 1, uint8 ucType =
   ID_CONTROLLER_DECK_STATE (9), uint8 ucLength = 64}, then the deck
   state: uint32 unPacketNum @4, uint32 ulButtonsL @8, ulButtonsH @12,
   int16 left / right pad x, y @16-22, accel x y z @24-28, gyro x y z
   @30-34, quaternion w x y z @36-42, uint16 sTriggerRawL @44, R @46,
   int16 sLeftStickX @48, Y @50, sRightStickX @52, Y @54, uint16 pad
   pressures @56, 58. Little-endian, packed. */
#define DECK_VERSION 1
#define DECK_STATE 9
#define DECK_LEN 64

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int le16s(const uint8_t *p)
{
    return (int16_t)(uint16_t)(p[0] | p[1] << 8);
}

static int16_t clamp16(int v)
{
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

int hid_deck_parse(const uint8_t *r, int len, hid_deck_state *s)
{
    if (!r || len < DECK_LEN || (r[0] | r[1] << 8) != DECK_VERSION || r[2] != DECK_STATE ||
        r[3] != DECK_LEN)
        return 0;
    uint32_t l = le32(r + 8), h = le32(r + 12);
    static const struct { uint8_t high; uint32_t bit; uint8_t button; } map[] = {
        {0, L_A, SDL_CONTROLLER_BUTTON_A},
        {0, L_B, SDL_CONTROLLER_BUTTON_B},
        {0, L_X, SDL_CONTROLLER_BUTTON_X},
        {0, L_Y, SDL_CONTROLLER_BUTTON_Y},
        {0, L_L, SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
        {0, L_R, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
        {0, L_VIEW, SDL_CONTROLLER_BUTTON_BACK},
        {0, L_MENU, SDL_CONTROLLER_BUTTON_START},
        {0, L_STEAM, SDL_CONTROLLER_BUTTON_GUIDE},
        {1, H_QAM, SDL_CONTROLLER_BUTTON_MISC1},
        {0, L_L3, SDL_CONTROLLER_BUTTON_LEFTSTICK},
        {0, L_R3, SDL_CONTROLLER_BUTTON_RIGHTSTICK},
        {1, H_R4, SDL_CONTROLLER_BUTTON_PADDLE1},
        {1, H_L4, SDL_CONTROLLER_BUTTON_PADDLE2},
        {0, L_R5, SDL_CONTROLLER_BUTTON_PADDLE3},
        {0, L_L5, SDL_CONTROLLER_BUTTON_PADDLE4},
        {0, L_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_UP},
        {0, L_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN},
        {0, L_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT},
        {0, L_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
    };
    uint32_t b = 0;
    for (size_t k = 0; k < sizeof map / sizeof *map; k++)
        if ((map[k].high ? h : l) & map[k].bit)
            b |= 1u << map[k].button;
    s->buttons = b;
    /* As SDL's driver sends them: triggers raw * 2 - 32768, y up -> down. */
    s->axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = clamp16((r[44] | r[45] << 8) * 2 - 32768);
    s->axis[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = clamp16((r[46] | r[47] << 8) * 2 - 32768);
    s->axis[SDL_CONTROLLER_AXIS_LEFTX] = clamp16(le16s(r + 48));
    s->axis[SDL_CONTROLLER_AXIS_LEFTY] = clamp16(-le16s(r + 50));
    s->axis[SDL_CONTROLLER_AXIS_RIGHTX] = clamp16(le16s(r + 52));
    s->axis[SDL_CONTROLLER_AXIS_RIGHTY] = clamp16(-le16s(r + 54));
    return 1;
}

/* ---- the rest is ours ---- */

int hid_deck_descriptor_ok(const uint8_t *d, int n)
{
    /* The first Usage Page item (global, tag 0x04) is vendor-defined. */
    for (int i = 0; i < n;) {
        uint8_t p = d[i];
        if (p == 0xFE) {   /* long item: size in the next byte */
            if (i + 1 >= n)
                return 0;
            i += 3 + d[i + 1];
            continue;
        }
        int size = (p & 3) == 3 ? 4 : (p & 3);
        if (i + 1 + size > n)
            return 0;
        if ((p & 0xFC) == 0x04) {
            unsigned v = 0;
            for (int k = 0; k < size; k++)
                v |= (unsigned)d[i + 1 + k] << (8 * k);
            return (v & 0xFFFF) >= 0xFF00;
        }
        i += 1 + size;
    }
    return 0;
}

int hid_deck_product_ok(int mode, uint16_t vendor, uint16_t product)
{
    if (vendor != 0x28de || mode == HID_DECK_OFF)
        return 0;
    return mode == HID_DECK_ON || product != 0x1205;
}

const char *hid_deck_mode_value(int mode)
{
    static const char *const v[] = {"auto", "on", "off"};
    return mode >= 0 && mode < HID_DECK_MODES ? v[mode] : "?";
}

static int mode = HID_DECK_AUTO, log_on;

void hid_deck_set_mode(int m)
{
    if (m >= 0 && m < HID_DECK_MODES)
        mode = m;
}

int hid_deck_mode(void)
{
    return mode;
}

void hid_deck_set_log(int on)
{
    log_on = on;
}

#if defined(__linux__) && !defined(__ANDROID__)

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#define RESCAN_MS 2000
#define CHECK_MS 1000
#define REJECT_MS 30000
#define FIRST_REPORT_MS 100

static const char *sysfs_dir = "/sys/class/hidraw", *dev_dir = "/dev";

void hid_deck_set_paths(const char *s, const char *d)
{
    sysfs_dir = s ? s : "/sys/class/hidraw";
    dev_dir = d ? d : "/dev";
}

static struct {
    int fd;
    SDL_Joystick *joy;
    SDL_JoystickID id;
    char node[32];      /* "hidraw10" */
    char name[128];     /* HID_NAME */
    char desc[192];
    uint16_t vid, pid;
    hid_deck_state last;
    int have_last;
    uint32_t last_report, last_check;
} dev = {.fd = -1, .id = -1};

static uint32_t next_scan;
static struct { char node[32]; uint32_t until; } rejected[8];
static char warned[8][32];

static void say(const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(1);
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("ct_sdl: hid_deck: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

int hid_deck_instance(void)
{
    return dev.joy ? (int)dev.id : -1;
}

const char *hid_deck_describe(void)
{
    return dev.joy ? dev.desc : "";
}

static void drop(const char *why)
{
    if (dev.joy) {
        say("dropping %s: %s", dev.desc, why);
        SDL_JoystickClose(dev.joy);
        for (int k = 0; k < SDL_NumJoysticks(); k++)
            if (SDL_JoystickGetDeviceInstanceID(k) == dev.id) {
                SDL_JoystickDetachVirtual(k);
                break;
            }
    }
    if (dev.fd >= 0)
        close(dev.fd);
    dev.fd = -1;
    dev.joy = NULL;
    dev.id = -1;
    dev.have_last = 0;
}

void hid_deck_close(void)
{
    drop("closing");
}

/* SDL has a game controller that isn't ours (Steam's virtual pad, a USB
   pad): auto mode leaves the built-in controls to it. Its name, or NULL. */
static const char *other_controller(void)
{
    for (int k = 0; k < SDL_NumJoysticks(); k++)
        if (SDL_IsGameController(k) && SDL_JoystickGetDeviceInstanceID(k) != dev.id) {
            const char *n = SDL_JoystickNameForIndex(k);
            return n ? n : "?";
        }
    return NULL;
}

static int read_file(const char *path, char *buf, int n)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    int got = 0, r;
    while (got < n && (r = (int)read(fd, buf + got, (size_t)(n - got))) > 0)
        got += r;
    close(fd);
    return got;
}

/* uevent: HID_ID=0003:000028DE:000012FD, HID_NAME=... */
static int parse_uevent(char *text, uint16_t *vid, uint16_t *pid, char *name, size_t n)
{
    int ok = 0;
    name[0] = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        unsigned bus, v, p;
        if (sscanf(line, "HID_ID=%x:%x:%x", &bus, &v, &p) == 3) {
            *vid = (uint16_t)v;
            *pid = (uint16_t)p;
            ok = 1;
        } else if (!strncmp(line, "HID_NAME=", 9)) {
            snprintf(name, n, "%s", line + 9);
        }
    }
    return ok;
}

static int recently_rejected(const char *node, uint32_t now)
{
    for (size_t k = 0; k < sizeof rejected / sizeof *rejected; k++)
        if (!strcmp(rejected[k].node, node) && (int32_t)(rejected[k].until - now) > 0)
            return 1;
    return 0;
}

static void reject(const char *node, uint32_t now)
{
    size_t slot = 0;
    for (size_t k = 0; k < sizeof rejected / sizeof *rejected; k++)
        if (!strcmp(rejected[k].node, node) || (int32_t)(rejected[k].until - now) <= 0) {
            slot = k;
            break;
        }
    snprintf(rejected[slot].node, sizeof rejected[slot].node, "%s", node);
    rejected[slot].until = now + REJECT_MS;
}

static void warn_once(const char *node, const char *fmt, const char *a, const char *b)
{
    for (size_t k = 0; k < sizeof warned / sizeof *warned; k++) {
        if (!strcmp(warned[k], node))
            return;
        if (!warned[k][0]) {
            snprintf(warned[k], sizeof warned[k], "%s", node);
            break;
        }
    }
    fputs("ct_sdl: hid_deck: ", stderr);
    fprintf(stderr, fmt, a, b);
    fputc('\n', stderr);
}

/* Wait (up to FIRST_REPORT_MS) for a Deck state report on fd. */
static int first_report(int fd, hid_deck_state *s)
{
    uint32_t start = SDL_GetTicks();
    for (;;) {
        int left = FIRST_REPORT_MS - (int)(SDL_GetTicks() - start);
        if (left <= 0)
            return 0;
        struct pollfd p = {fd, POLLIN, 0};
        int r = poll(&p, 1, left);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0 || ((p.revents & (POLLERR | POLLHUP | POLLNVAL)) && !(p.revents & POLLIN)))
            return 0;
        uint8_t buf[DECK_LEN];
        ssize_t n = read(fd, buf, sizeof buf);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR))
            return 0;
        if (n > 0 && hid_deck_parse(buf, (int)n, s))
            return 1;
    }
}

static int attach(int fd, const char *node, const char *name, uint16_t vid, uint16_t pid,
                  const hid_deck_state *s)
{
    snprintf(dev.name, sizeof dev.name, "%s", name[0] ? name : "Steam Deck-format controller");
    SDL_VirtualJoystickDesc d;
    memset(&d, 0, sizeof d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;   /* paddle1-4 and misc1 included */
    d.vendor_id = vid;
    d.product_id = pid;
    d.name = dev.name;
    int idx = SDL_JoystickAttachVirtualEx(&d);
    SDL_Joystick *j = idx >= 0 ? SDL_JoystickOpen(idx) : NULL;
    if (!j) {
        say("no virtual controller for %s: %s", node, SDL_GetError());
        if (idx >= 0)
            SDL_JoystickDetachVirtual(idx);
        close(fd);
        return 0;
    }
    dev.fd = fd;
    dev.joy = j;
    dev.id = SDL_JoystickInstanceID(j);
    dev.vid = vid;
    dev.pid = pid;
    snprintf(dev.node, sizeof dev.node, "%s", node);
    snprintf(dev.desc, sizeof dev.desc, "\"%s\" (%s/%s, %04x:%04x)", dev.name, dev_dir, node,
             vid, pid);
    dev.last = *s;
    dev.have_last = 0;
    dev.last_report = dev.last_check = SDL_GetTicks();
    say("using %s: Steam Deck reports, read-only, as a virtual game controller%s", dev.desc,
        SDL_IsGameController(idx) ? "" : " (but SDL doesn't take it as a game controller)");
    return 1;
}

static int scan(uint32_t now)
{
    DIR *dir = opendir(sysfs_dir);
    if (!dir)
        return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(dir))) {
        if (strncmp(e->d_name, "hidraw", 6) || strlen(e->d_name) >= sizeof dev.node)
            continue;
        char path[512], text[1024], name[128];
        uint16_t vid = 0, pid = 0;
        snprintf(path, sizeof path, "%s/%s/device/uevent", sysfs_dir, e->d_name);
        int n = read_file(path, text, (int)sizeof text - 1);
        if (n <= 0)
            continue;
        text[n] = 0;
        if (!parse_uevent(text, &vid, &pid, name, sizeof name) ||
            !hid_deck_product_ok(mode, vid, pid) || recently_rejected(e->d_name, now))
            continue;
        uint8_t rd[4096];
        snprintf(path, sizeof path, "%s/%s/device/report_descriptor", sysfs_dir, e->d_name);
        n = read_file(path, (char *)rd, (int)sizeof rd);
        if (n <= 0 || !hid_deck_descriptor_ok(rd, n)) {
            reject(e->d_name, now);
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", dev_dir, e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            warn_once(e->d_name, "can't read %s (%s): see docs/HANDHELD.md, Bazzite / InputPlumber",
                      path, strerror(errno));
            reject(e->d_name, now);
            continue;
        }
        hid_deck_state s;
        if (!first_report(fd, &s)) {
            close(fd);
            reject(e->d_name, now);
            continue;
        }
        found = attach(fd, e->d_name, name, vid, pid, &s);
    }
    closedir(dir);
    return found;
}

static const char *const button_names[SDL_CONTROLLER_BUTTON_MAX] = {
    "A", "B", "X", "Y", "View (back)", "Steam (guide)", "Menu (start)", "L3", "R3", "L1", "R1",
    "d-pad up", "d-pad down", "d-pad left", "d-pad right", "... (misc1)", "R4 (paddle1)",
    "L4 (paddle2)", "R5 (paddle3)", "L5 (paddle4)", "touchpad",
};

/* --log-input: what changed, in the device's own terms. */
static void log_change(const hid_deck_state *o, const hid_deck_state *s)
{
    uint32_t diff = o->buttons ^ s->buttons;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
        if (diff >> b & 1)
            say("%s %s", button_names[b], s->buttons >> b & 1 ? "down" : "up");
    static const char *const trig[2] = {"L2", "R2"};
    for (int t = 0; t < 2; t++) {
        int a = SDL_CONTROLLER_AXIS_TRIGGERLEFT + t;
        if ((o->axis[a] > 0) != (s->axis[a] > 0))
            say("%s %s", trig[t], s->axis[a] > 0 ? "down (past half)" : "up");
    }
    static const char *const stick[2] = {"left stick", "right stick"};
    for (int k = 0; k < 2; k++) {
        int x0 = o->axis[2 * k], y0 = o->axis[2 * k + 1];
        int x1 = s->axis[2 * k], y1 = s->axis[2 * k + 1];
        int d0 = (x0 < -16000) | (x0 > 16000) << 1 | (y0 < -16000) << 2 | (y0 > 16000) << 3;
        int d1 = (x1 < -16000) | (x1 > 16000) << 1 | (y1 < -16000) << 2 | (y1 > 16000) << 3;
        if (d0 != d1)
            say("%s %s%s%s%s%s%s", stick[k], d1 ? "" : "centered", d1 & 4 ? "up" : "",
                d1 & 8 ? "down" : "", (d1 & 12) && (d1 & 3) ? "-" : "", d1 & 1 ? "left" : "",
                d1 & 2 ? "right" : "");
    }
}

static void apply(const hid_deck_state *s)
{
    static const hid_deck_state idle = {0, {0, 0, 0, 0, -32768, -32768}};
    if (log_on)
        log_change(dev.have_last ? &dev.last : &idle, s);
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
        SDL_JoystickSetVirtualButton(dev.joy, b, (Uint8)(s->buttons >> b & 1));
    for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++)
        SDL_JoystickSetVirtualAxis(dev.joy, a, s->axis[a]);
    dev.last = *s;
    dev.have_last = 1;
}

void hid_deck_update(void)
{
    if (!SDL_WasInit(SDL_INIT_JOYSTICK))
        return;
    uint32_t now = SDL_GetTicks();
    if (mode == HID_DECK_OFF) {
        if (dev.joy)
            drop("hid_deck=off");
        return;
    }
    if (dev.joy) {
        if (!dev.have_last)
            apply(&dev.last);   /* the report the scan read */
        hid_deck_state s;
        int got = 0;
        for (;;) {
            uint8_t buf[DECK_LEN];
            ssize_t n = read(dev.fd, buf, sizeof buf);
            if (n > 0) {
                if (hid_deck_parse(buf, (int)n, &s))
                    got = 1;
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EINTR))
                break;
            /* gone: ENODEV / EIO from hidraw, end of file */
            char why[96];
            snprintf(why, sizeof why, "%s", n == 0 ? "end of file" : strerror(errno));
            drop(why);
            next_scan = now + 250;   /* InputPlumber makes a new one at once */
            return;
        }
        if (got) {
            apply(&s);
            dev.last_report = now;
        }
        if ((int32_t)(now - dev.last_check) >= CHECK_MS) {
            dev.last_check = now;
            const char *o;
            char path[512];
            snprintf(path, sizeof path, "%s/%s", sysfs_dir, dev.node);
            if (mode == HID_DECK_AUTO && (o = other_controller())) {
                char why[192];
                snprintf(why, sizeof why, "SDL has \"%s\" (hid_deck=auto)", o);
                drop(why);
                next_scan = now + RESCAN_MS;
            } else if ((int32_t)(now - dev.last_report) > 3000 && access(path, F_OK)) {
                drop("its sysfs node is gone");
                next_scan = now + 250;
            }
        }
        return;
    }
    if ((int32_t)(now - next_scan) < 0)
        return;
    next_scan = now + RESCAN_MS;
    if (mode == HID_DECK_AUTO && other_controller())
        return;
    scan(now);
}

#else   /* not desktop Linux: no hidraw */

void hid_deck_set_paths(const char *s, const char *d)
{
    (void)s;
    (void)d;
}

void hid_deck_update(void)
{
}

int hid_deck_instance(void)
{
    return -1;
}

const char *hid_deck_describe(void)
{
    return "";
}

void hid_deck_close(void)
{
}

#endif
