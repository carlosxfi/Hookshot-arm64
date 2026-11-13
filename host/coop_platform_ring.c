/* (oot-remake-multiplayer's patch 0003, nx2/coop_platform_lift.py) A started AI pause counter goes into co-op's ring.

   The same ring the emulator's cave fills (lichii/hook/coop_counter.s): its address is the u64 at main+0x3c46780,
   0 until the co-op module made it with the game's malloc (lichii/hook/coop_counters.h). Ring: +0 a u32 count, +8 256
   u64 nodes; the node goes at (count + 1) % 256 and the count goes up. The module reads it a few times a second and
   checks every node itself (vtable, actor), so a lost or doubled entry costs nothing; no lock, as on the emulator. */
#include "hos.h"

enum { COOP_RING_NODES = 256 };
#define COOP_MAIN 0x80020000ull
#define COOP_RING_POINTER (COOP_MAIN + 0x3c46780ull)

void hos_coop_counter(uint64_t node) {
    const uint64_t ring = G64(COOP_RING_POINTER);
    if (!ring || !node) return;
    const uint32_t n = G32(ring) + 1;
    G32(ring) = n;
    *(uint64_t *)GP(ring + 8 + 8ull * (n % COOP_RING_NODES)) = node;
}
