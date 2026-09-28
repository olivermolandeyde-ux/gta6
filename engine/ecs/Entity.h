#pragma once

#include "core/Types.h"

namespace engine {

// 64-bit generational handle. Never a raw pointer. Stale handles fail
// is_alive() instead of silently corrupting a recycled slot — the classic
// "NPC got replaced by a parked car" bug in open-world streams.
//
//  Layout
//  ------
//  bits  0..31  index       (slot in the entity record table)
//  bits 32..47  generation  (bumped on destroy, 65536 recycles before wrap)
//  bits 48..63  world_id    (guards against mixing handles across worlds)
struct Entity {
    u64 packed = 0;

    static constexpr u64 kIndexMask      = 0x00000000FFFFFFFFull;
    static constexpr u64 kGenerationMask = 0x0000FFFF00000000ull;
    static constexpr u64 kWorldMask      = 0xFFFF000000000000ull;
    static constexpr u32 kGenerationShift = 32;
    static constexpr u32 kWorldShift      = 48;

    [[nodiscard]] static constexpr Entity make(u32 index, u16 generation, u16 world_id) noexcept {
        Entity e{};
        e.packed = static_cast<u64>(index)
                 | (static_cast<u64>(generation) << kGenerationShift)
                 | (static_cast<u64>(world_id) << kWorldShift);
        return e;
    }

    [[nodiscard]] constexpr u32 index() const noexcept {
        return static_cast<u32>(packed & kIndexMask);
    }
    [[nodiscard]] constexpr u16 generation() const noexcept {
        return static_cast<u16>((packed & kGenerationMask) >> kGenerationShift);
    }
    [[nodiscard]] constexpr u16 world_id() const noexcept {
        return static_cast<u16>((packed & kWorldMask) >> kWorldShift);
    }
    [[nodiscard]] constexpr bool is_null() const noexcept { return packed == 0; }

    [[nodiscard]] constexpr bool operator==(Entity other) const noexcept {
        return packed == other.packed;
    }
    [[nodiscard]] constexpr bool operator!=(Entity other) const noexcept {
        return packed != other.packed;
    }
};

inline constexpr Entity kNullEntity{};

// Dense location of a live entity inside an archetype chunk.
struct EntityLocation {
    u32 archetype_index;
    u32 chunk_index;
    u32 row;
};

// One record per allocated entity index. Stored in the record pool.
// The generation here is the source of truth; the handle's generation is a
// snapshot taken at create time.
struct EntityRecord {
    u16            generation;
    u16            flags;
    EntityLocation location;
    EntityRecord*  next_free;

    static constexpr u16 kFlagAlive     = 1u << 0;
    static constexpr u16 kFlagDeferred  = 1u << 1; // queued in a command buffer
    static constexpr u16 kFlagStreaming = 1u << 2;

    [[nodiscard]] bool alive() const noexcept { return (flags & kFlagAlive) != 0; }
};

enum class InstantiationDomain : u8 {
    PersistentWorld = 0, // missions, player, always-loaded infrastructure
    StreamingCell   = 1, // props, parked cars, ambient peds — cell-owned
    TransientFrame  = 2, // NEVER for entities. Documented so people don't try.
};

} // namespace engine
