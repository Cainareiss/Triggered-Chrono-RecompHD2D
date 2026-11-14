/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: our own reader for Steam Deck-format controllers on Linux
 * hidraw, for when SDL finds none (docs/HANDHELD.md, "Bazzite /
 * InputPlumber").
 *
 * InputPlumber (Bazzite's input service on the ROG Ally, Legion Go, Claw,
 * Zone, ...) hides the handheld's real pad and offers an emulated Steam
 * Deck controller through uhid instead: Valve's vendor ID 28de with its
 * own product IDs (12fd for the Ally, 12f0 and 12fa-12ff for the others),
 * sending the Deck's 64-byte state reports (ID_CONTROLLER_DECK_STATE).
 * Steam reads it; SDL doesn't (its Deck driver wants product 1205, and
 * SDL 2.32's hidapi drops uhid devices), so a game started outside Steam
 * sees no controller.
 *
 * This reader finds such a device in /sys/class/hidraw (a Valve vendor ID,
 * a vendor-defined report descriptor, and Deck state reports when read),
 * opens it read-only and feeds an SDL virtual game controller with the
 * device's name, vendor and product, so the rest of the frontend (pads,
 * bindings, glyphs, back buttons, co-op, menu) uses it like any other.
 * It writes to the device only with rumble on (hid_deck_set_rumble), and
 * only the Deck's simple rumble command: then the node is opened
 * read-write, and the virtual controller's rumble (SDL_GameControllerRumble)
 * becomes that command, sent as a feature report from a thread of its own
 * (InputPlumber passes it on to the handheld's motors: docs/HANDHELD.md).
 *
 * Modes (settings.ini hid_deck=auto|on|off): auto uses it only while SDL
 * has no other game controller (Steam's virtual pad, a USB pad), and
 * drops it when one appears; on uses it alongside the others; off never.
 * A device that goes away (InputPlumber recreates it when Steam starts or
 * stops, and around suspend) is dropped and looked for again every 2 s.
 *
 * The report parsing follows SDL's Steam Deck driver
 * (src/joystick/hidapi/SDL_hidapi_steamdeck.c, Max Maisel, and
 * steam/controller_structs.h, Valve; zlib license: see hid_deck.c). */
#ifndef CT_HID_DECK_H
#define CT_HID_DECK_H

#include <stdint.h>

enum { HID_DECK_AUTO, HID_DECK_ON, HID_DECK_OFF, HID_DECK_MODES };

/* One state report, as SDL game controller values: bit b of buttons is
   SDL_CONTROLLER_BUTTON b (paddle1 = R4, paddle2 = L4, paddle3 = R5,
   paddle4 = L5, misc1 = the ... button), axis[a] is SDL_CONTROLLER_AXIS
   a as SDL's Deck driver gives it to the joystick (sticks -32768..32767,
   y down; triggers -32768 released .. 32766 fully pressed). */
typedef struct {
    uint32_t buttons;
    int16_t axis[6];
} hid_deck_state;

/* A 64-byte Deck state report (version 1, type 9, length 64) -> *s.
   1 if it was one, 0 otherwise (*s untouched). */
int hid_deck_parse(const uint8_t *report, int len, hid_deck_state *s);

/* The report descriptor declares a vendor-defined usage page (0xFF00 and
   up), as the Deck's controller interface does. */
int hid_deck_descriptor_ok(const uint8_t *desc, int len);

/* Whether this reader takes a device with these IDs in the given mode:
   Valve's vendor ID; in auto mode not the real Deck (28de:1205), which
   SDL's own driver reads. */
int hid_deck_product_ok(int mode, uint16_t vendor, uint16_t product);

/* settings.ini values / menu labels. */
const char *hid_deck_mode_value(int mode);   /* "auto", "on", "off" */

void hid_deck_set_mode(int mode);
int hid_deck_mode(void);
/* Print each button change of the device read (--log-input). */
void hid_deck_set_log(int on);

/* Each frame, before SDL's events are read (SDL_INIT_JOYSTICK needed):
   reads what the device sent and sets the virtual controller; looks for
   a device when there is none (now, then every 2 s). */
void hid_deck_update(void);
/* The virtual controller's SDL instance ID, or -1 when none is attached. */
int hid_deck_instance(void);
/* The device in use ("ROG Ally Controller (/dev/hidraw10)"), or "". */
const char *hid_deck_describe(void);
/* Close the device and detach the virtual controller. */
void hid_deck_close(void);

/* Rumble (port): on, the device is opened read-write and the virtual
   controller's rumble is sent to it; off (the default), read-only and no
   rumble. Applies to the device open now too. */
void hid_deck_set_rumble(int on);
/* For tests: how many rumble commands the thread has sent (or tried). */
long hid_deck_rumble_writes(void);
/* The Deck's simple rumble command (ID_TRIGGER_RUMBLE_CMD, $EB) as the
   feature report SDL's Deck driver sends: report number 0 first, then the
   64-byte message. low drives the left (heavy) motor, high the right. */
#define HID_DECK_RUMBLE_LEN 65
void hid_deck_rumble_report(uint8_t out[HID_DECK_RUMBLE_LEN], uint16_t low, uint16_t high);

/* For tests: the sysfs class directory and the device directory read
   instead of /sys/class/hidraw and /dev (NULL: those). */
void hid_deck_set_paths(const char *sysfs_dir, const char *dev_dir);

#endif
