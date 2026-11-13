/* (oot-remake-multiplayer's patch 0002, nx2/coop_puppet_guard.py) Null-pointer reads give 0 once a co-op puppet exists.

   On the emulator a guest read of an unmapped page gives 0, and the game's code for a co-op puppet (a stripped copy of
   Link, CoopPuppet_*) relies on that without knowing it: it follows pointers of components the puppet does not have,
   reads 0 and takes its own "nothing there" branch. Hookshot's guest window leaves the first MB unmapped on purpose
   (mkw_glue.c commit_on_touch: a null pointer is the game's own fault), so each such read crashed the game ("guest 0x20",
   "guest 0x7d8").

   hos_coop_null_page() is called the first time nx2/coop_puppet_guard.py meets a puppet (so never in a game without
   co-op puppets). It commits the guest's first MB read-only with PAGE_GUARD: a read there gives 0, as on the emulator,
   and a write still faults and is reported as a crash. Only guest memory, only that MB. The guard bit makes each page's
   first read since the last minute raise STATUS_GUARD_PAGE_VIOLATION once: the handler counts it, logs the first 32 (host
   code as hookshot.exe+offset, guest address), and lets the read go on. Once a minute a thread logs the minute's count and
   puts the guard bits back, so null reads the game makes stay visible in the log.
   HOS_COOP_NULL_READS=0: left as shipped (the puppet's update then crashes).
   Linux: the whole guest window is mapped readable already (hos_main.c), so nothing changes there. */
#include "hos.h"
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum { COOP_NULL_SIZE = 0x100000, COOP_NULL_SAID = 32 };
static volatile LONG coop_null_hits, coop_null_minute;

static LONG CALLBACK coop_null_read(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *rec = info->ExceptionRecord;
    if (rec->ExceptionCode != STATUS_GUARD_PAGE_VIOLATION || rec->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    const uint8_t *p = (const uint8_t *)rec->ExceptionInformation[1];
    if (p < (const uint8_t *)GP(0) || p >= (const uint8_t *)GP(COOP_NULL_SIZE)) return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&coop_null_minute);
    if (InterlockedIncrement(&coop_null_hits) <= COOP_NULL_SAID)
        hos_log("coop: null read: guest %#llx from hookshot.exe+%#llx (%s; reads 0, as on the emulator)",
                (unsigned long long)(p - (const uint8_t *)GP(0)),
                (unsigned long long)((uintptr_t)rec->ExceptionAddress - (uintptr_t)GetModuleHandleW(NULL)),
                rec->ExceptionInformation[0] ? "a write: it faults next" : "a read");
    return EXCEPTION_CONTINUE_EXECUTION;   /* (the guard bit is off for that page now: the access runs again) */
}

static DWORD WINAPI coop_null_rearm(LPVOID unused) {
    (void)unused;
    for (;;) {
        Sleep(60000);
        const LONG n = InterlockedExchange(&coop_null_minute, 0);
        if (n) hos_log("coop: null reads in the last minute: %ld page(s) first touched (%ld since the first puppet)", n,
                       coop_null_hits);
        DWORD was;
        VirtualProtect(GP(0), COOP_NULL_SIZE, PAGE_READONLY | PAGE_GUARD, &was);
    }
    return 0;
}

void hos_coop_null_page(void) {
    static volatile LONG done;
    if (done || InterlockedExchange(&done, 1)) return;
    const char *v = getenv("HOS_COOP_NULL_READS");
    if (v && v[0] == '0' && !v[1]) {
        hos_log("coop: null-pointer reads fault (HOS_COOP_NULL_READS=0)");
        return;
    }
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(GP(0), &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
        hos_log("coop: the guest's first MB is mapped already (HOS_LARGE_PAGES?): null-pointer reads give 0, not counted");
        return;
    }
    AddVectoredExceptionHandler(1, coop_null_read);
    if (!VirtualAlloc(GP(0), COOP_NULL_SIZE, MEM_COMMIT, PAGE_READONLY | PAGE_GUARD)) {
        hos_log("coop: the guest's first MB could not be mapped (error %lu): null-pointer reads still fault",
                (unsigned long)GetLastError());
        return;
    }
    CloseHandle(CreateThread(NULL, 0, coop_null_rearm, NULL, 0, NULL));
    hos_log("coop: the guest's first MB reads as zeros from now on (null-pointer reads give 0, as on the emulator; "
            "counted in this log; HOS_COOP_NULL_READS=0: off)");
}
#else
void hos_coop_null_page(void) {
    static int done;
    if (done) return;
    done = 1;
    hos_log("coop: the guest window is readable whole on this system: null-pointer reads give 0 already");
}
#endif
