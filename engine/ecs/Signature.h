#pragma once

#include "core/Types.h"

#include <cstring>

namespace engine {

// 256-bit component presence mask (kMaxComponents == 256).
// Archetype identity is this bitmask. Two entities with the same mask share
// an archetype and therefore share chunk layout.
struct Signature {
    u64 words[4]; // 4 * 64 = 256 bits

    void clear() noexcept {
        words[0] = words[1] = words[2] = words[3] = 0;
    }

    void set(u32 component_id) noexcept {
        const u32 word = component_id >> 6;
        const u32 bit  = component_id & 63u;
        words[word] |= (1ull << bit);
    }

    void unset(u32 component_id) noexcept {
        const u32 word = component_id >> 6;
        const u32 bit  = component_id & 63u;
        words[word] &= ~(1ull << bit);
    }

    [[nodiscard]] bool has(u32 component_id) const noexcept {
        const u32 word = component_id >> 6;
        const u32 bit  = component_id & 63u;
        return (words[word] & (1ull << bit)) != 0;
    }

    [[nodiscard]] bool contains(const Signature& required) const noexcept {
        return ((words[0] & required.words[0]) == required.words[0])
            && ((words[1] & required.words[1]) == required.words[1])
            && ((words[2] & required.words[2]) == required.words[2])
            && ((words[3] & required.words[3]) == required.words[3]);
    }

    [[nodiscard]] bool none_of(const Signature& excluded) const noexcept {
        return ((words[0] & excluded.words[0]) == 0)
            && ((words[1] & excluded.words[1]) == 0)
            && ((words[2] & excluded.words[2]) == 0)
            && ((words[3] & excluded.words[3]) == 0);
    }

    [[nodiscard]] bool operator==(const Signature& other) const noexcept {
        return words[0] == other.words[0]
            && words[1] == other.words[1]
            && words[2] == other.words[2]
            && words[3] == other.words[3];
    }

    [[nodiscard]] u64 hash() const noexcept {
        // Splitmix-ish fold. Good enough for the archetype hash map.
        u64 h = words[0] ^ (words[1] * 0x9E3779B97F4A7C15ull);
        h ^= words[2] + 0xBF58476D1CE4E5B9ull + (h << 6) + (h >> 2);
        h ^= words[3] + 0x94D049BB133111EBull + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace engine
