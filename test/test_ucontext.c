/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The context calls the runtime's coroutines use (runtime/compat/
 * ucontext.h): ours with CT_COMPAT_UCONTEXT, else the C library's.
 * Ping-pong between a coroutine and its caller with callee-saved integer
 * and floating-point values live across every switch, a 16-byte aligned
 * fresh stack on an unaligned buffer, getcontext resumed a second time,
 * and uc_link. "returns" (compat only): a coroutine that returns with no
 * uc_link aborts with a message.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compat/ucontext.h"

static int fails;
#define CHECK(c, ...)                         \
    do {                                      \
        if (!(c)) {                           \
            fails++;                          \
            fprintf(stderr, "FAIL: " __VA_ARGS__); \
            fputc('\n', stderr);              \
        }                                     \
    } while (0)

enum { STACK = 256 * 1024 };
static ucontext_t main_ctx, co_ctx, link_ctx;
static volatile long co_steps;
static volatile uintptr_t co_sp, co_fp;

/* Values the compiler keeps in callee-saved registers across the switch
   (the noinline call forces them out of caller-saved ones). */
static __attribute__((noinline)) double mix(double d, long i) { return d * 1.0000001 + (double)i; }

static void co_main(void)
{
    char here;
    co_sp = (uintptr_t)&here;
    /* 16-byte aligned when it was entered as after a call: aarch64's sp
       always is; x86_64's frame pointer (rsp + 8 at entry, pushed). */
#if defined(__aarch64__)
    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    co_fp = sp;
#else
    co_fp = (uintptr_t)__builtin_frame_address(0);
#endif
    double acc = 1.5;
    long n = 0;
    for (;;) {
        acc = mix(acc, n);
        n++;
        co_steps = n;
        swapcontext(&co_ctx, &main_ctx);
        CHECK(co_steps == n, "coroutine: step %ld came back as %ld", n, (long)co_steps);
        if (n == 1000) {
            CHECK(acc > 0.0, "coroutine: lost its accumulator");
            return;   /* to uc_link */
        }
    }
}

static void returns_main(void) {}

/* The abort, as a plain exit: ctest's regex check fails any test killed
   by a signal, and no core file is left behind. */
static void on_abort(int sig)
{
    (void)sig;
    _exit(3);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "returns")) {
        static char st[STACK];
        signal(SIGABRT, on_abort);
        getcontext(&co_ctx);
        co_ctx.uc_stack.ss_sp = st;
        co_ctx.uc_stack.ss_size = sizeof st;
        co_ctx.uc_link = NULL;
        makecontext(&co_ctx, returns_main, 0);
        swapcontext(&main_ctx, &co_ctx);
        fprintf(stderr, "FAIL: came back from a returned coroutine\n");
        return 1;
    }

    /* getcontext, resumed once more through setcontext. */
    volatile int passes = 0;
    getcontext(&main_ctx);
    if (++passes == 1)
        setcontext(&main_ctx);
    CHECK(passes == 2, "getcontext resumed: %d passes", passes);

    /* An odd-sized stack: the coroutine still gets an aligned one. */
    char *stack = malloc(STACK + 8);
    getcontext(&co_ctx);
    co_ctx.uc_stack.ss_sp = stack + 3;
    co_ctx.uc_stack.ss_size = STACK + 1;
    co_ctx.uc_link = &link_ctx;
    makecontext(&co_ctx, co_main, 0);

    volatile int linked = 0;
    getcontext(&link_ctx);
    if (!linked) {
        linked = 1;
        double mine = 2.25;
        long rounds = 0;
        while (rounds < 1000) {
            mine = mix(mine, -rounds);
            swapcontext(&main_ctx, &co_ctx);
            rounds++;
            CHECK(co_steps == rounds, "caller: step %ld, coroutine at %ld", rounds, (long)co_steps);
        }
        /* The coroutine's last swap came back here; once more and it
           returns, which resumes link_ctx (just above). */
        CHECK(mine != 2.25, "caller: lost its value");
        swapcontext(&main_ctx, &co_ctx);
        CHECK(0, "the coroutine's return didn't go to uc_link");
    }
    CHECK(co_steps == 1000, "coroutine ran %ld steps", (long)co_steps);
    CHECK(co_sp > (uintptr_t)stack && co_sp < (uintptr_t)stack + STACK + 8,
          "coroutine ran off its stack");
    CHECK(co_fp % 16 == 0, "coroutine stack at %#lx: not 16-byte aligned", (unsigned long)co_fp);
    free(stack);

    if (fails) {
        fprintf(stderr, "%d failures\n", fails);
        return 1;
    }
    printf("ucontext: ok\n");
    return 0;
}
