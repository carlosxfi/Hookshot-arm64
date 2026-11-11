#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace mkwvk {
// Upload generations are CPU writes only. Texture generations also include GPU write declarations,
// which can cover hundreds of MB before the GPU has changed any guest bytes. Copy-mode replay is
// serialized; callers use the same ordering as the renderer's existing page-generation tracking.
class GuestCopyCpuWrites {
    static constexpr uint64_t page_count = 1ull << 27, chunk_pages = 1ull << 16, region_pages = 512;
    std::vector<std::unique_ptr<uint32_t[]>> chunks;
    std::vector<uint32_t> regions;
public:
    void note(uint64_t page, uint32_t generation) {
        if (page >= page_count) return;
        if (chunks.empty()) {
            chunks.resize(page_count / chunk_pages);
            regions.resize(page_count / region_pages);
        }
        auto& chunk = chunks[page / chunk_pages];
        if (!chunk) chunk = std::make_unique<uint32_t[]>(chunk_pages);
        chunk[page % chunk_pages] = std::max(chunk[page % chunk_pages], generation);
        auto& region = regions[page / region_pages];
        region = std::max(region, generation);
    }
    void runs(uint64_t guest, uint64_t size, uint32_t since,
              std::vector<std::pair<uint64_t, uint64_t>>& out) const {
        if (!size || regions.empty() || guest >= (page_count << 12)) return;
        const uint64_t end = guest + std::min(size, (page_count << 12) - guest);
        const uint64_t first = guest >> 12, last = (end - 1) >> 12;
        for (uint64_t r = first / region_pages; r <= last / region_pages; ++r) {
            if (regions[r] <= since) continue;
            const uint64_t a = std::max(first, r * region_pages);
            const uint64_t b = std::min(last, (r + 1) * region_pages - 1);
            for (uint64_t p = a; p <= b; ++p) {
                const auto& chunk = chunks[p / chunk_pages];
                if (!chunk || chunk[p % chunk_pages] <= since) continue;
                const uint64_t lo = std::max(guest, p << 12) - guest;
                const uint64_t hi = std::min(end, (p + 1) << 12) - guest;
                if (!out.empty() && out.back().second == lo) out.back().second = hi;
                else out.push_back({lo, hi});
            }
        }
    }
};
} // namespace mkwvk
