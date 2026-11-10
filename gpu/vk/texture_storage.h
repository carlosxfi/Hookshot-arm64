#pragma once
#include <algorithm>
#include <cstdint>

namespace mkwvk::texture_storage {
// Compressed formats store bytes per block, including a whole block for each small mip.
// Dimensions describe the Vulkan image after any skipped/resized levels, not the guest image.
inline uint64_t bytes(uint32_t width, uint32_t height, uint32_t depth, uint32_t layers,
                      uint32_t levels, uint32_t samples, uint32_t block_w, uint32_t block_h,
                      uint32_t block_bytes) {
    uint64_t total = 0;
    for (uint32_t l = 0; l < levels; ++l) {
        const uint64_t w = std::max(1u, width), h = std::max(1u, height), d = std::max(1u, depth);
        total += ((w + block_w - 1) / block_w) * ((h + block_h - 1) / block_h) * d * block_bytes;
        width >>= 1; height >>= 1; depth >>= 1;
    }
    return total * layers * samples;
}
} // namespace mkwvk::texture_storage
