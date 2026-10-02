#pragma once

#include "core/Types.h"
#include "ecs/Chunk.h"
#include "ecs/Signature.h"

namespace engine {

class ComponentRegistry;
class MemorySystem;

inline constexpr u32 kMaxColumnsPerArchetype = 32;
inline constexpr u32 kMaxChunksPerArchetype  = 1024;

// An Archetype is a unique component combination plus the packed SoA
// storage of every entity that currently has that combination.
//
// Structural changes (add/remove component) MOVE the entity to a different
// archetype. That copy is memcpy of trivially-copyable columns — no
// allocator traffic, no GC.
struct Archetype {
    u32       index;
    Signature signature;
    u32       column_count;
    u16       chunk_capacity;
    u16       chunk_count;
    u32       entity_count;

    ChunkColumn columns[kMaxColumnsPerArchetype];
    Chunk*      chunks[kMaxChunksPerArchetype];

    // Edge cache: add/remove a single component_id → destination archetype.
    // Avoids hashing the signature on every structural change.
    struct Edge {
        u32  component_id;
        u32  archetype_index;
        bool add;
    };
    static constexpr u32 kMaxEdges = 64;
    Edge edges[kMaxEdges];
    u32  edge_count;

    [[nodiscard]] const ChunkColumn* find_column(u32 component_id) const noexcept {
        for (u32 i = 0; i < column_count; ++i) {
            if (columns[i].component_id == component_id) {
                return &columns[i];
            }
        }
        return nullptr;
    }

    [[nodiscard]] u32 find_edge(u32 component_id, bool add) const noexcept {
        for (u32 i = 0; i < edge_count; ++i) {
            if (edges[i].component_id == component_id && edges[i].add == add) {
                return edges[i].archetype_index;
            }
        }
        return kInvalidIndex;
    }

    void remember_edge(u32 component_id, bool add, u32 archetype_index) noexcept {
        if (edge_count >= kMaxEdges) {
            return;
        }
        edges[edge_count++] = Edge{component_id, archetype_index, add};
    }

    [[nodiscard]] void* component_at(u32 chunk_index, u32 column_index, u32 row) noexcept {
        Chunk* chunk = chunks[chunk_index];
        const ChunkColumn& col = columns[column_index];
        return chunk->bytes + col.offset + static_cast<usize>(row) * col.size;
    }

    [[nodiscard]] const void* component_at(u32 chunk_index, u32 column_index, u32 row) const noexcept {
        const Chunk* chunk = chunks[chunk_index];
        const ChunkColumn& col = columns[column_index];
        return chunk->bytes + col.offset + static_cast<usize>(row) * col.size;
    }
};

void archetype_build_layout(Archetype& arch, const ComponentRegistry& registry);
u16  archetype_allocate_row(Archetype& arch, MemorySystem& memory, Entity entity);
void archetype_swap_remove(Archetype& arch, const ComponentRegistry& registry,
                           u32 chunk_index, u32 row, EntityRecord* records_by_index);
void archetype_release_empty_trailing_chunks(Archetype& arch, MemorySystem& memory);

} // namespace engine
