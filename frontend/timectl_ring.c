/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the rewind ring (timectl_ring.h). */
#include "timectl_ring.h"

#include <stdlib.h>
#include <string.h>

/* A delta: the older state XOR the newer (the shorter padded with zeros),
   as runs: [zeros: varint][literals: varint][literal bytes], repeated to
   the end of the longer one. */

static size_t put_varint(uint8_t *p, size_t v)
{
    size_t n = 0;
    while (v >= 0x80) {
        p[n++] = (uint8_t)(v | 0x80);
        v >>= 7;
    }
    p[n++] = (uint8_t)v;
    return n;
}

static int get_varint(const uint8_t *p, size_t len, size_t *pos, size_t *v)
{
    size_t r = 0;
    for (int shift = 0; shift < 63; shift += 7) {
        if (*pos >= len)
            return -1;
        uint8_t b = p[(*pos)++];
        r |= (size_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            *v = r;
            return 0;
        }
    }
    return -1;
}

size_t tcr_delta_max(size_t nl, size_t ol)
{
    size_t n = nl > ol ? nl : ol;
    return 2 * n + 64;   /* literals, plus two small varints per run of 5 or more bytes */
}

static inline uint8_t at(const uint8_t *b, size_t len, size_t i) { return i < len ? b[i] : 0; }

size_t tcr_delta_enc(const uint8_t *nw, size_t nl, const uint8_t *ol, size_t ol_len, uint8_t *out,
                     size_t cap)
{
    size_t n = nl > ol_len ? nl : ol_len, common = nl < ol_len ? nl : ol_len;
    size_t o = 0, i = 0;
    while (i < n) {
        size_t z = i;   /* a zero run */
        while (z < common) {
            if (z + 8 <= common) {
                uint64_t a, b;
                memcpy(&a, nw + z, 8);
                memcpy(&b, ol + z, 8);
                if (a == b) {
                    z += 8;
                    continue;
                }
            }
            if (nw[z] != ol[z])
                break;
            z++;
        }
        if (z >= common)   /* the tail past the shorter one: older's bytes, or newer's */
            while (z < n && at(nw, nl, z) == at(ol, ol_len, z))
                z++;
        size_t l = z;   /* literals: until a run of 4 equal bytes (cheaper to end the literal) */
        while (l < n) {
            if (at(nw, nl, l) == at(ol, ol_len, l)) {
                size_t e = l;
                while (e < n && e - l < 4 && at(nw, nl, e) == at(ol, ol_len, e))
                    e++;
                if (e - l >= 4 || e == n)
                    break;
                l = e;
            } else {
                l++;
            }
        }
        if (o + 20 + (l - z) > cap)
            return 0;
        o += put_varint(out + o, z - i);
        o += put_varint(out + o, l - z);
        for (size_t k = z; k < l; k++)
            out[o++] = at(nw, nl, k) ^ at(ol, ol_len, k);
        i = l;
    }
    return o;
}

size_t tcr_delta_dec(uint8_t *buf, size_t nl, size_t ol, const uint8_t *d, size_t dl)
{
    size_t n = nl > ol ? nl : ol;
    if (ol > nl)
        memset(buf + nl, 0, ol - nl);
    size_t pos = 0, i = 0;
    while (pos < dl) {
        size_t z, l;
        if (get_varint(d, dl, &pos, &z) || get_varint(d, dl, &pos, &l) || z > n - i ||
            l > n - i - z || l > dl - pos)
            return 0;
        i += z;
        for (size_t k = 0; k < l; k++)
            buf[i + k] ^= d[pos + k];
        pos += l;
        i += l;
    }
    return ol;   /* (trailing equal bytes need no run) */
}

/* ---- the ring ---- */

typedef struct {
    uint8_t *data;
    size_t len;        /* the delta's bytes */
    size_t older_len;  /* the state it rebuilds */
    long frame;        /* ... and its frame */
} delta;

struct tcr {
    uint8_t *cur;      /* the newest, in full (cap_cur bytes of room) */
    size_t cur_len, cap_cur;
    long cur_frame;
    int have;
    delta *d;          /* circular: d[(first + k) % cap_d], k = 0 the oldest delta */
    int cap_d, first, nd;
    int max_entries;
    size_t max_bytes, bytes;   /* bytes: cur_len plus every delta */
};

tcr *tcr_new(int max_entries, size_t max_bytes)
{
    tcr *r = calloc(1, sizeof *r);
    if (!r)
        return NULL;
    tcr_limits(r, max_entries, max_bytes);
    return r;
}

static void drop_oldest(tcr *r)
{
    delta *e = &r->d[r->first];
    r->bytes -= e->len;
    free(e->data);
    memset(e, 0, sizeof *e);
    r->first = (r->first + 1) % r->cap_d;
    r->nd--;
}

static void trim(tcr *r)
{
    while (r->nd > 0 && ((int)r->nd + 1 > r->max_entries ||
                         (r->max_bytes && r->bytes > r->max_bytes)))
        drop_oldest(r);
}

void tcr_limits(tcr *r, int max_entries, size_t max_bytes)
{
    r->max_entries = max_entries < 1 ? 1 : max_entries;
    r->max_bytes = max_bytes;
    if (r->cap_d < r->max_entries) {   /* grow the circular array, in order */
        int nc = r->max_entries;
        delta *nd = calloc((size_t)nc, sizeof *nd);
        if (nd) {
            for (int k = 0; k < r->nd; k++)
                nd[k] = r->d[(r->first + k) % r->cap_d];
            free(r->d);
            r->d = nd;
            r->cap_d = nc;
            r->first = 0;
        } else {
            r->max_entries = r->cap_d ? r->cap_d : 1;   /* no memory: keep what fits */
        }
    }
    if (r->cap_d)
        trim(r);
}

void tcr_clear(tcr *r)
{
    while (r->nd > 0)
        drop_oldest(r);
    r->have = 0;
    r->cur_len = 0;
    r->bytes = 0;
    r->first = 0;
}

void tcr_free(tcr *r)
{
    if (!r)
        return;
    tcr_clear(r);
    free(r->d);
    free(r->cur);
    free(r);
}

int tcr_push(tcr *r, const uint8_t *blob, size_t len, long frame)
{
    if (!r->cap_d)
        return -1;
    if (r->have) {
        size_t cap = tcr_delta_max(len, r->cur_len);
        uint8_t *tmp = malloc(cap);
        if (!tmp)
            return -1;
        size_t n = tcr_delta_enc(blob, len, r->cur, r->cur_len, tmp, cap);
        if (!n && (len || r->cur_len)) {   /* did not fit: cannot happen within tcr_delta_max */
            free(tmp);
            return -1;
        }
        if (r->nd == r->cap_d)
            drop_oldest(r);
        uint8_t *keep = realloc(tmp, n ? n : 1);   /* shrink to what it is */
        delta *e = &r->d[(r->first + r->nd) % r->cap_d];
        e->data = keep ? keep : tmp;
        e->len = n;
        e->older_len = r->cur_len;
        e->frame = r->cur_frame;
        r->nd++;
        r->bytes += n;
    }
    if (len > r->cap_cur) {
        uint8_t *nb = realloc(r->cur, len);
        if (!nb)
            return -1;
        r->cur = nb;
        r->cap_cur = len;
    }
    memcpy(r->cur, blob, len);
    r->bytes += len;
    r->bytes -= r->have ? r->cur_len : 0;
    r->cur_len = len;
    r->cur_frame = frame;
    r->have = 1;
    trim(r);
    return 0;
}

int tcr_count(const tcr *r) { return r->have ? r->nd + 1 : 0; }
size_t tcr_bytes(const tcr *r) { return r->bytes; }

const uint8_t *tcr_newest(const tcr *r, size_t *len, long *frame)
{
    if (!r->have)
        return NULL;
    if (len)
        *len = r->cur_len;
    if (frame)
        *frame = r->cur_frame;
    return r->cur;
}

int tcr_drop_newest(tcr *r)
{
    if (!r->have || r->nd == 0)
        return -1;
    delta *e = &r->d[(r->first + r->nd - 1) % r->cap_d];
    size_t need = r->cur_len > e->older_len ? r->cur_len : e->older_len;
    if (need > r->cap_cur) {
        uint8_t *nb = realloc(r->cur, need);
        if (!nb)
            return -1;
        r->cur = nb;
        r->cap_cur = need;
    }
    size_t ol = tcr_delta_dec(r->cur, r->cur_len, e->older_len, e->data, e->len);
    if (!ol && e->older_len)
        return -1;   /* damaged: cannot happen */
    r->bytes -= r->cur_len + e->len;
    r->bytes += ol;
    r->cur_len = ol;
    r->cur_frame = e->frame;
    free(e->data);
    memset(e, 0, sizeof *e);
    r->nd--;
    return 0;
}
