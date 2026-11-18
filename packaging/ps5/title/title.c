/* SPDX-FileCopyrightText: 2026 ct-recomp contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: PS5 title build (docs/PS5.md). The engine is linked in from
 * vendor/lib/libct_title.a; its main is frontend/main.c.
 *
 * A title's process doesn't bind every libc import a payload gets: one
 * that isn't bound is left pointing at unmapped memory, and the first call
 * crashes (getcwd did, on 13.20). So this file:
 *
 *  - defines the libc functions known or likely to be unbound, on top of
 *    libkernel (which titles do get): getcwd, the environment, directory
 *    streams, and a few small ones. A definition here wins over the import.
 *  - checks every remaining import before main (ct_title_check_imports,
 *    a constructor) and writes the unbound ones to
 *    /data/ct-recomp/imports.log, using only libkernel, then shows them in a
 *    notification. The table comes from imports.gen.h (gen_imports.py).
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

/* ---- libkernel ---- */
typedef struct {
    void *start, *end;
    long offset;
    int protection, memory_type;
    unsigned flags;
    char name[32];
} ct_vquery_info;
int sceKernelVirtualQuery(const void *addr, int flags, ct_vquery_info *info, size_t size);
typedef struct {
    char reserved[45];
    char message[3075];
} ct_notify_request;
int sceKernelSendNotificationRequest(int device, ct_notify_request *req, size_t size, int blocking);
int sceKernelOpen(const char *path, int flags, int mode);
long sceKernelWrite(int fd, const void *buf, size_t n);
int sceKernelClose(int fd);
int sceKernelMkdir(const char *path, int mode);
int sceKernelGetdents(int fd, char *buf, int n);

/* ---- small libc pieces ---- */
static size_t k_strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* Byte loops through volatile pointers: never turned into memset/memcpy
   calls, which are imports themselves (the import check runs before any
   import is known to work). */
static void k_zero(void *p, size_t n)
{
    volatile char *d = p;
    while (n--)
        *d++ = 0;
}

static void k_copy(void *to, const void *from, size_t n)
{
    volatile char *d = to;
    const volatile char *s = from;
    while (n--)
        *d++ = *s++;
}

static int k_streq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* ---- the environment: a process-local table ---- */
#define CT_ENV_MAX 64
static char *env_keys[CT_ENV_MAX], *env_vals[CT_ENV_MAX];
static char env_store[8192];
static size_t env_used;

static char *env_dup(const char *s)
{
    size_t n = k_strlen(s) + 1;
    if (env_used + n > sizeof env_store)
        return NULL;
    char *p = env_store + env_used;
    k_copy(p, s, n);
    env_used += n;
    return p;
}

char *getenv(const char *name)
{
    for (int k = 0; k < CT_ENV_MAX; k++)
        if (env_keys[k] && k_streq(env_keys[k], name))
            return env_vals[k];
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
    int free_slot = -1;
    for (int k = 0; k < CT_ENV_MAX; k++) {
        if (env_keys[k] && k_streq(env_keys[k], name)) {
            if (!overwrite)
                return 0;
            char *v = env_dup(value);
            if (!v)
                return errno = ENOMEM, -1;
            env_vals[k] = v;
            return 0;
        }
        if (!env_keys[k] && free_slot < 0)
            free_slot = k;
    }
    if (free_slot < 0)
        return errno = ENOMEM, -1;
    char *key = env_dup(name), *v = env_dup(value);
    if (!key || !v)
        return errno = ENOMEM, -1;
    env_keys[free_slot] = key;
    env_vals[free_slot] = v;
    return 0;
}

int unsetenv(const char *name)
{
    for (int k = 0; k < CT_ENV_MAX; k++)
        if (env_keys[k] && k_streq(env_keys[k], name))
            env_keys[k] = env_vals[k] = NULL;
    return 0;
}

/* ---- the working directory: the title's own folder ---- */
char *getcwd(char *buf, size_t size)
{
    static const char cwd[] = "/app0";
    if (!buf || size < sizeof cwd)
        return errno = ERANGE, NULL;
    k_copy(buf, cwd, sizeof cwd);
    return buf;
}

/* ---- directory streams, on getdents ---- */
struct _dirdesc {
    int fd, len, pos;
    char buf[4096];
};
static struct _dirdesc dir_pool[4];
static int dir_used[4];

DIR *opendir(const char *path)
{
    int slot = -1;
    for (int k = 0; k < 4; k++)
        if (!dir_used[k]) {
            slot = k;
            break;
        }
    if (slot < 0)
        return errno = EMFILE, NULL;
    int fd = sceKernelOpen(path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0)
        return errno = ENOENT, NULL;
    dir_used[slot] = 1;
    dir_pool[slot].fd = fd;
    dir_pool[slot].len = dir_pool[slot].pos = 0;
    return &dir_pool[slot];
}

struct dirent *readdir(DIR *d)
{
    for (;;) {
        if (d->pos >= d->len) {
            int n = sceKernelGetdents(d->fd, d->buf, sizeof d->buf);
            if (n <= 0)
                return NULL;
            d->len = n;
            d->pos = 0;
        }
        struct dirent *e = (struct dirent *)(d->buf + d->pos);
        if (e->d_reclen == 0)
            return NULL;
        d->pos += e->d_reclen;
        if (e->d_fileno)
            return e;
    }
}

int closedir(DIR *d)
{
    sceKernelClose(d->fd);
    dir_used[d - dir_pool] = 0;
    return 0;
}

/* ---- functions not in the console's libc (localtime_r, the locale) ---- */
struct tm *localtime_r(const time_t *t, struct tm *out)
{
    struct tm *p = localtime(t);
    if (!p)
        return NULL;
    *out = *p;
    return out;
}

char *nl_langinfo(nl_item item)
{
    static char utf8[] = "UTF-8", empty[] = "";
    return item == CODESET ? utf8 : empty;
}

int ___mb_cur_max(void)
{
    return 1;
}

char *strcasestr(const char *s, const char *find)
{
    size_t n = k_strlen(find);
    for (; *s; s++)
        if (!strncasecmp(s, find, n))
            return (char *)s;
    return n ? NULL : (char *)s;
}

pid_t fork(void)
{
    return errno = ENOSYS, -1;
}

int link(const char *a, const char *b)
{
    (void)a, (void)b;
    return errno = ENOSYS, -1;
}

/* ---- the import check ---- */
#include "imports.gen.h"   /* CT_IMPORTS(X): X(index, "name") for every import */

#define CT_DECL(i, name) extern char ct_import_##i __asm__(name);
CT_IMPORTS(CT_DECL)
#define CT_ENTRY(i, name) {name, &ct_import_##i},
static const struct {
    const char *name;
    const void *addr;
} imports[] = {CT_IMPORTS(CT_ENTRY)};

static void kwrite(int fd, const char *s)
{
    sceKernelWrite(fd, s, k_strlen(s));
}

static void khex(char *out, uintptr_t v)
{
    static const char hex[] = "0123456789abcdef";
    out[0] = '0', out[1] = 'x';
    for (int k = 0; k < 16; k++)
        out[2 + k] = hex[(v >> (60 - 4 * k)) & 15];
    out[18] = 0;
}

__attribute__((constructor)) static void ct_title_check_imports(void)
{
    sceKernelMkdir("/data/ct-recomp", 0777);
    int fd = sceKernelOpen("/data/ct-recomp/imports.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    static ct_notify_request req;   /* static: zeroed without memset */
    size_t at = 0, bad = 0;
    const char *head = "CT Recomp: unbound imports:";
    k_copy(req.message, head, k_strlen(head));
    at = k_strlen(head);
    for (size_t k = 0; k < sizeof imports / sizeof *imports; k++) {
        ct_vquery_info info;
        k_zero(&info, sizeof info);
        int r = sceKernelVirtualQuery(imports[k].addr, 0, &info, sizeof info);
        int ok = r == 0 && (const char *)imports[k].addr >= (const char *)info.start &&
                 (const char *)imports[k].addr < (const char *)info.end;
        if (fd >= 0) {
            char h[19];
            khex(h, (uintptr_t)imports[k].addr);
            kwrite(fd, ok ? "ok   " : "BAD  ");
            kwrite(fd, h);
            kwrite(fd, " ");
            kwrite(fd, imports[k].name);
            kwrite(fd, "\n");
        }
        if (!ok) {
            bad++;
            size_t n = k_strlen(imports[k].name);
            if (at + n + 2 < 300) {
                req.message[at++] = ' ';
                k_copy(req.message + at, imports[k].name, n);
                at += n;
            }
        }
    }
    if (fd >= 0)
        sceKernelClose(fd);
    if (bad)
        sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}
