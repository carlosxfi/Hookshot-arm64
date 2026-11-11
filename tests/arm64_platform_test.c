/* Game-free checks of nonlocal jumps and Linux exception translation, on x86-64 and ARM64. */
#define _GNU_SOURCE
#include <assert.h>
#include <fenv.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <windows.h>
#include "hos_setjmp_arch.h"
#include "signal_context.h"

static void *outer[32], *inner[32];
static volatile int entered, returned;
__attribute__((noinline)) static void nested(int depth, void **slot, int value) {
    volatile unsigned char frame[97];
    memset((void *)frame, depth, sizeof frame);
    if (depth) nested(depth - 1, slot, value);
    else recomp_sj_longjmp(slot, value);
    assert(frame[0] == 255); /* never reached */
}

static void jumps(void) {
    memset(outer, 0xa5, sizeof outer);
    assert(fesetround(FE_DOWNWARD) == 0);
    int value = recomp_sj_setjmp(outer);
    if (!value) {
        entered++;
        int second = recomp_sj_setjmp(inner);
        if (!second) nested(12, inner, 17);
        assert(second == 17);
        returned++;
        assert(fesetround(FE_UPWARD) == 0);
        nested(12, outer, 0);
    }
    assert(value == 1 && entered == 1 && returned == 1);
    assert(fegetround() == FE_DOWNWARD);
    /* The largest frame (Win64) is 248 bytes; none may overwrite the last slot. */
    assert((uintptr_t)outer[31] == UINT64_C(0xa5a5a5a5a5a5a5a5));
    assert(fesetround(FE_TONEAREST) == 0);
}

static void signal_frames(void) {
    ucontext_t u;
    memset(&u, 0, sizeof u);
#if defined(__aarch64__)
    u.uc_mcontext.pc = 0x12345678;
    unsigned char *p = (void *)u.uc_mcontext.__reserved;
    uint32_t magic = 0x45535201u, size = 16;
    uint64_t esr = (UINT64_C(0x24) << 26) | (1u << 6);
    memcpy(p, &magic, 4); memcpy(p + 4, &size, 4); memcpy(p + 8, &esr, 8);
    assert(hos_signal_access(&u) == 1);
    esr |= 1u << 8; /* cache maintenance is not a write */
    memcpy(p + 8, &esr, 8);
    assert(hos_signal_access(&u) == 0);
    esr = UINT64_C(0x20) << 26;
    memcpy(p + 8, &esr, 8);
    assert(hos_signal_access(&u) == 8);
    size = UINT32_MAX; memcpy(p + 4, &size, 4);
    assert(hos_signal_access(&u) == 0); /* reject an out-of-bounds record */
#else
    u.uc_mcontext.gregs[REG_RIP] = 0x12345678;
    u.uc_mcontext.gregs[REG_ERR] = 2;
    assert(hos_signal_access(&u) == 1);
    u.uc_mcontext.gregs[REG_ERR] = 16;
    assert(hos_signal_access(&u) == 8);
#endif
    assert(hos_signal_pc(&u) == 0x12345678);
}

static unsigned char *page;
static size_t pagesize;
static volatile int faults;
static unsigned access_types[3];
static LONG fault(EXCEPTION_POINTERS *ep) {
    EXCEPTION_RECORD *r = ep->ExceptionRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters != 2 ||
        r->ExceptionInformation[1] < (uintptr_t)page ||
        r->ExceptionInformation[1] >= (uintptr_t)page + pagesize || faults >= 3)
        return EXCEPTION_CONTINUE_SEARCH;
    assert(r->ExceptionAddress == (void *)(uintptr_t)ep->ContextRecord->Rip);
    access_types[faults++] = (unsigned)r->ExceptionInformation[0];
    assert(mprotect(page, pagesize, PROT_READ | PROT_WRITE | PROT_EXEC) == 0);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void real_faults(void) {
    pagesize = (size_t)sysconf(_SC_PAGESIZE);
    page = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    void *handler = AddVectoredExceptionHandler(1, fault);
    assert(handler);
    assert(mprotect(page, pagesize, PROT_NONE) == 0);
    volatile unsigned char got = *(volatile unsigned char *)page;
    assert(got == 0 && faults == 1 && access_types[0] == 0);
    assert(mprotect(page, pagesize, PROT_READ) == 0);
    *(volatile unsigned char *)page = 42;
    assert(faults == 2 && access_types[1] == 1 && page[0] == 42);
#if defined(__aarch64__)
    const uint32_t ret = 0xd65f03c0u;
    memcpy(page, &ret, sizeof ret);
#else
    page[0] = 0xc3;
#endif
    __builtin___clear_cache((char *)page, (char *)page + 16);
    assert(mprotect(page, pagesize, PROT_READ) == 0);
    ((void (*)(void))page)();
    assert(faults == 3 && access_types[2] == 8);
    assert(RemoveVectoredExceptionHandler(handler));
    assert(munmap(page, pagesize) == 0);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
#if defined(__aarch64__)
    assert(si.wProcessorArchitecture == 12);
#else
    assert(si.wProcessorArchitecture == 9);
#endif
}

int main(void) {
    jumps();
    signal_frames();
    real_faults();
    puts("platform ABI, FP state, and read/write/execute faults passed");
}
