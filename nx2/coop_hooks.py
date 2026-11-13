"""Co-op's hooks for Ocarina of Time 1.1.1 (oot-remake-multiplayer's patch, its docs/hookshot.md): C after three of the
game's instructions (a64_lift.py Lifter.after), calling host/coop_bridge.c.

- The frame hook: moduleCalc_ calls the frame function (main+0x3b4e300) from two places, each right after `mov x0, x20`.
  hos_coop_frame runs after that mov, before the call, on the game's calc thread: this frame's actors have run and the
  next draw has not read them yet. The emulator's co-op patches the two calls themselves (lichii/hook/coop_svc.h kHooks).
- The AI hold: main+0x369df8 is `blr x8`, the call whose answer (w0) gives a BSA component (x21) its calc bits for the
  frame. After the next instruction (an ldrh into w8, which leaves w0 alone) hos_coop_hold may change those bits
  (lichii/hook/coop_hold.h).

Each site's words are checked, and the build id: another game version leaves the hooks out. Without the co-op module the
bridge's functions return at once.
"""

MAIN = 0x80020000   # (main's base in this image: nx2/eden_image.py)
BUILD_ID = "e0f3ccfaf50065ae4ea01eb5aeb13b1a45da3348"
MOV_X0_X20 = 0xaa1403e0

# (offset of the mov, the bl after it, the site number hos_coop_frame gets: host/hos_coop_api.h HOS_COOP_SITE_*)
FRAME_SITES = ((0x6e6dd4, 0x94d19d4a, 0), (0x6e70f4, 0x94d19c82, 1))
HOLD_CALL = 0x369df8      # blr x8
HOLD_WORDS = (0xd63f0100, 0x79418aa8)   # blr x8; ldrh w8, [x21, #0xc4]

FRAME_TEXT = ("{ extern void hos_coop_frame(Ctx *c, int site); hos_coop_frame(c, %d); }   "
              "/* (nx2/coop_hooks.py: co-op's frame hook) */")
HOLD_TEXT = ("{ extern uint32_t hos_coop_hold(uint64_t component, uint32_t bits); "
             "const uint32_t was = (uint32_t)c->x[0], now = hos_coop_hold(c->x[21], was); "
             "if (now != was) c->x[0] = now; }   /* (nx2/coop_hooks.py: co-op's AI hold) */")


def _word(img, pc):
    try:
        return img.word(pc)
    except Exception:
        return None


def _add(lifter, pc, text):
    lifter.after[pc] = (lifter.after[pc] + "\n" if pc in lifter.after else "") + text


def install(lifter, img, build_id):
    """The hooks into lifter.after; nothing at all when the build or any word differs. Returns how many went in."""
    if build_id != BUILD_ID:
        print("co-op hooks: build %s is not %s: left out" % (build_id or "?", BUILD_ID))
        return 0
    checks = [(MAIN + mov, MOV_X0_X20) for mov, _, _ in FRAME_SITES] + \
             [(MAIN + mov + 4, bl) for mov, bl, _ in FRAME_SITES] + \
             [(MAIN + HOLD_CALL + 4 * i, w) for i, w in enumerate(HOLD_WORDS)]
    for pc, want in checks:
        have = _word(img, pc)
        if have != want:
            print("co-op hooks: main+%#x holds %s, not %08x: left out" % (
                pc - MAIN, "?" if have is None else "%08x" % have, want))
            return 0
    for mov, _, site in FRAME_SITES:
        _add(lifter, MAIN + mov, FRAME_TEXT % site)
    _add(lifter, MAIN + HOLD_CALL + 4, HOLD_TEXT)
    print("co-op hooks: frame hook at main+%#x and main+%#x, AI hold at main+%#x" % (
        FRAME_SITES[0][0], FRAME_SITES[1][0], HOLD_CALL + 4))
    import coop_puppet_guard   # (patch 0002: the other players' Link)
    coop_puppet_guard.install(lifter, img, build_id)
    import coop_platform_lift   # (patch 0003: looping platforms' pause counters)
    coop_platform_lift.install(lifter, img, build_id)
    return len(FRAME_SITES) + 1
