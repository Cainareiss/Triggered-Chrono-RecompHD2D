/* Host for ares's cycle-accurate SPC700 core (third_party/ares/spc700).
 *
 * The core calls read/write/idle once per SPC700 cycle; here each one
 * steps the vendored APU (third_party/snes: DSP, timers, cycle count) by a
 * cycle, and the core runs as a coroutine (ucontext) that yields whenever
 * it reaches the cycle it was asked to run to, mid-instruction if need be,
 * so the SPC700 is exactly at the CPU's time on every $2140-$2143 access.
 *
 * Access timing within a cycle, as Mesen 2 has it (the boot reference,
 * #33): every read and write lands at the end of its cycle, after the
 * DSP and timers have stepped. spc_host_cycle_end, if set, runs after
 * each cycle's access (the CPU-port latch in snes_adapter.c). */
#include <stdint.h>
#include <stdlib.h>
#include "compat/ucontext.h"   /* port: <ucontext.h>, or ours (bionic, musl) */

#include "nall_shim.hpp"

extern "C" {
#include "apu.h"
#include "savestate.h"   /* port: spc_host_state */
#include "spc700_host.h"
}

#include "spc700.hpp"

namespace ares {
/* spc700.cpp without the disassembler and serializer. */
#define PC r.pc.w
#define YA r.ya.w
#define A r.ya.byte.l
#define X r.x
#define Y r.ya.byte.h
#define S r.s
#define P r.p
#define CF r.p.c
#define ZF r.p.z
#define IF r.p.i
#define HF r.p.h
#define BF r.p.b
#define PF r.p.p
#define VF r.p.v
#define NF r.p.n
#define alu (this->*op)
#include "memory.cpp"
#include "algorithms.cpp"
#include "instructions.cpp"
#include "instruction.cpp"

auto SPC700::power() -> void {
    PC = 0x0000;
    YA = 0x0000;
    X = 0x00;
    S = 0xef;
    P = 0x02;
    r.wait = false;
    r.stop = false;
}
#undef PC
#undef YA
#undef A
#undef X
#undef Y
#undef S
#undef P
#undef CF
#undef ZF
#undef IF
#undef HF
#undef BF
#undef PF
#undef VF
#undef NF
#undef alu
}  // namespace ares

void (*spc_host_cycle_end)(void);
void (*spc_host_write_hook)(uint16_t address, uint8_t data);

namespace {

struct Host : ares::SPC700 {
    Apu *apu = nullptr;
    uint64_t cycle = 0;    /* cycles run (ticked) so far */
    uint64_t target = 0;   /* run cycles below this, then yield */
    ucontext_t core_ctx, caller_ctx;
    void *stack = nullptr;
    bool started = false;
    /* port: save states: no cycle of the current instruction has run yet
       (it is suspended before its opcode fetch) */
    bool boundary = true;
    uint16_t insn_pc = 0;   /* PC at that instruction's start (fetch has moved it) */
    bool entered = false;   /* the coroutine has run (it is suspended in a fetch) */

    void yield() { swapcontext(&core_ctx, &caller_ctx); }
    void begin() {
        while (cycle >= target)
            yield();
    }
    void tick() {
        apu_tick(apu);
        cycle++;
        boundary = false;
    }
    void end() {
        if (spc_host_cycle_end)
            spc_host_cycle_end();
    }

    auto idle() -> void override {
        begin();
        tick();
        end();
    }
    auto read(ares::n16 address) -> ares::n8 override {
        begin();
        tick();
        uint8_t v = apu_cpuRead(apu, address);
        end();
        return v;
    }
    auto write(ares::n16 address, ares::n8 data) -> void override {
        begin();
        tick();
        apu_cpuWrite(apu, address, data);
        if (spc_host_write_hook)
            spc_host_write_hook(address, data);
        end();
    }
    auto synchronizing() const -> bool override { return false; }
};

Host host;

void core_main()
{
    host.entered = true;   /* port: save states */
    for (;;) {
        host.boundary = true;   /* port: save states */
        host.insn_pc = (uint16_t)host.r.pc.w;
        host.instruction();
    }
}

/* port: a new core coroutine, suspended before its next instruction (a
   reset, or a save state's load). */
void core_start()
{
    enum { STACK = 256 * 1024 };
    if (!host.stack)
        host.stack = malloc(STACK);
    getcontext(&host.core_ctx);
    host.core_ctx.uc_stack.ss_sp = host.stack;
    host.core_ctx.uc_stack.ss_size = STACK;
    host.core_ctx.uc_link = nullptr;
    makecontext(&host.core_ctx, core_main, 0);
    host.started = true;
    host.boundary = true;
    host.entered = false;
    host.insn_pc = (uint16_t)host.r.pc.w;
}

}  // namespace

extern "C" void spc_host_reset(Apu *apu)
{
    host.apu = apu;
    host.cycle = host.target = 0;
    host.power();
    /* PC from the IPL ROM's reset vector, read through two bus cycles
       (Mesen 2, the boot reference, #33; bsnes/ares load it with none):
       the first instruction starts at cycle 2. */
    host.r.pc.w = (uint32_t)(apu_cpuRead(apu, 0xFFFE) | apu_cpuRead(apu, 0xFFFF) << 8);
    host.tick();
    host.tick();
    core_start();
}

extern "C" void spc_host_run(uint32_t cycles)
{
    if (!host.started)
        return;
    host.target = host.cycle + cycles;
    swapcontext(&host.caller_ctx, &host.core_ctx);
}

extern "C" uint64_t spc_host_cycle(void) { return host.cycle; }

extern "C" void spc_host_regs(uint8_t out[8])
{
    out[0] = host.r.ya.byte.l;
    out[1] = host.r.x;
    out[2] = host.r.ya.byte.h;
    out[3] = host.r.s;
    out[4] = (uint8_t)host.r.pc.w;
    out[5] = (uint8_t)(host.r.pc.w >> 8);
    out[6] = (uint8_t)(uint32_t)host.r.p;
    out[7] = (uint8_t)(host.r.wait | host.r.stop << 1);
}

/* port: save states (savestate.h). Only at an instruction boundary
   (spc_host_at_boundary): the core's registers, its cycle count and run
   target; a load starts a new core coroutine there. The APU it drives
   (RAM, DSP, timers) is the adapter's (snes_hw_state). A core in SLEEP or
   STOP never reaches a boundary again; the game this was written for
   never uses either. */
extern "C" int spc_host_at_boundary(void) { return host.boundary; }

extern "C" void spc_host_state(ss_io *s)
{
    /* At a boundary the core is suspended in its opcode fetch, whose PC++
       has already happened: the instruction's own PC is saved. */
    uint8_t r[10] = {(uint8_t)host.insn_pc, (uint8_t)(host.insn_pc >> 8),
                    (uint8_t)host.r.ya.byte.l, (uint8_t)host.r.ya.byte.h, (uint8_t)host.r.x,
                    (uint8_t)host.r.s, (uint8_t)(uint32_t)host.r.p, (uint8_t)host.r.wait,
                    (uint8_t)host.r.stop, (uint8_t)host.entered};
    uint64_t cycle = host.cycle, target = host.target;
    ss_begin(s, "SPC7");
    ss_bytes(s, r, sizeof r);
    SS(s, cycle);
    SS(s, target);
    ss_end(s);
    if (!SS_APPLY(s))
        return;
    host.r.pc.w = (uint16_t)(r[0] | r[1] << 8);
    host.r.ya.byte.l = r[2];
    host.r.ya.byte.h = r[3];
    host.r.x = r[4];
    host.r.s = r[5];
    host.r.p = r[6];
    host.r.wait = r[7] != 0;
    host.r.stop = r[8] != 0;
    host.cycle = cycle;
    core_start();
    if (r[9]) {
        /* Suspended in its opcode fetch, as the saved core was (its PC
           already past the opcode): run it to there, no cycle further. */
        host.target = cycle;
        swapcontext(&host.caller_ctx, &host.core_ctx);
    }
    host.target = target;
}
