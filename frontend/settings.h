/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the settings registry: every option the in-game menu (menu.h)
 * shows, with how to read and change it and, for most, its settings.ini
 * key. Whoever owns an option registers it once at start-up (main.c for
 * the frontend's, qol.c for the quality-of-life ones); the menu draws the
 * registered items tab by tab, and settings.ini is read and written
 * through the same table, so a new option needs no menu code.
 *
 * Values are ints: a bool 0 / 1, an enum's index, an int itself, a key
 * an SDL_Scancode. A change through settings_set_value calls the setter
 * (which applies it live where it can) and then the saver
 * (settings_on_change): settings.ini is written at once.
 *
 * settings.ini lines "key=value" for registered keys are parsed by
 * settings_parse_line (bool: on/off, 1/0, true/false, yes/no, or the
 * item's own value names; enum: a value name, or the index; int: the
 * number, or a decimal when ini_div is set: gyro_strength=1.25 is 125
 * with ini_div 100). settings_write_lines writes them back. */
#ifndef CT_SETTINGS_H
#define CT_SETTINGS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETTING_BOOL,     /* on / off */
    SETTING_ENUM,     /* one of count options */
    SETTING_INT,      /* min..max in steps of step */
    SETTING_ACTION,   /* a button: run() */
    SETTING_KEY,      /* a keyboard key (SDL_Scancode); the menu asks for a key press */
    SETTING_INFO,     /* a line of text (text()), nothing to change */
} setting_type;

enum {
    SETTING_RESTART = 1,   /* takes effect on the next start: the menu says so */
    SETTING_CONFIRM = 2,   /* action: a second press runs it ("press again") */
    SETTING_NO_SAVE = 4,   /* a change doesn't write settings.ini (session only) */
    SETTING_HEADING = 8,   /* info: a section heading */
    SETTING_CLOSE = 16,    /* action: the menu closes after it runs (a state load) */
};

typedef struct setting {
    const char *key;        /* settings.ini key; NULL: not read or written here */
    const char *label;      /* the row's name in the menu */
    const char *category;   /* the menu tab ("Look", "Display", ...: settings_category_add) */
    const char *help;       /* a line under the list while the row is selected; may be NULL */
    setting_type type;
    /* SETTING_ENUM: count labels in options (or option() for labels made
       on the fly); values: settings.ini names (NULL: the index). For
       SETTING_BOOL, values may give the two names (default "off", "on"). */
    const char *const *options;
    const char *const *values;
    int count;
    /* SETTING_INT: the range and step; format: how the value reads
       ("%d%%"; NULL: "%d"); ini_div: settings.ini holds value / ini_div
       as a decimal (0: the int). */
    int min, max, step;
    const char *format;
    int ini_div;
    int (*get)(void *user);
    void (*set)(void *user, int value);
    void (*run)(void *user);                     /* SETTING_ACTION */
    int (*enabled)(void *user);                  /* NULL: always; 0 greys the row out */
    /* SETTING_INFO / SETTING_ACTION: text on the right of the row
       (NULL: none); SETTING_ENUM: option() labels value instead of
       options[] when set. Write into buf, return it (or a constant). */
    const char *(*text)(void *user, char *buf, size_t n);
    const char *(*option)(void *user, int value, char *buf, size_t n);
    /* A picture beside the list while the row is selected (save state
       thumbnails): B G R x pixels, *w x *h, or NULL for none. *gen
       changes whenever the pixels do. */
    const uint8_t *(*preview)(void *user, int *w, int *h, unsigned *gen);
    void *user;
    int flags;   /* SETTING_* above */
} setting;

#define SETTINGS_MAX 160

/* Add an item (copied; the strings it points to must outlive it).
   Returns its index, or -1 when the table is full. A key registered
   twice replaces the first item. */
int settings_register(const setting *s);
/* Drop every item of a category (lists that change: the asset packs). */
void settings_unregister_category(const char *category);

int settings_count(void);
const setting *settings_at(int k);
const setting *settings_find(const char *key);

/* The menu's tabs, in order: categories added here first, then any
   other in the order its first item was registered. */
void settings_category_add(const char *category);
int settings_categories(const char **out, int max);

/* Read, change (clamped to the item's range, then saved), step (left /
   right: an enum or bool wraps, an int moves by step). */
int settings_get_value(const setting *s);
void settings_set_value(const setting *s, int value);
void settings_step(const setting *s, int dir);
int settings_is_enabled(const setting *s);
/* The value as the menu shows it ("On", "HD-2D", "80%", "F6"). */
const char *settings_value_text(const setting *s, char *buf, size_t n);

/* settings.ini: one line; 1 if it was a registered key (applied when
   valid, else a warning on stderr), 0 if not. */
int settings_parse_line(const char *line);
/* Every registered key's line. */
void settings_write_lines(FILE *f);

/* Called after each change (settings.ini is written there), and when the
   menu wants the dynamic lists rebuilt (on opening). */
void settings_on_change(void (*save)(void));
void settings_changed(void);
void settings_on_refresh(void (*refresh)(void));
void settings_refresh(void);

/* Key names for SETTING_KEY (SDL's), for the menu: set by the frontend
   so this file needs no SDL. */
void settings_key_namer(const char *(*name)(int scancode));

#ifdef __cplusplus
}
#endif

#endif
