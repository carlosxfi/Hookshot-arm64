// CPU/GPU storage for devices that cannot import guest pages. Vulkan synchronization belongs to the caller:
// upload only after the preceding GPU work finishes; download only after a HOST_READ barrier and its fence.
// Inspired by TotK's shadow storage (see host/ARM64_PROVENANCE.md); this implementation is specific to Hookshot.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mkwvk {
class GuestCopy {
    struct Range { size_t lo, hi; };
    std::vector<uint8_t> before;
    std::vector<Range> writes;
public:
    void init(uint8_t* mapped, const uint8_t* guest, size_t size) {
        before.resize(size);
        writes.clear();
        if (!size) return;
        std::memcpy(before.data(), guest, size);
        std::memcpy(mapped, before.data(), size);
    }
    void note_write(size_t offset, size_t size) {
        if (offset >= before.size() || !size) return;
        writes.push_back({offset, offset + std::min(size, before.size() - offset)});
    }
    // Compare pages rather than blindly replacing the buffer: unchanged pages keep GPU results. Keep exactly
    // the bytes uploaded as the baseline, even if another guest thread is writing while the copy is taken.
    uint64_t upload(uint8_t* mapped, const uint8_t* guest) {
        uint64_t bytes = 0;
        for (size_t at = 0; at < before.size(); at += 4096) {
            const size_t n = std::min<size_t>(4096, before.size() - at);
            if (!std::memcmp(guest + at, before.data() + at, n)) continue;
            // Capture once into cached CPU memory, then upload those same bytes. Reading the mapped GPU
            // allocation back just to build this baseline is especially expensive on uncached ARM mappings.
            std::memcpy(before.data() + at, guest + at, n);
            std::memcpy(mapped + at, before.data() + at, n);
            bytes += n;
        }
        return bytes;
    }
    template<class Written> uint64_t download(uint8_t* guest, const uint8_t* mapped, Written written) {
        if (before.empty()) return 0;
        uint64_t bytes = 0;
        // Pointer-following shaders can write outside their reported binding. Compare all pages as well as
        // the explicit write ranges, and preserve CPU changes in bytes the GPU left alone.
        for (size_t at = 0; at < before.size(); at += 4096) {
            const size_t n = std::min<size_t>(4096, before.size() - at);
            if (!std::memcmp(mapped + at, before.data() + at, n)) continue;
            for (size_t i = at; i < at + n; ++i)
                if (mapped[i] != before[i]) guest[i] = mapped[i], ++bytes;
            std::memcpy(before.data() + at, mapped + at, n);
            written(at, n);
        }
        // A known GPU store must win even if it stored its previous value. A diff alone cannot detect that.
        std::sort(writes.begin(), writes.end(), [](Range a, Range b) { return a.lo < b.lo; });
        for (size_t i = 0; i < writes.size();) {
            Range r = writes[i++];
            while (i < writes.size() && writes[i].lo <= r.hi) r.hi = std::max(r.hi, writes[i++].hi);
            std::memcpy(guest + r.lo, mapped + r.lo, r.hi - r.lo);
            written(r.lo, r.hi - r.lo);
            bytes += r.hi - r.lo;
        }
        writes.clear();
        return bytes;
    }
    uint64_t download(uint8_t* guest, const uint8_t* mapped) {
        return download(guest, mapped, [](size_t, size_t) {});
    }
};
} // namespace mkwvk
