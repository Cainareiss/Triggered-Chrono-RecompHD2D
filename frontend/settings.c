/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the settings registry (settings.h). */
#include "settings.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static setting items[SETTINGS_MAX];
static int n_items;
#define MAX_CATS 24
static const char *cats[MAX_CATS];
static int n_cats;
static void (*saver)(void), (*refresher)(void);
static const char *(*key_name)(int);

static int find_index(const char *key)
{
    for (int k = 0; key && k < n_items; k++)
        if (items[k].key && !strcmp(items[k].key, key))
            return k;
    return -1;
}

void settings_category_add(const char *c)
{
    for (int k = 0; k < n_cats; k++)
        if (!strcmp(cats[k], c))
            return;
    if (n_cats < MAX_CATS)
        cats[n_cats++] = c;
}

int settings_register(const setting *s)
{
    int k = find_index(s->key);
    if (k < 0) {
        if (n_items == SETTINGS_MAX)
            return -1;
        k = n_items++;
    }
    items[k] = *s;
    if (s->category)
        settings_category_add(s->category);
    return k;
}

void settings_unregister_category(const char *c)
{
    int out = 0;
    for (int k = 0; k < n_items; k++)
        if (!items[k].category || strcmp(items[k].category, c))
            items[out++] = items[k];
    n_items = out;
}

int settings_count(void) { return n_items; }
const setting *settings_at(int k) { return k >= 0 && k < n_items ? &items[k] : NULL; }

const setting *settings_find(const char *key)
{
    int k = find_index(key);
    return k < 0 ? NULL : &items[k];
}

int settings_categories(const char **out, int max)
{
    int n = 0;
    for (int k = 0; k < n_cats && n < max; k++)
        out[n++] = cats[k];
    return n;
}

static int lo(const setting *s) { return s->type == SETTING_INT ? s->min : 0; }

static int hi(const setting *s)
{
    switch (s->type) {
    case SETTING_BOOL: return 1;
    case SETTING_ENUM: return s->count > 0 ? s->count - 1 : 0;
    case SETTING_INT: return s->max;
    default: return 0x7fffffff;
    }
}

int settings_get_value(const setting *s)
{
    return s->get ? s->get(s->user) : 0;
}

static void apply(const setting *s, int v)
{
    if (s->type == SETTING_BOOL || s->type == SETTING_ENUM || s->type == SETTING_INT) {
        if (v < lo(s))
            v = lo(s);
        if (v > hi(s))
            v = hi(s);
    }
    if (s->set)
        s->set(s->user, v);
}

void settings_set_value(const setting *s, int v)
{
    apply(s, v);
    if (!(s->flags & SETTING_NO_SAVE))
        settings_changed();
}

void settings_step(const setting *s, int dir)
{
    int v = settings_get_value(s);
    switch (s->type) {
    case SETTING_BOOL:
        v = !v;
        break;
    case SETTING_ENUM:
        if (s->count <= 0)
            return;
        v = ((v + dir) % s->count + s->count) % s->count;
        break;
    case SETTING_INT: {
        int step = s->step > 0 ? s->step : 1;
        v += dir * step;
        if (v < s->min)
            v = s->min;
        if (v > s->max)
            v = s->max;
        if (v == settings_get_value(s))
            return;
        break;
    }
    default:
        return;
    }
    settings_set_value(s, v);
}

int settings_is_enabled(const setting *s)
{
    return !s->enabled || s->enabled(s->user);
}

static const char *const bool_names[2] = {"off", "on"};

const char *settings_value_text(const setting *s, char *buf, size_t n)
{
    int v = settings_get_value(s);
    switch (s->type) {
    case SETTING_BOOL:
        snprintf(buf, n, "%s", v ? "On" : "Off");
        return buf;
    case SETTING_ENUM:
        if (s->option)
            return s->option(s->user, v, buf, n);
        snprintf(buf, n, "%s", v >= 0 && v < s->count && s->options ? s->options[v] : "?");
        return buf;
    case SETTING_INT:
        snprintf(buf, n, s->format ? s->format : "%d", v);
        return buf;
    case SETTING_KEY: {
        const char *name = v > 0 && key_name ? key_name(v) : NULL;
        snprintf(buf, n, "%s", name && *name ? name : "None");
        return buf;
    }
    default:
        if (s->text)
            return s->text(s->user, buf, n);
        buf[0] = 0;
        return buf;
    }
}

/* "on" / "off" and friends: 1, 0, or -1. */
static int parse_bool(const char *v)
{
    static const char *const yes[] = {"on", "1", "true", "yes"}, *const no[] = {"off", "0", "false", "no"};
    for (int k = 0; k < 4; k++) {
        if (!strcmp(v, yes[k]))
            return 1;
        if (!strcmp(v, no[k]))
            return 0;
    }
    return -1;
}

static int parse_value(const setting *s, const char *v, int *out)
{
    char *end;
    switch (s->type) {
    case SETTING_BOOL:
        if (s->values)
            for (int k = 0; k < 2; k++)
                if (!strcmp(v, s->values[k]))
                    return *out = k, 0;
        return (*out = parse_bool(v)) < 0 ? -1 : 0;
    case SETTING_ENUM:
        for (int k = 0; s->values && k < s->count; k++)
            if (!strcmp(v, s->values[k]))
                return *out = k, 0;
        *out = (int)strtol(v, &end, 10);
        return end != v && !*end && *out >= 0 && *out < s->count ? 0 : -1;
    case SETTING_INT:
        if (s->ini_div) {
            double d = strtod(v, &end);
            *out = (int)(d * s->ini_div + (d < 0 ? -0.5 : 0.5));
        } else {
            *out = (int)strtol(v, &end, 10);
        }
        return end != v && !*end ? 0 : -1;
    case SETTING_KEY:
        *out = (int)strtol(v, &end, 10);
        return end != v && !*end ? 0 : -1;
    default:
        return -1;
    }
}

int settings_parse_line(const char *line)
{
    const char *eq = strchr(line, '=');
    if (!eq)
        return 0;
    char key[64], val[192];
    size_t kn = (size_t)(eq - line);
    if (kn == 0 || kn >= sizeof key)
        return 0;
    memcpy(key, line, kn);
    key[kn] = 0;
    int k = find_index(key);
    if (k < 0)
        return 0;
    snprintf(val, sizeof val, "%s", eq + 1);
    size_t n = strlen(val);
    while (n && isspace((unsigned char)val[n - 1]))
        val[--n] = 0;
    int v;
    if (parse_value(&items[k], val, &v))
        fprintf(stderr, "ct_sdl: settings.ini: bad value for %s: \"%s\"\n", key, val);
    else
        apply(&items[k], v);
    return 1;
}

void settings_write_lines(FILE *f)
{
    for (int k = 0; k < n_items; k++) {
        const setting *s = &items[k];
        if (!s->key || !s->get)
            continue;
        int v = s->get(s->user);
        switch (s->type) {
        case SETTING_BOOL:
            fprintf(f, "%s=%s\n", s->key, (s->values ? s->values : bool_names)[v ? 1 : 0]);
            break;
        case SETTING_ENUM:
            if (s->values && v >= 0 && v < s->count)
                fprintf(f, "%s=%s\n", s->key, s->values[v]);
            else
                fprintf(f, "%s=%d\n", s->key, v);
            break;
        case SETTING_INT:
            if (s->ini_div)
                fprintf(f, "%s=%.2f\n", s->key, (double)v / s->ini_div);
            else
                fprintf(f, "%s=%d\n", s->key, v);
            break;
        case SETTING_KEY:
            fprintf(f, "%s=%d\n", s->key, v);
            break;
        default:
            break;
        }
    }
}

void settings_on_change(void (*save)(void)) { saver = save; }
void settings_changed(void)
{
    if (saver)
        saver();
}
void settings_on_refresh(void (*refresh)(void)) { refresher = refresh; }
void settings_refresh(void)
{
    if (refresher)
        refresher();
}
void settings_key_namer(const char *(*name)(int)) { key_name = name; }
