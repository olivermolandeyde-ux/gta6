#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"

namespace engine {

struct ComponentType;
class Archetype;

// 16 KiB slab. Structure-of-arrays inside:
//
//   [ ChunkHeader | Entity[capacity] | col0[capacity] | col1[capacity] | ... ]
//
// Each column is aligned to the component's alignment. Capacity is computed
// so the whole layout fits in kChunkBytes with no remainder wasted on a
// partial row.
//
// Entities in a chunk are dense [0, count). Destroy uses swap-remove so
// iteration never branches on holes — required for 100k+ peds/vehicles.
struct ChunkHeader {
    Archetype* archetype = nullptr;
    u16        count     = 0;
    u16        capacity  = 0;
    u16        index_in_archetype = 0;
    u16        reserved  = 0;
};

struct Chunk {
    static constexpr usize kBytes = kChunkBytes;

    alignas(kCacheLineBytes) u8 bytes[kBytes];

    [[nodiscard]] ChunkHeader& header() noexcept {
        return *reinterpret_cast<ChunkHeader*>(bytes);
    }
    [[nodiscard]] const ChunkHeader& header() const noexcept {
        return *reinterpret_cast<const ChunkHeader*>(bytes);
    }

    [[nodiscard]] Entity* entities() noexcept {
        return reinterpret_cast<Entity*>(bytes + sizeof(ChunkHeader));
    }
    [[nodiscard]] const Entity* entities() const noexcept {
        return reinterpret_cast<const Entity*>(bytes + sizeof(ChunkHeader));
    }
};

// Column descriptor: byte offset of a component array inside a chunk.
struct ChunkColumn {
    u32 component_id;
    u32 offset;
    u32 size;
    u32 alignment;
};

[[nodiscard]] u16 compute_chunk_capacity(u32 entity_stride_plus_components, u32 header_bytes);

} // namespace engine
