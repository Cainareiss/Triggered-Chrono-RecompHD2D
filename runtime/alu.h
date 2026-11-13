/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ADC and SBC, binary and decimal (D=1), shared by generated code (ops.h)
 * and the interpreter (interp.c), so both compute the same thing.
 *
 * Decimal mode follows bsnes/higan's WDC65816 algorithms (algorithmADC8/16,
 * algorithmSBC8/16) and fullsnes' description of the 65816 (not the NMOS
 * 6502): each nibble is added in binary and adjusted by 6 as the carry
 * ripples up; V comes from the sum before the top nibble's adjust; N, Z and
 * C from the final result. Digits above 9 aren't rejected: they go through
 * the same adjust, which is what the hardware (and bsnes) does with them.
 * SBC adds the operand's complement and adjusts down where no nibble carry
 * came out. The binary path is the plain sum, as before.
 *
 * Every helper takes the accumulator value and the operand, sets n v z c and
 * returns the result; the caller stores it (8-bit: B is kept). The decimal
 * test is a runtime branch on c->d (well predicted: D is clear almost
 * always), so code that sets D dynamically runs correctly in both paths. */
#ifndef CT_ALU_H
#define CT_ALU_H

#include <stdint.h>

#include "cpu.h"

static inline uint8_t alu_adc8(CPU *c, uint8_t a, uint8_t b)
{
    int r;
    if (!c->d) {
        r = a + b + c->c;
    } else {
        r = (a & 0x0F) + (b & 0x0F) + c->c;
        if (r > 0x09) r += 0x06;
        c->c = r > 0x0F;
        r = (a & 0xF0) + (b & 0xF0) + (c->c << 4) + (r & 0x0F);
    }
    c->v = (~(a ^ b) & (a ^ r) & 0x80) != 0;
    if (c->d && r > 0x9F) r += 0x60;
    c->c = r > 0xFF;
    c->z = (uint8_t)r == 0;
    c->n = (r & 0x80) != 0;
    return (uint8_t)r;
}

static inline uint8_t alu_sbc8(CPU *c, uint8_t a, uint8_t b)
{
    int r;
    b = (uint8_t)~b;
    if (!c->d) {
        r = a + b + c->c;
    } else {
        r = (a & 0x0F) + (b & 0x0F) + c->c;
        if (r <= 0x0F) r -= 0x06;
        c->c = r > 0x0F;
        r = (a & 0xF0) + (b & 0xF0) + (c->c << 4) + (r & 0x0F);
    }
    c->v = (~(a ^ b) & (a ^ r) & 0x80) != 0;
    if (c->d && r <= 0xFF) r -= 0x60;
    c->c = r > 0xFF;
    c->z = (uint8_t)r == 0;
    c->n = (r & 0x80) != 0;
    return (uint8_t)r;
}

static inline uint16_t alu_adc16(CPU *c, uint16_t a, uint16_t b)
{
    int r;
    if (!c->d) {
        r = a + b + c->c;
    } else {
        r = (a & 0x000F) + (b & 0x000F) + c->c;
        if (r > 0x0009) r += 0x0006;
        c->c = r > 0x000F;
        r = (a & 0x00F0) + (b & 0x00F0) + (c->c << 4) + (r & 0x000F);
        if (r > 0x009F) r += 0x0060;
        c->c = r > 0x00FF;
        r = (a & 0x0F00) + (b & 0x0F00) + (c->c << 8) + (r & 0x00FF);
        if (r > 0x09FF) r += 0x0600;
        c->c = r > 0x0FFF;
        r = (a & 0xF000) + (b & 0xF000) + (c->c << 12) + (r & 0x0FFF);
    }
    c->v = (~(a ^ b) & (a ^ r) & 0x8000) != 0;
    if (c->d && r > 0x9FFF) r += 0x6000;
    c->c = r > 0xFFFF;
    c->z = (uint16_t)r == 0;
    c->n = (r & 0x8000) != 0;
    return (uint16_t)r;
}

static inline uint16_t alu_sbc16(CPU *c, uint16_t a, uint16_t b)
{
    int r;
    b = (uint16_t)~b;
    if (!c->d) {
        r = a + b + c->c;
    } else {
        r = (a & 0x000F) + (b & 0x000F) + c->c;
        if (r <= 0x000F) r -= 0x0006;
        c->c = r > 0x000F;
        r = (a & 0x00F0) + (b & 0x00F0) + (c->c << 4) + (r & 0x000F);
        if (r <= 0x00FF) r -= 0x0060;
        c->c = r > 0x00FF;
        r = (a & 0x0F00) + (b & 0x0F00) + (c->c << 8) + (r & 0x00FF);
        if (r <= 0x0FFF) r -= 0x0600;
        c->c = r > 0x0FFF;
        r = (a & 0xF000) + (b & 0xF000) + (c->c << 12) + (r & 0x0FFF);
    }
    c->v = (~(a ^ b) & (a ^ r) & 0x8000) != 0;
    if (c->d && r <= 0xFFFF) r -= 0x6000;
    c->c = r > 0xFFFF;
    c->z = (uint16_t)r == 0;
    c->n = (r & 0x8000) != 0;
    return (uint16_t)r;
}

#endif
