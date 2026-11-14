/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PNG reading and writing for asset packs (pngio.h). The inflate follows
 * RFC 1951 directly: canonical Huffman codes decoded a bit at a time from
 * per-length counts, which is plenty fast for pack images. */
#include "pngio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- CRC-32 and Adler-32 ---- */

static uint32_t crc_table[256];

static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t k = 0; k < 256; k++) {
            uint32_t v = k;
            for (int b = 0; b < 8; b++)
                v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[k] = v;
        }
    for (size_t k = 0; k < n; k++)
        c = crc_table[(c ^ p[k]) & 0xFF] ^ (c >> 8);
    return c;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* ---- writer ---- */

static int write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t hdr[8], tail[4];
    put32(hdr, n);
    memcpy(hdr + 4, type, 4);
    uint32_t c = crc32_update(0xFFFFFFFFu, hdr + 4, 4);
    c = crc32_update(c, data, n) ^ 0xFFFFFFFFu;
    put32(tail, c);
    return fwrite(hdr, 1, 8, f) == 8 && (!n || fwrite(data, 1, n, f) == n) &&
                   fwrite(tail, 1, 4, f) == 4
               ? 0
               : -1;
}

int pngio_write_rgba(const char *path, const uint8_t *rgba, int w, int h)
{
    size_t row = (size_t)w * 4 + 1, raw_n = row * (size_t)h;
    size_t blocks = (raw_n + 65534) / 65535;
    uint8_t *z = malloc(2 + raw_n + blocks * 5 + 4);
    if (!z)
        return -1;
    size_t zn = 0;
    z[zn++] = 0x78;
    z[zn++] = 0x01;
    uint32_t a = 1, b = 0;
    size_t off = 0;   /* position in the raw scanline stream */
    while (off < raw_n) {
        size_t len = raw_n - off < 65535 ? raw_n - off : 65535;
        z[zn++] = off + len == raw_n;
        z[zn++] = (uint8_t)len;
        z[zn++] = (uint8_t)(len >> 8);
        z[zn++] = (uint8_t)~len;
        z[zn++] = (uint8_t)(~len >> 8);
        for (size_t k = off; k < off + len; k++) {
            size_t y = k / row, x = k % row;
            uint8_t v = x == 0 ? 0 : rgba[y * (size_t)w * 4 + x - 1];
            z[zn++] = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
        off += len;
    }
    put32(z + zn, b << 16 | a);
    zn += 4;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;   /* bit depth */
    ihdr[9] = 6;   /* RGBA */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    FILE *f = fopen(path, "wb");
    int rc = !f || fwrite(sig, 1, 8, f) != 8 || write_chunk(f, "IHDR", ihdr, 13) ||
                     write_chunk(f, "IDAT", z, (uint32_t)zn) || write_chunk(f, "IEND", NULL, 0)
                 ? -1
                 : 0;
    if (f && fclose(f))
        rc = -1;
    free(z);
    return rc;
}

/* ---- inflate ---- */

typedef struct {
    const uint8_t *src;
    size_t n, pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *dst;
    size_t cap, out;
    int bad;
} inf_state;

static int bits(inf_state *s, int need)
{
    uint32_t v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->pos >= s->n) {
            s->bad = 1;
            return 0;
        }
        v |= (uint32_t)s->src[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int)(v & ((1u << need) - 1));
}

/* A canonical Huffman code: how many codes of each length 1-15, and the
   symbols in code order. */
typedef struct {
    short count[16];
    short symbol[288];
} huff;

static int huff_build(huff *h, const uint8_t *lengths, int n)
{
    memset(h->count, 0, sizeof h->count);
    for (int k = 0; k < n; k++)
        h->count[lengths[k]]++;
    if (h->count[0] == n)
        return 0;   /* no codes: only an error if used */
    int left = 1;   /* over-subscribed sets are bad */
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return -1;
    }
    short offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++)
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int k = 0; k < n; k++)
        if (lengths[k])
            h->symbol[offs[lengths[k]]++] = (short)k;
    return 0;
}

static int decode(inf_state *s, const huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= bits(s, 1);
        if (s->bad)
            return -1;
        int count = h->count[len];
        if (code - first < count)
            return h->symbol[index + code - first];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->bad = 1;
    return -1;
}

static const short len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                    6145, 8193, 12289, 16385, 24577};
static const short dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int inflate_codes(inf_state *s, const huff *lit, const huff *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 0)
            return -1;
        if (sym < 256) {
            if (s->out >= s->cap)
                return -1;
            s->dst[s->out++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29)
                return -1;
            int len = len_base[sym] + bits(s, len_extra[sym]);
            int ds = decode(s, dist);
            if (ds < 0 || ds >= 30)
                return -1;
            size_t d = (size_t)(dist_base[ds] + bits(s, dist_extra[ds]));
            if (s->bad || d > s->out || s->out + (size_t)len > s->cap)
                return -1;
            for (int k = 0; k < len; k++, s->out++)
                s->dst[s->out] = s->dst[s->out - d];
        }
    }
}

static int inflate_fixed(inf_state *s)
{
    static huff lit, dist;
    static int built;
    if (!built) {
        uint8_t l[288];
        for (int k = 0; k < 288; k++)
            l[k] = k < 144 ? 8 : k < 256 ? 9 : k < 280 ? 7 : 8;
        huff_build(&lit, l, 288);
        for (int k = 0; k < 30; k++)
            l[k] = 5;
        huff_build(&dist, l, 30);
        built = 1;
    }
    return inflate_codes(s, &lit, &dist);
}

static int inflate_dynamic(inf_state *s)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->bad || nlen > 286 || ndist > 30)
        return -1;
    uint8_t lengths[320] = {0};
    for (int k = 0; k < ncode; k++)
        lengths[order[k]] = (uint8_t)bits(s, 3);
    huff lencode, lit, dist;
    if (huff_build(&lencode, lengths, 19))
        return -1;
    memset(lengths, 0, sizeof lengths);
    for (int k = 0; k < nlen + ndist;) {
        int sym = decode(s, &lencode);
        if (sym < 0)
            return -1;
        if (sym < 16) {
            lengths[k++] = (uint8_t)sym;
            continue;
        }
        int rep, val = 0;
        if (sym == 16) {
            if (k == 0)
                return -1;
            val = lengths[k - 1];
            rep = 3 + bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + bits(s, 3);
        } else {
            rep = 11 + bits(s, 7);
        }
        if (s->bad || k + rep > nlen + ndist)
            return -1;
        while (rep--)
            lengths[k++] = (uint8_t)val;
    }
    if (lengths[256] == 0 || huff_build(&lit, lengths, nlen) || huff_build(&dist, lengths + nlen, ndist))
        return -1;
    return inflate_codes(s, &lit, &dist);
}

long pngio_inflate(const uint8_t *src, size_t n, uint8_t *dst, size_t cap)
{
    if (n < 2 || (src[0] & 0x0F) != 8 || ((src[0] << 8) | src[1]) % 31 || (src[1] & 0x20))
        return -1;
    inf_state s = {src, n, 2, 0, 0, dst, cap, 0, 0};
    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.bad)
            return -1;
        int rc;
        if (type == 0) {
            s.bitbuf = 0;
            s.bitcnt = 0;   /* to a byte boundary */
            if (s.pos + 4 > s.n)
                return -1;
            unsigned len = s.src[s.pos] | s.src[s.pos + 1] << 8;
            unsigned nlen = s.src[s.pos + 2] | s.src[s.pos + 3] << 8;
            s.pos += 4;
            if (len != (~nlen & 0xFFFF) || s.pos + len > s.n || s.out + len > s.cap)
                return -1;
            memcpy(s.dst + s.out, s.src + s.pos, len);
            s.pos += len;
            s.out += len;
            rc = 0;
        } else if (type == 1) {
            rc = inflate_fixed(&s);
        } else if (type == 2) {
            rc = inflate_dynamic(&s);
        } else {
            return -1;
        }
        if (rc || s.bad)
            return -1;
    } while (!last);
    return (long)s.out;
}

/* ---- reader ---- */

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

uint8_t *pngio_decode_rgba(const uint8_t *d, size_t n, int *pw, int *ph, const char **err)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    const char *why = "not a PNG";
    uint8_t *idat = NULL, *raw = NULL, *rgba = NULL;
    size_t idat_n = 0;
    uint8_t pal[256][4];
    int npal = 0, w = 0, h = 0, depth = 0, ctype = 0;
    int trns_gray = -1, trns_r = -1, trns_g = -1, trns_b = -1;
    for (int k = 0; k < 256; k++)
        pal[k][0] = pal[k][1] = pal[k][2] = 0, pal[k][3] = 255;
    if (n < 8 || memcmp(d, sig, 8))
        goto fail;
    size_t pos = 8;
    int have_ihdr = 0;
    while (pos + 12 <= n) {
        uint32_t len = get32(d + pos);
        const uint8_t *type = d + pos + 4, *data = d + pos + 8;
        if (len > n - pos - 12) {
            why = "truncated chunk";
            goto fail;
        }
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = (int)get32(data);
            h = (int)get32(data + 4);
            depth = data[8];
            ctype = data[9];
            if (data[12]) {
                why = "interlaced PNGs are not supported";
                goto fail;
            }
            have_ihdr = 1;
        } else if (!memcmp(type, "PLTE", 4)) {
            npal = (int)(len / 3 > 256 ? 256 : len / 3);
            for (int k = 0; k < npal; k++)
                pal[k][0] = data[k * 3], pal[k][1] = data[k * 3 + 1], pal[k][2] = data[k * 3 + 2];
        } else if (!memcmp(type, "tRNS", 4)) {
            if (ctype == 3)
                for (uint32_t k = 0; k < len && k < 256; k++)
                    pal[k][3] = data[k];
            else if (ctype == 0 && len >= 2)
                trns_gray = data[0] << 8 | data[1];
            else if (ctype == 2 && len >= 6)
                trns_r = data[0] << 8 | data[1], trns_g = data[2] << 8 | data[3],
                trns_b = data[4] << 8 | data[5];
        } else if (!memcmp(type, "IDAT", 4)) {
            uint8_t *p = realloc(idat, idat_n + len + 1);
            if (!p) {
                why = "out of memory";
                goto fail;
            }
            idat = p;
            memcpy(idat + idat_n, data, len);
            idat_n += len;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        pos += 12 + len;
    }
    if (!have_ihdr || w <= 0 || h <= 0 || w > 16384 || h > 16384 || !idat) {
        why = "missing or bad IHDR/IDAT";
        goto fail;
    }
    int channels = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!channels || depth == 16 || (depth != 8 && ctype != 0 && ctype != 3)) {
        why = "unsupported PNG format (16-bit or odd bit depth)";
        goto fail;
    }
    int bpp_bits = channels * depth;
    size_t stride = ((size_t)w * bpp_bits + 7) / 8, filt_bpp = (size_t)(bpp_bits + 7) / 8;
    size_t raw_n = (stride + 1) * (size_t)h;
    raw = malloc(raw_n);
    rgba = malloc((size_t)w * h * 4);
    if (!raw || !rgba) {
        why = "out of memory";
        goto fail;
    }
    if (pngio_inflate(idat, idat_n, raw, raw_n) != (long)raw_n) {
        why = "bad compressed data";
        goto fail;
    }
    /* Undo the filters in place (each row's data follows its filter byte). */
    for (int y = 0; y < h; y++) {
        uint8_t *r = raw + (size_t)y * (stride + 1) + 1;
        const uint8_t *up = y ? r - (stride + 1) : NULL;
        int f = r[-1];
        for (size_t x = 0; x < stride; x++) {
            int a = x >= filt_bpp ? r[x - filt_bpp] : 0, b = up ? up[x] : 0,
                c = up && x >= filt_bpp ? up[x - filt_bpp] : 0;
            int v = f == 0 ? 0 : f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : f == 4 ? paeth(a, b, c) : -1;
            if (v < 0) {
                why = "bad filter";
                goto fail;
            }
            r[x] = (uint8_t)(r[x] + v);
        }
    }
    for (int y = 0; y < h; y++) {
        const uint8_t *r = raw + (size_t)y * (stride + 1) + 1;
        uint8_t *o = rgba + (size_t)y * w * 4;
        for (int x = 0; x < w; x++, o += 4) {
            if (depth < 8) {   /* gray or palette, 1/2/4 bits */
                int per = 8 / depth, v = (r[x / per] >> ((per - 1 - x % per) * depth)) & ((1 << depth) - 1);
                if (ctype == 3) {
                    memcpy(o, pal[v], 4);
                } else {
                    int g = v * 255 / ((1 << depth) - 1);
                    o[0] = o[1] = o[2] = (uint8_t)g;
                    o[3] = v == trns_gray ? 0 : 255;
                }
                continue;
            }
            const uint8_t *s = r + (size_t)x * channels;
            switch (ctype) {
            case 0: o[0] = o[1] = o[2] = s[0]; o[3] = s[0] == trns_gray ? 0 : 255; break;
            case 2:
                o[0] = s[0], o[1] = s[1], o[2] = s[2];
                o[3] = s[0] == trns_r && s[1] == trns_g && s[2] == trns_b ? 0 : 255;
                break;
            case 3: memcpy(o, pal[s[0]], 4); break;
            case 4: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; break;
            default: memcpy(o, s, 4); break;
            }
        }
    }
    free(idat);
    free(raw);
    *pw = w;
    *ph = h;
    return rgba;
fail:
    free(idat);
    free(raw);
    free(rgba);
    if (err)
        *err = why;
    return NULL;
}

uint8_t *pngio_read_rgba(const char *path, int *w, int *h, const char **err)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err)
            *err = "cannot open";
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        fclose(f);
        if (err)
            *err = "cannot read";
        return NULL;
    }
    fclose(f);
    uint8_t *img = pngio_decode_rgba(d, (size_t)n, w, h, err);
    free(d);
    return img;
}
