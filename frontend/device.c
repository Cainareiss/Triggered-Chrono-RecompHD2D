/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Device detection (device.h). */
#include "device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

int device_game_mode(const char **why)
{
    const char *v, *w = NULL;
    if ((v = getenv("GAMESCOPE_WAYLAND_DISPLAY")) && *v)
        w = "GAMESCOPE_WAYLAND_DISPLAY";
    else if ((v = getenv("SteamDeck")) && !strcmp(v, "1"))
        w = "SteamDeck=1";
    else if ((v = getenv("SteamGamepadUI")) && *v)
        w = "SteamGamepadUI";
    else if ((v = getenv("XDG_CURRENT_DESKTOP")) && !strcmp(v, "gamescope"))
        w = "XDG_CURRENT_DESKTOP=gamescope";
    if (why)
        *why = w;
    return w != NULL;
}

int device_product_kind(const char *p)
{
    if (!p)
        return DEVICE_PC;
    if (!strcmp(p, "Jupiter") || !strcmp(p, "Galileo"))
        return DEVICE_STEAM_DECK;
    if (strstr(p, "ROG Ally"))
        return DEVICE_ROG_ALLY;
    return DEVICE_PC;
}

const char *device_product(void)
{
    static char name[128];
    static int done;
    if (!done) {
        done = 1;
        const char *env = getenv("CT_DEVICE_PRODUCT");
        if (env) {
            snprintf(name, sizeof name, "%s", env);
        } else {
            FILE *f = fopen("/sys/class/dmi/id/product_name", "r");
            if (f) {
                if (!fgets(name, sizeof name, f))
                    name[0] = 0;
                fclose(f);
            }
        }
        name[strcspn(name, "\r\n")] = 0;
    }
    return name;
}

int device_kind(void)
{
    return device_product_kind(device_product());
}

int device_renderer_is_deck(const char *r)
{
    return r && (strstr(r, "VANGOGH") || strstr(r, "Custom GPU 0405"));
}

int device_gl_is_deck(const char **renderer)
{
    const unsigned char *(*get)(unsigned) = NULL;
    const char *r = NULL;
    if (SDL_GL_GetCurrentContext()) {
        *(void **)&get = SDL_GL_GetProcAddress("glGetString");
        if (get)
            r = (const char *)get(0x1F01);   /* GL_RENDERER */
    }
    if (renderer)
        *renderer = r ? r : "";
    return device_renderer_is_deck(r);
}

const char *device_no_pad_hint(int kind, int controllers)
{
    if (kind == DEVICE_PC || controllers > 0)
        return NULL;
    return "No controller found. Launch from Steam, or see HANDHELD.md (InputPlumber)";
}
