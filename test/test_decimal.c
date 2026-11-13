/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Decimal mode (D=1) ADC and SBC, 8- and 16-bit, in both paths: the helpers
 * generated code calls (runtime/ops.h) and the interpreter (one ADC/SBC #imm
 * instruction run from WRAM by interp_step).
 *
 * Where the vectors come from:
 * - the table: worked by hand from bsnes' WDC65816 algorithms
 *   (processor/wdc65816/algorithms.cpp, algorithmADC8/16, algorithmSBC8/16),
 *   which fullsnes describes the same way; the valid-BCD rows are plain
 *   decimal arithmetic, the V rows include the classic 6502/65C02 decimal
 *   cases (e.g. $79 + $00 + C = $80 with V set), and the invalid-digit rows
 *   ($0F, $FF, $9A ...) are what bsnes' per-nibble adjust gives;
 * - exhaustively over valid BCD operands and carry: the result and C must
 *   be the decimal sum / difference (mod 100, 10000), Z and N from it;
 * - every 8-bit input pair, both carries: generated helpers and interpreter
 *   agree on result and flags (so the two paths share invalid-digit
 *   behavior too), and A's high byte is kept. */
#include <setjmp.h>

#include "harness.h"

typedef struct {
    int sbc, wide;
    uint16_t a, b;
    int cin;
    uint16_t r;
    int c, v, z, n;
} vec;

static const vec vecs[] = {
    /* 8-bit ADC: valid BCD, carry in and out */
    {0, 0, 0x12, 0x34, 0, 0x46, 0, 0, 0, 0},
    {0, 0, 0x12, 0x34, 1, 0x47, 0, 0, 0, 0},
    {0, 0, 0x09, 0x01, 0, 0x10, 0, 0, 0, 0},   /* low digit carries */
    {0, 0, 0x99, 0x01, 0, 0x00, 1, 0, 1, 0},   /* carry out, zero */
    {0, 0, 0x99, 0x00, 1, 0x00, 1, 0, 1, 0},
    {0, 0, 0x99, 0x99, 1, 0x99, 1, 1, 0, 1},   /* V: $139 binary-like */
    {0, 0, 0x50, 0x50, 0, 0x00, 1, 1, 1, 0},   /* V: $50+$50 = $A0 binary-like */
    {0, 0, 0x58, 0x46, 1, 0x05, 1, 1, 0, 0},   /* V from $A5 before the top adjust */
    {0, 0, 0x79, 0x00, 1, 0x80, 0, 1, 0, 1},   /* V and N, no carry */
    {0, 0, 0x80, 0x80, 0, 0x60, 1, 1, 0, 0},   /* V: two negatives -> positive */
    {0, 0, 0x81, 0x92, 0, 0x73, 1, 1, 0, 0},
    {0, 0, 0x00, 0x00, 0, 0x00, 0, 0, 1, 0},
    /* 8-bit ADC: invalid digits, per bsnes */
    {0, 0, 0x0F, 0x01, 0, 0x16, 0, 0, 0, 0},
    {0, 0, 0x0A, 0x00, 0, 0x10, 0, 0, 0, 0},
    {0, 0, 0xFF, 0xFF, 1, 0x55, 1, 0, 0, 0},
    {0, 0, 0x9A, 0x00, 0, 0x00, 1, 0, 1, 0},
    {0, 0, 0xA0, 0x00, 0, 0x00, 1, 0, 1, 0},
    /* 8-bit SBC: valid BCD, borrow in and out (C=1: no borrow) */
    {1, 0, 0x46, 0x12, 1, 0x34, 1, 0, 0, 0},
    {1, 0, 0x46, 0x12, 0, 0x33, 1, 0, 0, 0},
    {1, 0, 0x40, 0x13, 1, 0x27, 1, 0, 0, 0},   /* low digit borrows */
    {1, 0, 0x00, 0x01, 1, 0x99, 0, 0, 0, 1},   /* borrow out */
    {1, 0, 0x00, 0x00, 0, 0x99, 0, 0, 0, 1},
    {1, 0, 0x12, 0x12, 1, 0x00, 1, 0, 1, 0},
    {1, 0, 0x80, 0x01, 1, 0x79, 1, 1, 0, 0},   /* V: negative - positive -> positive */
    {1, 0, 0x20, 0x90, 1, 0x30, 0, 1, 0, 0},   /* V: positive - negative -> negative-like */
    /* 8-bit SBC: invalid digits, per bsnes */
    {1, 0, 0x0F, 0x00, 1, 0x0F, 1, 0, 0, 0},
    {1, 0, 0x00, 0x0F, 1, 0x9B, 0, 0, 0, 1},
    {1, 0, 0xFF, 0xFF, 1, 0x00, 1, 0, 1, 0},
    {1, 0, 0x1A, 0x0B, 1, 0x09, 1, 0, 0, 0},
    /* 16-bit ADC */
    {0, 1, 0x1234, 0x5678, 0, 0x6912, 0, 0, 0, 0},
    {0, 1, 0x1234, 0x5678, 1, 0x6913, 0, 0, 0, 0},
    {0, 1, 0x0999, 0x0001, 0, 0x1000, 0, 0, 0, 0},  /* carry ripples through 3 digits */
    {0, 1, 0x9999, 0x0001, 0, 0x0000, 1, 0, 1, 0},
    {0, 1, 0x9999, 0x9999, 1, 0x9999, 1, 1, 0, 1},
    {0, 1, 0x7999, 0x0000, 1, 0x8000, 0, 1, 0, 1},  /* V */
    {0, 1, 0x5000, 0x5000, 0, 0x0000, 1, 1, 1, 0},
    {0, 1, 0x00FF, 0x0001, 0, 0x0166, 0, 0, 0, 0},  /* invalid digits */
    {0, 1, 0xFFFF, 0xFFFF, 1, 0x5555, 1, 0, 0, 0},
    /* 16-bit SBC */
    {1, 1, 0x5678, 0x1234, 1, 0x4444, 1, 0, 0, 0},
    {1, 1, 0x1000, 0x0001, 1, 0x0999, 1, 0, 0, 0},  /* borrow ripples */
    {1, 1, 0x1000, 0x0001, 0, 0x0998, 1, 0, 0, 0},
    {1, 1, 0x0000, 0x0001, 1, 0x9999, 0, 0, 0, 1},
    {1, 1, 0x8000, 0x0001, 1, 0x7999, 1, 1, 0, 0},  /* V */
    {1, 1, 0x0000, 0x000F, 1, 0x999B, 0, 0, 0, 1},  /* invalid digits */
    {1, 1, 0xFFFF, 0xFFFF, 1, 0x0000, 1, 0, 1, 0},
};

typedef struct {
    uint16_t r;
    int c, v, z, n;
} out;

/* Generated code's path (ops.h). */
static out helper(int sbc, int wide, uint16_t a, uint16_t b, int cin, uint16_t hi)
{
    CPU c;
    cpu_init(&c);
    c.d = 1;
    c.c = (uint8_t)cin;
    c.m = (uint8_t)!wide;
    c.A = wide ? a : (uint16_t)(hi | a);
    if (wide) {
        if (sbc) sbc16(&c, b, 0x7E2000);
        else     adc16(&c, b, 0x7E2000);
    } else {
        if (sbc) sbc8(&c, (uint8_t)b, 0x7E2000);
        else     adc8(&c, (uint8_t)b, 0x7E2000);
        CHECK((c.A & 0xFF00) == hi, "helper: B changed to %04X", c.A);
    }
    return (out){wide ? c.A : (uint16_t)(c.A & 0xFF), c.c, c.v, c.z, c.n};
}

/* The interpreter's path: ADC/SBC #b at $7E2000, one interp_step. */
static out interp(int sbc, int wide, uint16_t a, uint16_t b, int cin, uint16_t hi)
{
    uint8_t *w = bus_wram();
    w[0x2000] = sbc ? 0xE9 : 0x69;
    w[0x2001] = (uint8_t)b;
    w[0x2002] = (uint8_t)(b >> 8);
    CPU c;
    cpu_init(&c);
    c.PB = 0x7E;
    c.DB = 0x7E;
    c.PC = 0x2000;
    c.S = 0x01F0;
    c.d = 1;
    c.c = (uint8_t)cin;
    c.m = (uint8_t)!wide;
    c.A = wide ? a : (uint16_t)(hi | a);
    interp_step(&c);
    CHECK(c.PC == (wide ? 0x2003 : 0x2002), "interp: PC %04X", c.PC);
    CHECK(c.d == 1, "interp: D cleared");
    if (!wide)
        CHECK((c.A & 0xFF00) == hi, "interp: B changed to %04X", c.A);
    return (out){wide ? c.A : (uint16_t)(c.A & 0xFF), c.c, c.v, c.z, c.n};
}

static int same(out x, out y)
{
    return x.r == y.r && x.c == y.c && x.v == y.v && x.z == y.z && x.n == y.n;
}

static void test_vectors(void)
{
    for (unsigned k = 0; k < sizeof vecs / sizeof vecs[0]; k++) {
        const vec *t = &vecs[k];
        out want = {t->r, t->c, t->v, t->z, t->n};
        out h = helper(t->sbc, t->wide, t->a, t->b, t->cin, 0xAB00);
        out i = interp(t->sbc, t->wide, t->a, t->b, t->cin, 0xAB00);
        CHECK(same(h, want), "helper row %u: %s%d %04X,%04X,c%d -> %04X c%d v%d z%d n%d",
              k, t->sbc ? "sbc" : "adc", t->wide ? 16 : 8, t->a, t->b, t->cin,
              h.r, h.c, h.v, h.z, h.n);
        CHECK(same(i, want), "interp row %u: %s%d %04X,%04X,c%d -> %04X c%d v%d z%d n%d",
              k, t->sbc ? "sbc" : "adc", t->wide ? 16 : 8, t->a, t->b, t->cin,
              i.r, i.c, i.v, i.z, i.n);
    }
}

static unsigned to_bcd(unsigned v)
{
    return (v % 10) | (v / 10 % 10) << 4 | (v / 100 % 10) << 8 | (v / 1000 % 10) << 12;
}

static void check_arith(int sbc, int wide, unsigned x, unsigned y, int cin)
{
    unsigned mod = wide ? 10000 : 100;
    int sum = sbc ? (int)x - (int)y - !cin : (int)(x + y) + cin;
    int cout = sbc ? sum >= 0 : sum >= (int)mod;
    unsigned r = to_bcd((unsigned)((sum + (int)mod) % (int)mod));
    unsigned top = wide ? 0x8000 : 0x80;
    out want = {(uint16_t)r, cout, 0, r == 0, (r & top) != 0};
    uint16_t a = (uint16_t)to_bcd(x), b = (uint16_t)to_bcd(y);
    out h = helper(sbc, wide, a, b, cin, 0x5A00);
    out i = interp(sbc, wide, a, b, cin, 0x5A00);
    h.v = i.v = 0;   /* V isn't a decimal quantity; the table covers it */
    CHECK(same(h, want), "helper %s%d %u,%u,c%d -> %04X c%d", sbc ? "sbc" : "adc",
          wide ? 16 : 8, x, y, cin, h.r, h.c);
    CHECK(same(i, want), "interp %s%d %u,%u,c%d -> %04X c%d", sbc ? "sbc" : "adc",
          wide ? 16 : 8, x, y, cin, i.r, i.c);
}

static void test_valid_bcd(void)
{
    for (int sbc = 0; sbc < 2; sbc++)
        for (int cin = 0; cin < 2; cin++) {
            for (unsigned x = 0; x < 100; x++)
                for (unsigned y = 0; y < 100; y++)
                    check_arith(sbc, 0, x, y, cin);
            for (int k = 0; k < 20000; k++)
                check_arith(sbc, 1, rnd32() % 10000, rnd32() % 10000, cin);
        }
}

static void test_paths_agree(void)
{
    for (int sbc = 0; sbc < 2; sbc++)
        for (int cin = 0; cin < 2; cin++)
            for (unsigned a = 0; a < 256; a++)
                for (unsigned b = 0; b < 256; b++) {
                    out h = helper(sbc, 0, (uint16_t)a, (uint16_t)b, cin, 0x3C00);
                    out i = interp(sbc, 0, (uint16_t)a, (uint16_t)b, cin, 0x3C00);
                    CHECK(same(h, i), "%s8 %02X,%02X,c%d: helper %02X c%d v%d, interp %02X c%d v%d",
                          sbc ? "sbc" : "adc", a, b, cin, h.r, h.c, h.v, i.r, i.c, i.v);
                }
    for (int sbc = 0; sbc < 2; sbc++)
        for (int k = 0; k < 200000; k++) {
            uint16_t a = (uint16_t)rnd32(), b = (uint16_t)rnd32();
            int cin = rnd32() & 1;
            out h = helper(sbc, 1, a, b, cin, 0);
            out i = interp(sbc, 1, a, b, cin, 0);
            CHECK(same(h, i), "%s16 %04X,%04X,c%d: helper %04X, interp %04X",
                  sbc ? "sbc" : "adc", a, b, cin, h.r, i.r);
        }
}

/* D=0 stays plain binary (the helpers' other branch). */
static void test_binary_unchanged(void)
{
    for (int k = 0; k < 100000; k++) {
        CPU c;
        cpu_init(&c);
        c.m = 0;
        c.A = (uint16_t)rnd32();
        uint16_t a = c.A, b = (uint16_t)rnd32();
        c.c = rnd32() & 1;
        unsigned cin = c.c;
        adc16(&c, b, 0);
        unsigned r = a + b + cin;
        CHECK(c.A == (uint16_t)r && c.c == (r > 0xFFFF) &&
              c.v == (((~(a ^ b) & (a ^ r)) >> 15) & 1), "binary adc16 %04X+%04X", a, b);
        c.A = a;
        c.c = (uint8_t)cin;
        sbc16(&c, b, 0);
        r = a + (uint16_t)~b + cin;
        CHECK(c.A == (uint16_t)r && c.c == (r > 0xFFFF), "binary sbc16 %04X-%04X", a, b);
    }
}

int main(void)
{
    th_bus_init();
    test_vectors();
    test_valid_bcd();
    test_paths_agree();
    test_binary_unchanged();
    return th_report("decimal");
}
