/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the settings registry (frontend/settings.h): registering items,
 * settings.ini lines in and out, stepping values, the tabs, the saver. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"

static int fails;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "test_settings: %s:%d: %s\n", __FILE__, __LINE__, #c); \
            fails++;                                                     \
        }                                                                \
    } while (0)

static int b_val, e_val = 1, i_val = 100, saves, runs;
static int get_b(void *u) { (void)u; return b_val; }
static void set_b(void *u, int v) { (void)u; b_val = v; }
static int get_e(void *u) { (void)u; return e_val; }
static void set_e(void *u, int v) { (void)u; e_val = v; }
static int get_i(void *u) { (void)u; return i_val; }
static void set_i(void *u, int v) { (void)u; i_val = v; }
static void run(void *u) { (void)u; runs++; }
static void save(void) { saves++; }
static int off(void *u) { (void)u; return 0; }

int main(void)
{
    static const char *const labels[] = {"Auto", "On", "Off"}, *const values[] = {"auto", "on", "off"};
    static const char *const fs[] = {"0", "1"};
    settings_category_add("A");
    settings_category_add("B");
    CHECK(settings_register(&(setting){.key = "flag", .label = "Flag", .category = "B",
                                       .type = SETTING_BOOL, .values = fs, .get = get_b,
                                       .set = set_b}) == 0);
    settings_register(&(setting){.key = "mode", .label = "Mode", .category = "A",
                                 .type = SETTING_ENUM, .options = labels, .values = values,
                                 .count = 3, .get = get_e, .set = set_e});
    settings_register(&(setting){.key = "gain", .label = "Gain", .category = "A",
                                 .type = SETTING_INT, .min = 25, .max = 300, .step = 25,
                                 .format = "%d%%", .ini_div = 100, .get = get_i, .set = set_i});
    settings_register(&(setting){.label = "Go", .category = "C", .type = SETTING_ACTION, .run = run,
                                 .enabled = off});
    CHECK(settings_count() == 4);
    const char *tabs[8];
    int nt = settings_categories(tabs, 8);
    CHECK(nt == 3 && !strcmp(tabs[0], "A") && !strcmp(tabs[1], "B") && !strcmp(tabs[2], "C"));

    /* settings.ini in */
    CHECK(settings_parse_line("flag=1\n") && b_val == 1);
    CHECK(settings_parse_line("mode=off") && e_val == 2);
    CHECK(settings_parse_line("mode=1") && e_val == 1);
    CHECK(settings_parse_line("mode=sideways") && e_val == 1);   /* bad value: kept */
    CHECK(settings_parse_line("gain=1.25") && i_val == 125);
    CHECK(settings_parse_line("gain=9") && i_val == 300);         /* clamped */
    CHECK(!settings_parse_line("other=1"));
    CHECK(!settings_parse_line("no equals sign"));
    CHECK(saves == 0);   /* loading saves nothing */

    /* stepping, saving */
    settings_on_change(save);
    const setting *m = settings_find("mode"), *g = settings_find("gain"), *f = settings_find("flag");
    CHECK(m && g && f);
    settings_step(m, 1);
    CHECK(e_val == 2 && saves == 1);
    settings_step(m, 1);   /* wraps */
    CHECK(e_val == 0);
    settings_step(g, 1);   /* at the top: no change, no save */
    CHECK(i_val == 300 && saves == 2);
    settings_step(g, -1);
    CHECK(i_val == 275 && saves == 3);
    settings_step(f, 1);
    CHECK(b_val == 0);
    char buf[64];
    CHECK(!strcmp(settings_value_text(g, buf, sizeof buf), "275%"));
    CHECK(!strcmp(settings_value_text(m, buf, sizeof buf), "Auto"));
    CHECK(!strcmp(settings_value_text(f, buf, sizeof buf), "Off"));
    CHECK(!settings_is_enabled(settings_at(3)));

    /* settings.ini out */
    char out[512] = {0};
    FILE *mf = tmpfile();
    CHECK(mf != NULL);
    if (mf) {
        settings_write_lines(mf);
        rewind(mf);
        size_t n = fread(out, 1, sizeof out - 1, mf);
        out[n] = 0;
        fclose(mf);
    }
    CHECK(!strcmp(out, "flag=0\nmode=auto\ngain=2.75\n"));

    /* the same key again replaces the item; a category goes */
    settings_register(&(setting){.key = "mode", .label = "Mode 2", .category = "A",
                                 .type = SETTING_ENUM, .options = labels, .values = values,
                                 .count = 3, .get = get_e, .set = set_e});
    CHECK(settings_count() == 4 && !strcmp(settings_find("mode")->label, "Mode 2"));
    settings_unregister_category("A");
    CHECK(settings_count() == 2 && !settings_find("mode") && settings_find("flag"));

    if (!fails)
        printf("test_settings: ok\n");
    return fails ? 1 : 0;
}
