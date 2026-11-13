/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Save RAM on disk: see saves.h. */
#include "saves.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>

#include "bus.h"

#define PATHLEN 4096

static char path[PATHLEN];   /* "" while saving is off */
static uint8_t on_disk[CT_SRAM_SIZE];   /* what the file holds */
static uint8_t prev[CT_SRAM_SIZE];      /* the SRAM at the previous frame edge */
static Uint32 last_write, dirty_since;
static int have_write, dirty, exit_hooked;
static const char *dump_path;
static long cur_frame = -1;   /* for the log: the frame a timed write follows */

static void at_exit(void);

static void hook_exit(void)
{
    if (!exit_hooked) {
        exit_hooked = 1;
        atexit(at_exit);
    }
}

/* Move a wrong-size file out of the way: FILE.bad, else FILE.bad.N. */
static void set_aside(void)
{
    char bad[PATHLEN + 16];
    for (int n = 0; n < 100; n++) {
        if (n)
            snprintf(bad, sizeof bad, "%s.bad.%d", path, n);
        else
            snprintf(bad, sizeof bad, "%s.bad", path);
        if (access(bad, F_OK) && errno == ENOENT) {
            if (rename(path, bad))
                break;
            fprintf(stderr, "ct_sdl: save: WARNING: %s is not an %u-byte save file; kept it "
                            "as %s and started without a save\n", path, CT_SRAM_SIZE, bad);
            return;
        }
    }
    fprintf(stderr, "ct_sdl: save: WARNING: %s is not an %u-byte save file and cannot be "
                    "moved aside (%s): saving is off\n", path, CT_SRAM_SIZE, strerror(errno));
    path[0] = 0;
}

int saves_open(const char *file, const char *game_id)
{
    if (file) {
        snprintf(path, sizeof path, "%s", file);
    } else {
        char *dir = SDL_GetPrefPath("ct-recomp", game_id);
        if (!dir) {
            fprintf(stderr, "ct_sdl: save: no preferences folder (%s): saving is off\n",
                    SDL_GetError());
            return 1;
        }
        snprintf(path, sizeof path, "%s%s.srm", dir, game_id);
        SDL_free(dir);
    }
    uint8_t *sram = bus_sram();
    memcpy(on_disk, sram, CT_SRAM_SIZE);   /* no file: the power-on SRAM counts as saved */
    FILE *f = fopen(path, "rb");
    if (!f && errno != ENOENT) {
        fprintf(stderr, "ct_sdl: save: cannot read %s (%s): saving is off\n", path,
                strerror(errno));
        path[0] = 0;
        return 1;
    }
    if (f) {
        size_t n = fread(on_disk, 1, CT_SRAM_SIZE, f);
        int extra = fgetc(f), err = ferror(f);
        fclose(f);
        if (err) {
            fprintf(stderr, "ct_sdl: save: read error on %s: saving is off\n", path);
            path[0] = 0;
            return 1;
        }
        if (n != CT_SRAM_SIZE || extra != EOF) {
            memcpy(on_disk, sram, CT_SRAM_SIZE);
            set_aside();
            if (!path[0])
                return 1;
        } else {
            memcpy(sram, on_disk, CT_SRAM_SIZE);
            fprintf(stderr, "ct_sdl: save: loaded %s\n", path);
        }
    } else {
        fprintf(stderr, "ct_sdl: save: %s (new)\n", path);
    }
    memcpy(prev, sram, CT_SRAM_SIZE);
    hook_exit();
    return 0;
}

static int fsync_dir(const char *file)
{
    char dir[PATHLEN];
    snprintf(dir, sizeof dir, "%s", file);
    char *slash = strrchr(dir, '/');
    if (slash)
        slash[slash == dir] = 0;   /* "/x" -> "/" */
    else
        snprintf(dir, sizeof dir, ".");
    int fd = open(dir, O_RDONLY);
    if (fd < 0)
        return -1;
    int r = fsync(fd);
    close(fd);
    return r;
}

/* FILE.tmp, fsync, FILE -> FILE.bak (a hard link, else a copy), FILE.tmp
   -> FILE. 0 on success. */
static int write_atomic(const uint8_t *data)
{
    char tmp[PATHLEN + 8], bak[PATHLEN + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    snprintf(bak, sizeof bak, "%s.bak", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    size_t done = 0;
    while (done < CT_SRAM_SIZE) {
        ssize_t w = write(fd, data + done, CT_SRAM_SIZE - done);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            break;
        done += (size_t)w;
    }
    if (done != CT_SRAM_SIZE || fsync(fd)) {
        close(fd);
        remove(tmp);
        return -1;
    }
    if (close(fd)) {
        remove(tmp);
        return -1;
    }
    if (!access(path, F_OK)) {
        remove(bak);
        if (link(path, bak)) {
            /* No hard links here: copy it. */
            FILE *in = fopen(path, "rb"), *out = in ? fopen(bak, "wb") : NULL;
            uint8_t buf[CT_SRAM_SIZE];
            size_t n = in ? fread(buf, 1, sizeof buf, in) : 0;
            if (out)
                fwrite(buf, 1, n, out);
            if (out)
                fclose(out);
            if (in)
                fclose(in);
        }
    }
    if (rename(tmp, path)) {
        remove(tmp);
        return -1;
    }
    fsync_dir(path);
    return 0;
}

static int save_now(const uint8_t *sram)
{
    if (write_atomic(sram)) {
        fprintf(stderr, "ct_sdl: save: cannot write %s (%s)\n", path, strerror(errno));
        return 1;   /* on_disk unchanged: retried after the interval */
    }
    memcpy(on_disk, sram, CT_SRAM_SIZE);
    if (cur_frame >= 0)
        fprintf(stderr, "ct_sdl: save: wrote %s (after frame %ld)\n", path, cur_frame);
    else
        fprintf(stderr, "ct_sdl: save: wrote %s (at exit)\n", path);
    return 0;
}

#define MAX_POKES 16
static struct { long frame; unsigned addr, val; } pokes[MAX_POKES];
static int n_pokes;

int saves_poke(const char *spec)
{
    long f;
    unsigned a, v;
    char end;
    if (n_pokes == MAX_POKES || sscanf(spec, "%ld:%x=%x%c", &f, &a, &v, &end) != 3 ||
        a >= CT_SRAM_SIZE || v > 0xFF)
        return -1;
    pokes[n_pokes].frame = f;
    pokes[n_pokes].addr = a;
    pokes[n_pokes].val = v;
    n_pokes++;
    return 0;
}

void saves_frame(long f)
{
    for (int k = 0; k < n_pokes; k++)
        if (pokes[k].frame == f)
            bus_sram()[pokes[k].addr] = (uint8_t)pokes[k].val;
    if (!path[0])
        return;
    const uint8_t *sram = bus_sram();
    int stable = !memcmp(sram, prev, CT_SRAM_SIZE);
    if (!stable)
        memcpy(prev, sram, CT_SRAM_SIZE);
    if (!memcmp(sram, on_disk, CT_SRAM_SIZE)) {
        dirty = 0;
        return;
    }
    Uint32 now = SDL_GetTicks();
    if (!dirty) {
        dirty = 1;
        dirty_since = now;
    }
    /* Not in the middle of the game's own write, unless it never settles. */
    if (!stable && now - dirty_since < 2 * SAVES_INTERVAL_MS)
        return;
    if (have_write && now - last_write < SAVES_INTERVAL_MS)
        return;
    last_write = now;
    have_write = 1;
    cur_frame = f;
    if (!save_now(sram))
        dirty = 0;
    cur_frame = -1;
}

void saves_flush(void)
{
    if (path[0] && memcmp(bus_sram(), on_disk, CT_SRAM_SIZE))
        save_now(bus_sram());
}

void saves_dump_at_exit(const char *file)
{
    dump_path = file;
    hook_exit();
}

static void at_exit(void)
{
    saves_flush();
    if (!dump_path)
        return;
    FILE *f = fopen(dump_path, "wb");
    int ok = f && fwrite(bus_sram(), 1, CT_SRAM_SIZE, f) == CT_SRAM_SIZE;
    if (f && fclose(f))
        ok = 0;
    fprintf(stderr, "ct_sdl: SRAM %s %s\n", ok ? "dumped to" : "cannot be dumped to", dump_path);
}
