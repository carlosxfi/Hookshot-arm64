/* Co-op (oot-remake-multiplayer, GPL-3.0; its docs/hookshot.md): the bridge between the game and the co-op module.

   nx2/coop_hooks.py puts two calls into the lifted game: hos_coop_frame at the frame function's two calls in moduleCalc_,
   and hos_coop_hold after the AI's calc-bits call. On the first frame hook this loads hookshot_coop.dll (hookshot_coop.so)
   from beside the executable, unless HOS_COOP is 0/off, and hands it the table in hos_coop_api.h: guest memory, game
   function calls and the log. The module owns its sockets and network thread; the game never waits for them. Without the
   module (no file, refused, or co-op off in its settings) every hook returns at once. */
#include "hos.h"
#include "hos_coop_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

uint8_t *hos_guest_base(void);                       /* (hos_main.c) */
int hos_mem_region(uint64_t addr, uint64_t size);    /* (hos_mem.c: 0 if not wholly in mapped guest memory) */
void hos_mark_written(const void *p, uint64_t n);    /* (hos_mem.c: the GPU mirror picks the pages up) */
extern const char hos_main_build_id[];              /* (gen/hos_image.c) */

#ifdef _WIN32
#define COOP_DLL "hookshot_coop.dll"
#define COOP_DLL_W L"hookshot_coop.dll"
#else
#define COOP_DLL "hookshot_coop.so"
#endif

static int state;   /* (atomic) 0 not tried, 1 loading, 2 on, 3 off */
static hos_coop_frame_fn coop_frame;
static hos_coop_hold_fn coop_hold;
static hos_coop_stop_fn coop_stop;
static char game_dir[MAX_PATH * 3];   /* (UTF-8) */

static int in_guest(uint64_t a, size_t n) { return n == 0 || (a + n > a && hos_mem_region(a, n)); }

static int bridge_read(uint64_t a, void *out, size_t n) {
    if (!in_guest(a, n)) return 0;
    if (n) memcpy(out, hos_guest_base() + a, n);
    return 1;
}
static int bridge_write(uint64_t a, const void *in, size_t n) {
    if (!in_guest(a, n)) return 0;
    if (n) {
        memcpy(hos_guest_base() + a, in, n);
        hos_mark_written(hos_guest_base() + a, n);
    }
    return 1;
}
static int bridge_cas32(uint64_t a, uint32_t expected, uint32_t desired, uint32_t *seen) {
    if ((a & 3) || !in_guest(a, 4)) return 0;
    uint32_t *p = (uint32_t *)(hos_guest_base() + a);
    uint32_t e = expected;
    if (__atomic_compare_exchange_n(p, &e, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) hos_mark_written(p, 4);
    if (seen) *seen = e;
    return 1;
}
/* A game function from the frame hook: the game's own context, saved and put back whole, with the call's arguments, four
   words at a stack pointer below the game's, and s0. Lifted code returns to us like any C function. */
static int bridge_call(void *ctx, const HosCoopCall *k, uint64_t *x0) {
    Ctx *c = (Ctx *)ctx;
    A64Fn f = k && k->function ? a64_lookup(k->function) : NULL;
    if (!c || !f) return 0;
    const Ctx saved = *c;
    const uint64_t sp = (c->x[31] - 0x200) & ~0xfULL;
    if (!in_guest(sp, sizeof k->stack)) return 0;
    for (int i = 0; i < 4; i++) recomp_store64(c, sp + 8 * i, k->stack[i]);
    for (int i = 0; i < 8; i++) c->x[i] = k->x[i];
    uint32_t s0;
    memcpy(&s0, &k->s0, sizeof s0);
    c->vreg[0][0] = s0, c->vreg[0][1] = 0;
    c->x[30] = 0;
    c->x[31] = sp;
    f(c);
    if (x0) *x0 = c->x[0];
    *c = saved;
    return 1;
}
static void bridge_log(const char *line) { hos_log("%s", line ? line : ""); }
static hos_coop_key_fn coop_key;   /* (optional: typing, display.c and nn_hid.c below) */
static hos_coop_char_fn coop_char;
static hos_coop_keys_taken_fn coop_keys_taken;
static hos_coop_mouse_fn coop_mouse;   /* (optional: the cheats panel's clicks, display.c) */
extern volatile uint64_t hos_coop_pad;   /* nn_hid.c: the word npad_states gave the game */
static uint64_t bridge_pad_buttons(void) { return __atomic_load_n(&hos_coop_pad, __ATOMIC_RELAXED); }
/* (a key held now, by virtual-key code: Windows' own, or the SDL window's keys on Linux, host/posix/display_sdl.c) */
static int bridge_key_down(uint32_t vk) { return vk < 256 && (GetAsyncKeyState((int)vk) & 0x8000) != 0; }

static void at_exit(void) {
    if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) == 2 && coop_stop) coop_stop();
}

static void load(void) {
    const char *e = getenv("HOS_COOP");
    if (e && (!strcmp(e, "0") || !strcmp(e, "off") || !strcmp(e, "no") || !strcmp(e, "false") || !strcmp(e, "OFF"))) {
        hos_log("coop: off (HOS_COOP=%s)", e);
        __atomic_store_n(&state, 3, __ATOMIC_RELEASE);
        return;
    }
    char path[MAX_PATH * 3];   /* (UTF-8: the module's folder for the module, and for the log) */
#ifdef _WIN32
    /* Wide calls, so a game folder whose name has characters outside the ANSI code page still loads the module. */
    wchar_t wpath[MAX_PATH];
    const DWORD wn = GetModuleFileNameW(NULL, wpath, MAX_PATH);
    wchar_t *wslash = wn && wn < MAX_PATH ? wcsrchr(wpath, L'\\') : NULL;
    if (!wslash || (size_t)(wslash + 1 - wpath) + sizeof COOP_DLL > MAX_PATH) {
        __atomic_store_n(&state, 3, __ATOMIC_RELEASE);
        return;
    }
    *wslash = 0;
    if (!WideCharToMultiByte(CP_UTF8, 0, wpath, -1, game_dir, sizeof game_dir, NULL, NULL)) {
        __atomic_store_n(&state, 3, __ATOMIC_RELEASE);
        return;
    }
    memcpy(wslash, L"\\" COOP_DLL_W, sizeof(L"\\" COOP_DLL_W));
    snprintf(path, sizeof path, "%s\\" COOP_DLL, game_dir);
    if (GetFileAttributesW(wpath) == INVALID_FILE_ATTRIBUTES) {   /* (no module: not a co-op game folder, nothing to say) */
        __atomic_store_n(&state, 3, __ATOMIC_RELEASE);
        return;
    }
    HMODULE m = LoadLibraryW(wpath);
#else
    const DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    char *slash = n && n < MAX_PATH ? strrchr(path, '/') : NULL;
    if (!slash || (size_t)(slash + 1 - path) + sizeof COOP_DLL > MAX_PATH) {
        __atomic_store_n(&state, 3, __ATOMIC_RELEASE);
        return;
    }
    *slash = 0;
    snprintf(game_dir, sizeof game_dir, "%s", path);
    memcpy(slash, "/" COOP_DLL, sizeof "/" COOP_DLL);
    HMODULE m = LoadLibraryA(path);
#endif
    if (!m) {
        hos_log("coop: %s did not load (%lu): co-op off", path, (unsigned long)GetLastError());
        __atomic_store_n(&state, 3, __ATOMIC_RELEASE);
        return;
    }
    hos_coop_start_fn start = (hos_coop_start_fn)(void *)GetProcAddress(m, "hos_coop_start");
    coop_frame = (hos_coop_frame_fn)(void *)GetProcAddress(m, "hos_coop_frame");
    coop_hold = (hos_coop_hold_fn)(void *)GetProcAddress(m, "hos_coop_hold");
    coop_stop = (hos_coop_stop_fn)(void *)GetProcAddress(m, "hos_coop_stop");
    coop_key = (hos_coop_key_fn)(void *)GetProcAddress(m, "hos_coop_key");
    coop_char = (hos_coop_char_fn)(void *)GetProcAddress(m, "hos_coop_char");
    coop_keys_taken = (hos_coop_keys_taken_fn)(void *)GetProcAddress(m, "hos_coop_keys_taken");
    coop_mouse = (hos_coop_mouse_fn)(void *)GetProcAddress(m, "hos_coop_mouse");
    if (!start || !coop_frame || !coop_hold || !coop_stop) {
        hos_log("coop: %s lacks an export: co-op off", path);
        __atomic_store_n(&state, 3, __ATOMIC_RELEASE);
        return;
    }
    static HosCoopApi api;
    api.version = HOS_COOP_API_VERSION;
    api.size = sizeof api;
    api.main_base = 0x80020000ULL;
    api.main_build_id = hos_main_build_id;
    api.game_dir = game_dir;
    api.read = bridge_read, api.write = bridge_write, api.cas32 = bridge_cas32, api.call = bridge_call, api.log = bridge_log;
    api.pad_buttons = bridge_pad_buttons;
    api.key_down = bridge_key_down;
    if (!start(&api)) {
        hos_log("coop: the module stays off");
        __atomic_store_n(&state, 3, __ATOMIC_RELEASE);
        return;
    }
    atexit(at_exit);
    hos_log("coop: module loaded (%s)", path);
    __atomic_store_n(&state, 2, __ATOMIC_RELEASE);
}

/* display.c: a key press or a character for the module first (its chat box); 1 = taken. */
int hos_coop_key(unsigned vk, int repeat) {
    return __atomic_load_n(&state, __ATOMIC_ACQUIRE) == 2 && coop_key ? coop_key(vk, repeat) : 0;
}
int hos_coop_char(unsigned ch) {
    return __atomic_load_n(&state, __ATOMIC_ACQUIRE) == 2 && coop_char ? coop_char(ch) : 0;
}
/* display.c: the cursor (a fraction of the client area) for the module first (its cheats panel); 1 = taken. */
int hos_coop_mouse(float fx, float fy, int event) {
    return __atomic_load_n(&state, __ATOMIC_ACQUIRE) == 2 && coop_mouse ? coop_mouse(fx, fy, event) : 0;
}
/* nn_hid.c key: the module has the keyboard (the game reads no keys) */
int hos_coop_keys_taken(void) {
    return __atomic_load_n(&state, __ATOMIC_ACQUIRE) == 2 && coop_keys_taken ? coop_keys_taken() : 0;
}

/* nvn_native.c nvnQueuePresentTexture: every present, once the module is loaded (it loads at a frame hook). */
void hos_coop_present(void) {
    if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) == 2) coop_frame(NULL, HOS_COOP_SITE_PRESENT);
}

void hos_coop_frame(Ctx *c, int site) {
    int s = __atomic_load_n(&state, __ATOMIC_ACQUIRE);
    if (s == 0) {
        int expected = 0;
        if (!__atomic_compare_exchange_n(&state, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
        load();   /* (sets 2 or 3) */
        s = __atomic_load_n(&state, __ATOMIC_ACQUIRE);
    }
    if (s == 2) coop_frame(c, site);
}

uint32_t hos_coop_hold(uint64_t component, uint32_t bits) {
    return __atomic_load_n(&state, __ATOMIC_ACQUIRE) == 2 ? coop_hold(component, bits) : bits;
}
