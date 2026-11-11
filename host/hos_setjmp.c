/* (hookshot) setjmp / longjmp in lifted code.

   The guest's __setjmp and longjmp (sdk+0xaec40 / +0xaed50, musl's with pointer-auth hashes) run as lifted code and
   save and restore the guest registers themselves. What lifting breaks is longjmp's last instruction, BR x30 to the
   setjmp call's return address: in lifted code that address is the middle of a C function, not an entry, so the
   indirect jump trapped (OoT's UpdateTextBoxTextureThread, at 0x81681690). And the function that called setjmp may
   have returned by then: 0x81681670 is a wrapper that returns setjmp's result, which works on the console because
   its stack frame is still intact when longjmp comes back to it.

   So the lifter (a64_lift.py find_setjmp_owners) makes each setjmp return address an entry, and wraps every call of
   a function that calls setjmp (its "owner") as

       { void **sj = recomp_sj_push(c, r0..r3); if (recomp_sj_setjmp(sj) == 0) f_owner(c);
         else a64_icall(c, recomp_sj_target(sj)); recomp_sj_pop(sj); }

   in the caller's own frame, which is alive while the owner runs and after it returns. A jump that misses
   (a64_call_miss) to one of a pushed slot's return addresses is a longjmp: the guest registers are restored already
   (x0 the value, SP and x30 as setjmp saw them), so the newest such slot is taken (slots above it dropped), and
   recomp_sj_longjmp brings the host back to that caller, which runs the owner's code from the return address: what
   the owner would have done, ending in its own return to the caller.

   (recomp_sj_setjmp / recomp_sj_longjmp are this file's own, in assembly: every Win64 callee-saved register, xmm6-15
   with them, and the stack pointer and return address. clang's __builtin_setjmp, which this used first, saves the
   frame address, which on Win64 sits 128 bytes below the frame pointer clang addresses the frame's spill slots with,
   and its longjmp set rbp to it: the caller's reloads of c and the slot read garbage, and the text box thread
   called a stack address after its first longjmp.) */
#include "hos.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>
#include <string.h>

#define DEPTH 256
typedef struct { uint64_t rets[4], target; void *buf[32]; } SjSlot;   /* (buf: recomp_sj_setjmp's 248 bytes) */
typedef struct { SjSlot slot[DEPTH]; int depth, overflow; } SjStack;
static _Thread_local SjStack *stack;

#include "hos_setjmp_arch.h"

void **recomp_sj_push(Ctx *c, uint64_t r0, uint64_t r1, uint64_t r2, uint64_t r3) {
    (void)c;
    if (!stack) stack = (SjStack *)calloc(1, sizeof *stack);
    if (stack->depth >= DEPTH) {   /* (too deep: the outermost slots go, as they are the least likely targets) */
        memmove(&stack->slot[0], &stack->slot[1], sizeof stack->slot[0] * (DEPTH - 1));
        stack->depth = DEPTH - 1;
        if (!stack->overflow++) hos_log("setjmp: more than %d nested setjmp owners on this thread", DEPTH);
    }
    SjSlot *s = &stack->slot[stack->depth++];
    s->rets[0] = r0, s->rets[1] = r1, s->rets[2] = r2, s->rets[3] = r3, s->target = 0;
    return s->buf;
}

uint64_t recomp_sj_target(void **buf) { return ((SjSlot *)((char *)buf - offsetof(SjSlot, buf)))->target; }

void recomp_sj_pop(void **buf) {
    if (!stack) return;
    const int i = (int)(((SjSlot *)((char *)buf - offsetof(SjSlot, buf))) - stack->slot);
    if (i >= 0 && i < DEPTH && i < stack->depth) stack->depth = i;
}

/* a64_call_miss's first question: a longjmp to a pushed setjmp owner? (does not return when it is) */
void hos_setjmp_return(Ctx *c, uint64_t target) {
    if (!stack || !stack->depth || !target) return;
    for (int i = stack->depth - 1; i >= 0; i--) {
        SjSlot *s = &stack->slot[i];
        if (s->rets[0] != target && s->rets[1] != target && s->rets[2] != target && s->rets[3] != target) continue;
        static volatile LONG logged;
        if (InterlockedIncrement(&logged) <= 4)
            hos_log("longjmp to %#llx (value %llu, %d slots above dropped)", (unsigned long long)target,
                    (unsigned long long)c->x[0], stack->depth - 1 - i);
        s->target = target;
        stack->depth = i + 1;
        recomp_sj_longjmp(s->buf, 1);
    }
}

/* ...and every indirect jump's (third_party/a64-kit/rt a64_ijump_hook): the setjmp return addresses are entries too
   (the callers run them, recomp_sj_target), so the longjmp's BR finds a function there and never missed: it called
   the owner's code inside longjmp's own frame, which returned into longjmp's caller (0x8168215c, the UDF after OoT's
   BL longjmp: the text box thread's "branch to a non-function") */
void a64_ijump_hook(Ctx *c, uint64_t target) { hos_setjmp_return(c, target); }

