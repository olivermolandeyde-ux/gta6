#include "ecs/Archetype.h"

#include "core/Assert.h"
#include "ecs/ComponentRegistry.h"
#include "memory/MemorySystem.h"

#include <cstring>

namespace engine {

void archetype_build_layout(Archetype& arch, const ComponentRegistry& registry) {
    ENGINE_ASSERT(arch.column_count <= kMaxColumnsPerArchetype, "too many columns");

    u32 row_bytes = static_cast<u32>(sizeof(Entity));
    for (u32 i = 0; i < arch.column_count; ++i) {
        const ComponentType& type = registry.type(arch.columns[i].component_id);
        arch.columns[i].size      = type.size;
        arch.columns[i].alignment = type.alignment;
        row_bytes += type.size;
    }

    u16 capacity = compute_chunk_capacity(row_bytes, static_cast<u32>(sizeof(ChunkHeader)));

    auto fits = [&](u16 cap) -> bool {
        usize offset = sizeof(ChunkHeader);
        offset = align_up(offset, static_cast<usize>(alignof(Entity)));
        offset += sizeof(Entity) * cap;
        for (u32 i = 0; i < arch.column_count; ++i) {
            offset = align_up(offset, static_cast<usize>(arch.columns[i].alignment));
            offset += static_cast<usize>(arch.columns[i].size) * cap;
        }
        return offset <= kChunkBytes;
    };

    while (capacity > 0 && !fits(capacity)) {
        --capacity;
    }
    ENGINE_ASSERT(capacity > 0, "archetype layout does not fit in 16 KiB chunk");

    usize offset = sizeof(ChunkHeader);
    offset = align_up(offset, static_cast<usize>(alignof(Entity)));
    offset += sizeof(Entity) * capacity;
    for (u32 i = 0; i < arch.column_count; ++i) {
        offset = align_up(offset, static_cast<usize>(arch.columns[i].alignment));
        arch.columns[i].offset = static_cast<u32>(offset);
        offset += static_cast<usize>(arch.columns[i].size) * capacity;
    }

    arch.chunk_capacity = capacity;
}

u16 archetype_allocate_row(Archetype& arch, MemorySystem& memory, Entity entity) {
    Chunk* target      = nullptr;
    u16    chunk_index = 0;

    if (arch.chunk_count > 0) {
        chunk_index = static_cast<u16>(arch.chunk_count - 1);
        target      = arch.chunks[chunk_index];
        if (target->header().count >= target->header().capacity) {
            target = nullptr;
        }
    }

    if (!target) {
        ENGINE_ASSERT(arch.chunk_count < kMaxChunksPerArchetype, "archetype chunk cap reached");
        void* raw = memory.chunk_pool().allocate();
        target    = static_cast<Chunk*>(raw);
        std::memset(target->bytes, 0, kChunkBytes);

        ChunkHeader& header       = target->header();
        header.archetype          = &arch;
        header.count              = 0;
        header.capacity           = arch.chunk_capacity;
        header.index_in_archetype = static_cast<u16>(arch.chunk_count);

        arch.chunks[arch.chunk_count] = target;
        chunk_index = static_cast<u16>(arch.chunk_count);
        ++arch.chunk_count;
    }

    ChunkHeader& header           = target->header();
    const u16    row              = header.count;
    target->entities()[row]       = entity;
    ++header.count;
    ++arch.entity_count;
    return row;
}

void archetype_swap_remove(Archetype& arch, const ComponentRegistry& registry,
                           u32 chunk_index, u32 row, EntityRecord* records_by_index) {
    ENGINE_ASSERT(chunk_index < arch.chunk_count, "chunk index out of range");
    Chunk*       chunk  = arch.chunks[chunk_index];
    ChunkHeader& header = chunk->header();
    ENGINE_ASSERT(row < header.count, "row out of range");

    const u16 last_row = static_cast<u16>(header.count - 1);

    if (row != last_row) {
        const Entity moved           = chunk->entities()[last_row];
        chunk->entities()[row]       = moved;

        for (u32 c = 0; c < arch.column_count; ++c) {
            const ComponentType& type = registry.type(arch.columns[c].component_id);
            void* dst = chunk->bytes + arch.columns[c].offset
                        + static_cast<usize>(row) * arch.columns[c].size;
            void* src = chunk->bytes + arch.columns[c].offset
                        + static_cast<usize>(last_row) * arch.columns[c].size;
            type.destroy(dst);
            type.move(dst, src);
        }

        EntityRecord& moved_record        = records_by_index[moved.index()];
        moved_record.location.row         = row;
        moved_record.location.chunk_index = chunk_index;
    } else {
        for (u32 c = 0; c < arch.column_count; ++c) {
            const ComponentType& type = registry.type(arch.columns[c].component_id);
            void* ptr = chunk->bytes + arch.columns[c].offset
                        + static_cast<usize>(row) * arch.columns[c].size;
            type.destroy(ptr);
        }
    }

    --header.count;
    --arch.entity_count;
}

void archetype_release_empty_trailing_chunks(Archetype& arch, MemorySystem& memory) {
    while (arch.chunk_count > 0) {
        Chunk* last = arch.chunks[arch.chunk_count - 1];
        if (last->header().count != 0) {
            break;
        }
        memory.chunk_pool().deallocate(last);
        arch.chunks[arch.chunk_count - 1] = nullptr;
        --arch.chunk_count;
    }
}

} // namespace engine
