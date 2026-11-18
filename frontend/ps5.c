/* SPDX-FileCopyrightText: 2026 ct-recomp contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: PlayStation 5 homebrew (ps5.h, docs/PS5.md).
 */
#ifdef CT_PS5

#include "ps5.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* libkernel and libSceSystemService (linked through SDL2). */
typedef struct {
    char reserved[45];
    char message[3075];
} ps5_notify_request;
int sceKernelSendNotificationRequest(int device, ps5_notify_request *req, size_t size,
                                     int blocking);
int sceSystemServiceHideSplashScreen(void);

/* The program's own folder (a homebrew launcher's app folder) first, then
   USB: a drive the player plugged in wins over an older copy in /data. */
static char self_dir[256];
static const char *dirs[] = {
    self_dir,
    "/mnt/usb0/ct-recomp",
    "/mnt/usb1/ct-recomp",
    "/data/ct-recomp",
};
#define N_DIRS (sizeof dirs / sizeof *dirs)

static int is_dir(const char *p)
{
    struct stat st;
    return !stat(p, &st) && S_ISDIR(st.st_mode);
}

static int is_file(const char *p)
{
    struct stat st;
    return !stat(p, &st) && S_ISREG(st.st_mode);
}

void ps5_notify(const char *fmt, ...)
{
    ps5_notify_request req;
    memset(&req, 0, sizeof req);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(req.message, sizeof req.message, fmt, ap);
    va_end(ap);
    fprintf(stderr, "ct_ps5: notify: %s\n", req.message);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

void ps5_init(const char *argv0)
{
    const char *slash = argv0 ? strrchr(argv0, '/') : NULL;
    if (slash && argv0[0] == '/' && (size_t)(slash - argv0) < sizeof self_dir)
        snprintf(self_dir, sizeof self_dir, "%.*s", (int)(slash - argv0), argv0);
    else if (!getcwd(self_dir, sizeof self_dir) || !strcmp(self_dir, "/"))
        self_dir[0] = 0;   /* no usable folder: is_dir("") fails */
    mkdir("/data/ct-recomp", 0777);   /* so the log always has a home */
    const char *log_dir = "/data/ct-recomp";
    for (size_t k = 0; k < N_DIRS; k++)
        if (is_dir(dirs[k])) {
            log_dir = dirs[k];
            break;
        }
    char path[256];
    snprintf(path, sizeof path, "%s/ct-recomp.log", log_dir);
    if (freopen(path, "w", stderr))
        setvbuf(stderr, NULL, _IOLBF, 0);   /* line by line: survives a crash */
    if (freopen(path, "a", stdout))
        setvbuf(stdout, NULL, _IOLBF, 0);
    fprintf(stderr, "ct_ps5: log %s (program folder \"%s\")\n", path, self_dir);
    sceSystemServiceHideSplashScreen();
}

/* rom.sfc in dir, else the first *.sfc / *.smc there: 1 with its path in
   out, or 0. */
static int find_rom_in(const char *dir, char *out, size_t n)
{
    if (!*dir)
        return 0;
    snprintf(out, n, "%s/rom.sfc", dir);
    if (is_file(out))
        return 1;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        const char *dot = strrchr(e->d_name, '.');
        if (dot && (!strcasecmp(dot, ".sfc") || !strcasecmp(dot, ".smc"))) {
            snprintf(out, n, "%s/%s", dir, e->d_name);
            found = is_file(out);
        }
    }
    closedir(d);
    return found;
}

int ps5_find_files(const char **rom, const char **module, int want_module)
{
    static char rom_buf[512], mod_buf[512];
    const char *rom_dir = NULL;
    if (!*rom) {
        for (size_t k = 0; k < N_DIRS && !*rom; k++)
            if (find_rom_in(dirs[k], rom_buf, sizeof rom_buf)) {
                *rom = rom_buf;
                rom_dir = dirs[k];
            }
        if (!*rom) {
            ps5_notify("ct-recomp: no ROM found.\nCopy your own headerless dump as "
                       "rom.sfc into %s", self_dir[0] ? self_dir : "/data/ct-recomp");
            return 1;
        }
    }
    fprintf(stderr, "ct_ps5: ROM %s\n", *rom);
    if (want_module && module && !*module) {
        /* Beside the ROM first, then every folder. */
        if (rom_dir) {
            snprintf(mod_buf, sizeof mod_buf, "%s/ct_game.so", rom_dir);
            if (is_file(mod_buf))
                *module = mod_buf;
        }
        for (size_t k = 0; k < N_DIRS && !*module; k++) {
            snprintf(mod_buf, sizeof mod_buf, "%s/ct_game.so", dirs[k]);
            if (is_file(mod_buf))
                *module = mod_buf;
        }
        if (*module)
            fprintf(stderr, "ct_ps5: game module %s\n", *module);
        else
            fprintf(stderr, "ct_ps5: no ct_game.so: running in the interpreter only\n");
    }
    return 0;
}

#else
typedef int ct_ps5_unused;   /* ISO C: no empty translation unit */
#endif /* CT_PS5 */
