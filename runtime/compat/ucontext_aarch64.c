/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * compat/ucontext.h for aarch64, from AAPCS64: a call preserves x19-x28,
 * x29 (frame pointer), sp and the low 64 bits of v8-v15 (d8-d15); x30
 * (lr) holds the return address. A switch saves exactly those, the
 * caller's x30 standing for its resume address, and resumes by loading
 * the other set and returning through its x30 with w0 = 0.
 *
 * regs[]: 0-9 x19-x28, 10 x29, 11 x30, 12 sp, 13-20 d8-d15.
 */
#if defined(CT_COMPAT_UCONTEXT) && defined(__aarch64__)

#include "ucontext.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Called by the entry trampoline when a context's function returns and it
   has no uc_link. Hidden: the assembly calls it directly, even in a
   shared object. */
__attribute__((visibility("hidden"), noreturn, used)) void ct_compat_uc_returned(void)
{
    fputs("ct: a makecontext coroutine returned with no uc_link\n", stderr);
    fflush(stderr);
    abort();
}

__attribute__((visibility("hidden"))) void ct_compat_uc_trampoline(void);

/* The save and load halves are shared: SAVE(xN) stores into the context
   at xN, LOAD(xN) restores from it and returns into it. */
#define CT_UC_SAVE(r) \
    "stp x19, x20, [" r ", #0]\n\t"   \
    "stp x21, x22, [" r ", #16]\n\t"  \
    "stp x23, x24, [" r ", #32]\n\t"  \
    "stp x25, x26, [" r ", #48]\n\t"  \
    "stp x27, x28, [" r ", #64]\n\t"  \
    "stp x29, x30, [" r ", #80]\n\t"  \
    "mov x9, sp\n\t"                  \
    "str x9, [" r ", #96]\n\t"        \
    "stp d8, d9, [" r ", #104]\n\t"   \
    "stp d10, d11, [" r ", #120]\n\t" \
    "stp d12, d13, [" r ", #136]\n\t" \
    "stp d14, d15, [" r ", #152]\n\t"
#define CT_UC_LOAD(r) \
    "ldp x19, x20, [" r ", #0]\n\t"   \
    "ldp x21, x22, [" r ", #16]\n\t"  \
    "ldp x23, x24, [" r ", #32]\n\t"  \
    "ldp x25, x26, [" r ", #48]\n\t"  \
    "ldp x27, x28, [" r ", #64]\n\t"  \
    "ldp x29, x30, [" r ", #80]\n\t"  \
    "ldr x9, [" r ", #96]\n\t"        \
    "mov sp, x9\n\t"                  \
    "ldp d8, d9, [" r ", #104]\n\t"   \
    "ldp d10, d11, [" r ", #120]\n\t" \
    "ldp d12, d13, [" r ", #136]\n\t" \
    "ldp d14, d15, [" r ", #152]\n\t" \
    "mov w0, #0\n\t"                  \
    "ret\n\t"

__asm__(
    ".text\n\t"
    ".p2align 4\n\t"
    ".globl ct_compat_getcontext\n\t"
    ".type ct_compat_getcontext, %function\n"
    "ct_compat_getcontext:\n\t"
    CT_UC_SAVE("x0")
    "mov w0, #0\n\t"
    "ret\n\t"
    ".size ct_compat_getcontext, .-ct_compat_getcontext\n\t"

    ".p2align 4\n\t"
    ".globl ct_compat_swapcontext\n\t"
    ".type ct_compat_swapcontext, %function\n"
    "ct_compat_swapcontext:\n\t"
    CT_UC_SAVE("x0")
    CT_UC_LOAD("x1")
    ".size ct_compat_swapcontext, .-ct_compat_swapcontext\n\t"

    ".p2align 4\n\t"
    ".globl ct_compat_setcontext\n\t"
    ".type ct_compat_setcontext, %function\n"
    "ct_compat_setcontext:\n"
    ".Lct_uc_set:\n\t"
    CT_UC_LOAD("x0")
    ".size ct_compat_setcontext, .-ct_compat_setcontext\n\t"

    /* A fresh context starts here (its x30), sp at the top of its stack,
       x19 its function and x20 its uc_link. */
    ".p2align 4\n\t"
    ".hidden ct_compat_uc_trampoline\n\t"
    ".type ct_compat_uc_trampoline, %function\n"
    "ct_compat_uc_trampoline:\n\t"
    "blr x19\n\t"
    "mov x0, x20\n\t"
    "cbnz x0, .Lct_uc_set\n\t"
    "bl ct_compat_uc_returned\n\t"
    "brk #0\n\t"
    ".size ct_compat_uc_trampoline, .-ct_compat_uc_trampoline\n\t");

void ct_compat_makecontext(ct_ucontext_t *ucp, void (*fn)(void), int argc, ...)
{
    if (argc != 0) {
        fprintf(stderr, "ct: makecontext with %d arguments (only 0 supported)\n", argc);
        abort();
    }
    uintptr_t top = (uintptr_t)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size;
    top &= ~(uintptr_t)15;   /* AAPCS64: sp 16-byte aligned */
    ucp->regs[0] = (uint64_t)(uintptr_t)fn;          /* x19 */
    ucp->regs[1] = (uint64_t)(uintptr_t)ucp->uc_link; /* x20 */
    ucp->regs[10] = 0;                                /* x29: end of the frame chain */
    ucp->regs[11] = (uint64_t)(uintptr_t)ct_compat_uc_trampoline; /* x30 */
    ucp->regs[12] = (uint64_t)top;                    /* sp */
}

#else
typedef int ct_compat_ucontext_other_arch;   /* not empty: ISO C */
#endif
