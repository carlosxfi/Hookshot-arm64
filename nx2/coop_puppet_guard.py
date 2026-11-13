"""Co-op's guard for the other players' Link (oot-remake-multiplayer's patch 0002, its docs/hookshot.md "Link puppets").

A co-op puppet is a stripped copy of Link (CoopPuppet_*: the pack keeps Link's model, animation and a few other
components, not his input, camera, physics or life). Making one on Hookshot crashed the game in its create, on
BaseProcCreateAndDeleteThread: "exception 0xc0000005 (read ..., guest 0x20)", guest lr main+0x4b8338.

main+0x4b8240 (called from main+0x10aceb0 when its mode is 0) sets ns_tex_mtx0 on the model's Mt_Hairs, then marks the
entries of another component dirty: x8 = [actor+0x270] (the actor's component table), [x8+0x50] (slot 10), [+0x20],
[+0x1c8], and when that is 0 it skips the marking (cbz to main+0x4b83b8). The puppet has no component in slot 10, so the
read of [0+0x20] faults. On the emulator unmapped guest reads give 0, so there the chain ends in that same cbz and the
create goes on; on Hookshot a guest read is a host read and it crashes.

The guard: C after `ldr x8, [x8, #0x50]` (main+0x4b833c): when the component is missing, go to main+0x4b83b8, the
function's own way out for "nothing to mark" (what the emulator does); the same when that component's +0x20 is 0. The first few are logged. Link (who has the
component) runs as shipped.

More of the puppet's create and update reads through such missing pointers ("guest 0x7d8" next, on BaseProcExecutor1).
So the first time the guard meets such a puppet it also calls host/coop_null_page.c (once): the guest's first MB is
committed read-only, and a read through a null pointer gives 0 from then on, as on the emulator (HOS_COOP_NULL_READS=0:
off); such reads are counted in the log. Only a game that made a co-op puppet changes; the game alone runs as shipped.

The words are checked and the build id; another game version leaves the guard out.
"""

MAIN = 0x80020000
BUILD_ID = "e0f3ccfaf50065ae4ea01eb5aeb13b1a45da3348"
SITE = 0x4b833c           # ldr x8, [x8, #0x50]
SKIP = 0x4b83b8           # the epilogue the function's cbz at main+0x4b8348 goes to
WORDS = ((0x4b8338, 0xf9413aa8),   # ldr x8, [x21, #0x270]
         (0x4b833c, 0xf9402908),   # ldr x8, [x8, #0x50]
         (0x4b8340, 0xf9401108),   # ldr x8, [x8, #0x20]
         (0x4b8344, 0xf940e508),   # ldr x8, [x8, #0x1c8]
         (0x4b8348, 0xb4000388))   # cbz x8, main+0x4b83b8
THEN = 0x4b8340           # ldr x8, [x8, #0x20]: its +0x20 missing too ends the same way
TEXT_THEN = "if (!c->x[8]) goto L_%x;   /* (nx2/coop_puppet_guard.py) */" % (MAIN + SKIP)
TEXT = ("if (!c->x[8]) { extern void hos_log(const char *fmt, ...); extern void hos_coop_null_page(void); "
        "static volatile long said; hos_coop_null_page(); "
        "if (said++ < 8) hos_log(\"coop: puppet guard: actor %%#llx has no component in slot 10: its hair's marks \"\n"
        "\"skipped, as on the emulator (nx2/coop_puppet_guard.py)\", (unsigned long long)c->x[21]); "
        "goto L_%x; }   /* (nx2/coop_puppet_guard.py) */") % (MAIN + SKIP)


def install(lifter, img, build_id):
    """The guard into lifter.after; nothing when the build or a word differs. Returns 1 when it went in."""
    if build_id != BUILD_ID:
        print("co-op puppet guard: build %s is not %s: left out" % (build_id or "?", BUILD_ID))
        return 0
    for off, want in WORDS:
        try:
            have = img.word(MAIN + off)
        except Exception:
            have = None
        if have != want:
            print("co-op puppet guard: main+%#x holds %s, not %08x: left out" % (
                off, "?" if have is None else "%08x" % have, want))
            return 0
    pc = MAIN + SITE
    lifter.after[pc] = (lifter.after[pc] + "\n" if pc in lifter.after else "") + TEXT
    print("co-op puppet guard: at main+%#x (a missing component skips to main+%#x); null reads give 0 from the "
          "first puppet on" % (SITE, SKIP))
    return 1
