"""Co-op's looping platforms (oot-remake-multiplayer's patch 0003, its docs/objects.md "Moving platforms: the pause
counters"; method and addresses from Hot Onion).

main+0x5e6760 starts an AI ExecuteGenericCounter node (x0 = the node), the timer of each pause between two moves of a
looping platform (Dodongo's Cavern's lifts), each time the AI graph enters it. On the emulator co-op patches its first
word into a jump to a small cave (lichii/hook/coop_counter.s) that puts the node into a ring in the game's heap. Here:
C after `mov x19, x0` (main+0x5e6778, the end of the prologue) puts x0 into that same ring through
host/coop_platform_ring.c. The co-op module makes the ring (the game's malloc) and writes its address at
main+0x3c46780; until then, and without the module, nothing is written.

The words are checked and the build id; another game version leaves the hook out.
"""

MAIN = 0x80020000
BUILD_ID = "e0f3ccfaf50065ae4ea01eb5aeb13b1a45da3348"
SITE = 0x5e6778           # mov x19, x0
WORDS = ((0x5e6760, 0xd503237f),   # pacibsp
         (0x5e6764, 0xfc1d0fe8),   # str d8, [sp, #-0x30]!
         (0x5e6774, 0xf9400008),   # ldr x8, [x0]
         (0x5e6778, 0xaa0003f3),   # mov x19, x0
         (0x5e677c, 0xf940a108))   # ldr x8, [x8, #0x140]: the base length getter
TEXT = ("{ extern void hos_coop_counter(uint64_t node); hos_coop_counter(c->x[0]); }   "
        "/* (nx2/coop_platform_lift.py: co-op's pause counters, host/coop_platform_ring.c) */")


def _word(img, pc):
    try:
        return img.word(pc)
    except Exception:
        return None


def install(lifter, img, build_id):
    """The hook into lifter.after; nothing when the build or a word differs. Returns 1 when it went in."""
    if build_id != BUILD_ID:
        return 0
    for off, want in WORDS:
        have = _word(img, MAIN + off)
        if have != want:
            print("co-op platforms: main+%#x holds %s, not %08x: left out" % (
                off, "?" if have is None else "%08x" % have, want))
            return 0
    pc = MAIN + SITE
    lifter.after[pc] = (lifter.after[pc] + "\n" if pc in lifter.after else "") + TEXT
    print("co-op platforms: pause counter hook at main+%#x" % SITE)
    return 1
