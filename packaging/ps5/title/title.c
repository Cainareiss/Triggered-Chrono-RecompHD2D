/* SPDX-FileCopyrightText: 2026 ct-recomp contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: PS5 title build (docs/PS5.md). The engine is linked in from
 * vendor/lib/libct_title.a; its main is frontend/main.c. This file holds
 * the few C library functions the console's libc doesn't export, which
 * the payload SDK's static libc used to provide.
 */
#include <langinfo.h>
#include <stddef.h>
#include <time.h>

/* localtime_r: the console's localtime, copied out. */
struct tm *localtime_r(const time_t *t, struct tm *out)
{
    struct tm *p = localtime(t);
    if (!p)
        return NULL;
    *out = *p;
    return out;
}

/* libiconv's locale queries: the C locale, which is all the port uses. */
char *nl_langinfo(nl_item item)
{
    static char utf8[] = "UTF-8", empty[] = "";
    return item == CODESET ? utf8 : empty;
}

int ___mb_cur_max(void)
{
    return 1;
}
