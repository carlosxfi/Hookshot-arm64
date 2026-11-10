/* Linux signal-frame helpers. The ARM64 ESR decoder is adapted from TotK Recomp's platform/linux.c.
   Copyright (c) 2026 Valiantsin Dzerakh. MIT license: ../ARM64_PROVENANCE.md. */
#ifndef HOOKSHOT_SIGNAL_CONTEXT_H
#define HOOKSHOT_SIGNAL_CONTEXT_H
#include <stdint.h>
#include <string.h>
#include <ucontext.h>

static inline uintptr_t hos_signal_pc(const ucontext_t *u) {
#if defined(__aarch64__)
    return (uintptr_t)u->uc_mcontext.pc;
#elif defined(__x86_64__)
    return (uintptr_t)u->uc_mcontext.gregs[REG_RIP];
#else
#error "Hookshot Linux signals require AArch64 or x86-64"
#endif
}

/* Windows access-violation codes: 0 read, 1 write, 8 execute. Without an ESR, use read. */
static inline unsigned hos_signal_access(const ucontext_t *u) {
#if defined(__aarch64__)
    const unsigned char *p = (const void *)u->uc_mcontext.__reserved;
    const unsigned char *end = p + sizeof u->uc_mcontext.__reserved;
    while ((size_t)(end - p) >= 8) {
        uint32_t magic, size;
        memcpy(&magic, p, 4);
        memcpy(&size, p + 4, 4);
        if (!magic || size < 8 || size > (size_t)(end - p)) break;
        if (magic == 0x45535201u && size >= 16) {  /* ESR_MAGIC */
            uint64_t esr;
            memcpy(&esr, p + 8, 8);
            unsigned ec = (unsigned)(esr >> 26) & 0x3f;
            if (ec == 0x20 || ec == 0x21) return 8;
            if ((ec == 0x24 || ec == 0x25) && (esr & (1u << 6)) && !(esr & (1u << 8))) return 1;
            return 0;
        }
        p += size;
    }
    return 0;
#else
    const unsigned long err = (unsigned long)u->uc_mcontext.gregs[REG_ERR];
    return (err & 16) ? 8 : (err & 2) ? 1 : 0;
#endif
}
#endif
