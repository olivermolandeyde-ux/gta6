#include "ecs/World.h"

#include "core/Assert.h"
#include "memory/MemorySystem.h"

#include <cstring>

namespace engine {

void World::boot(MemorySystem& memory, u16 world_id, u32 entity_capacity) {
    ENGINE_ASSERT(memory.is_booted(), "MemorySystem must boot before World");
    ENGINE_ASSERT(world_id != 0, "world_id 0 is reserved");
    ENGINE_ASSERT(entity_capacity > 1, "entity capacity too small");

    memory_          = &memory;
    world_id_        = world_id;
    entity_capacity_ = entity_capacity;
    next_index_      = 1;
    free_count_      = 0;
    live_entities_   = 0;
    archetype_count_ = 0;
    frame_index_     = 0;
    std::memset(archetype_map_, 0, sizeof(archetype_map_));
    for (u32 i = 0; i < kArchetypeMapSlots; ++i) {
        archetype_map_[i].index = kInvalidIndex;
    }
    std::memset(archetypes_, 0, sizeof(archetypes_));

    Allocator world_alloc = memory.world_allocator();

    records_ = static_cast<EntityRecord*>(
        world_alloc.allocate(sizeof(EntityRecord) * entity_capacity_, alignof(EntityRecord)));
    std::memset(records_, 0, sizeof(EntityRecord) * entity_capacity_);

    free_indices_ = static_cast<u32*>(
        world_alloc.allocate(sizeof(u32) * entity_capacity_, alignof(u32)));
    std::memset(free_indices_, 0, sizeof(u32) * entity_capacity_);

    // Empty archetype (no columns) so create_empty has a home.
    Signature empty{};
    (void)ensure_archetype(empty);

    constexpr usize kCmdBytes     = 256 * 1024;
    constexpr usize kPayloadBytes = 1 * 1024 * 1024;
    cmd_block_   = world_alloc.allocate(kCmdBytes, alignof(CommandBuffer::Command));
    cmd_payload_ = world_alloc.allocate(kPayloadBytes, 16);
    frame_commands_.bind(cmd_block_, kCmdBytes, cmd_payload_, kPayloadBytes);
}

void World::shutdown() {
    if (!memory_) {
        return;
    }
    for (u32 a = 0; a < archetype_count_; ++a) {
        Archetype* arch = archetypes_[a];
        for (u16 c = 0; c < arch->chunk_count; ++c) {
            Chunk* chunk = arch->chunks[c];
            ChunkHeader& header = chunk->header();
            for (u16 row = 0; row < header.count; ++row) {
                for (u32 col = 0; col < arch->column_count; ++col) {
                    const ComponentType& type = registry_.type(arch->columns[col].component_id);
                    void* ptr = chunk->bytes + arch->columns[col].offset
                                + static_cast<usize>(row) * arch->columns[col].size;
                    type.destroy(ptr);
                }
            }
            memory_->chunk_pool().deallocate(chunk);
            arch->chunks[c] = nullptr;
        }
        arch->chunk_count = 0;
        archetypes_[a] = nullptr;
    }
    archetype_count_ = 0;
    live_entities_   = 0;
    memory_          = nullptr;
}

void World::begin_frame(u64 frame_index) {
    frame_index_ = frame_index;
    memory_->begin_frame(frame_index);
    frame_commands_.reset();
}

bool World::is_alive(Entity entity) const noexcept {
    if (entity.is_null() || entity.world_id() != world_id_) {
        return false;
    }
    const u32 index = entity.index();
    if (index == 0 || index >= entity_capacity_) {
        return false;
    }
    const EntityRecord& rec = records_[index];
    return rec.alive() && rec.generation == entity.generation();
}

EntityRecord& World::record(Entity entity) {
    ENGINE_ASSERT(is_alive(entity), "stale or null entity handle");
    return records_[entity.index()];
}

const EntityRecord& World::record(Entity entity) const {
    ENGINE_ASSERT(is_alive(entity), "stale or null entity handle");
    return records_[entity.index()];
}

u32 World::acquire_index() {
    if (free_count_ > 0) {
        --free_count_;
        return free_indices_[free_count_];
    }
    ENGINE_ASSERT(next_index_ < entity_capacity_, "entity table exhausted");
    const u32 index = next_index_;
    ++next_index_;
    return index;
}

void World::release_index(u32 index) {
    ENGINE_ASSERT(free_count_ < entity_capacity_, "free list overflow");
    free_indices_[free_count_] = index;
    ++free_count_;
}

Entity World::create_empty(const InstantiationRequest& request) {
    validate_instantiation(request);

    const u32 index = acquire_index();
    EntityRecord& rec = records_[index];
    if (rec.generation == 0) {
        rec.generation = 1;
    }
    rec.flags = EntityRecord::kFlagAlive;
    if (request.domain == InstantiationDomain::StreamingCell) {
        rec.flags = static_cast<u16>(rec.flags | EntityRecord::kFlagStreaming);
    }
    rec.next_free = nullptr;

    const Entity entity = Entity::make(index, rec.generation, world_id_);

    Signature empty{};
    Archetype& arch = ensure_archetype(empty);
    const u16 row   = archetype_allocate_row(arch, *memory_, entity);

    rec.location.archetype_index = arch.index;
    rec.location.chunk_index     = static_cast<u32>(arch.chunk_count - 1);
    rec.location.row             = row;

    // The last chunk is the one allocate_row appended into, but if it reused
    // a partially filled chunk we must read the header's index.
    rec.location.chunk_index = arch.chunks[rec.location.chunk_index]->header().index_in_archetype;
    // allocate_row may have used an earlier non-full last chunk — always
    // resolve from the entity's stored row by scanning is wasteful. The
    // function appends to the last chunk or creates a new last chunk, so the
    // live chunk is always chunks[chunk_count-1].
    rec.location.chunk_index = static_cast<u32>(arch.chunk_count - 1);

    ++live_entities_;
    return entity;
}

void World::destroy(Entity entity) {
    if (!is_alive(entity)) {
        return;
    }
    EntityRecord& rec = records_[entity.index()];
    Archetype& arch   = *archetypes_[rec.location.archetype_index];
    archetype_swap_remove(arch, registry_, rec.location.chunk_index, rec.location.row, records_);
    archetype_release_empty_trailing_chunks(arch, *memory_);

    rec.flags = 0;
    rec.location.archetype_index = kInvalidIndex;
    rec.location.chunk_index     = kInvalidIndex;
    rec.location.row             = kInvalidIndex;
    rec.generation = static_cast<u16>(rec.generation + 1);
    if (rec.generation == 0) {
        rec.generation = 1; // 0 is reserved for "never used"
    }
    release_index(entity.index());
    --live_entities_;
}

Archetype& World::ensure_archetype(const Signature& signature) {
    const u64 hash = signature.hash();
    const u32 found = map_find(signature, hash);
    if (found != kInvalidIndex) {
        return *archetypes_[found];
    }

    ENGINE_ASSERT(archetype_count_ < kMaxArchetypes, "archetype cap reached");
    Allocator world_alloc = memory_->world_allocator();
    auto* arch = static_cast<Archetype*>(world_alloc.allocate(sizeof(Archetype), alignof(Archetype)));
    std::memset(arch, 0, sizeof(Archetype));
    arch->index     = archetype_count_;
    arch->signature = signature;

    // Columns follow the component-id order so layout is deterministic.
    for (u32 id = 0; id < registry_.registered_count(); ++id) {
        if (signature.has(id)) {
            ENGINE_ASSERT(arch->column_count < kMaxColumnsPerArchetype, "column cap");
            arch->columns[arch->column_count].component_id = id;
            ++arch->column_count;
        }
    }
    archetype_build_layout(*arch, registry_);

    archetypes_[archetype_count_] = arch;
    map_insert(hash, archetype_count_);
    ++archetype_count_;
    return *arch;
}

Archetype* World::find_archetype(const Signature& signature) const noexcept {
    const u32 found = map_find(signature, signature.hash());
    if (found == kInvalidIndex) {
        return nullptr;
    }
    return archetypes_[found];
}

Archetype& World::archetype_at(u32 index) noexcept {
    ENGINE_ASSERT(index < archetype_count_, "archetype index");
    return *archetypes_[index];
}

void World::map_insert(u64 hash, u32 index) {
    u32 slot = static_cast<u32>(hash & (kArchetypeMapSlots - 1));
    for (u32 probe = 0; probe < kArchetypeMapSlots; ++probe) {
        MapSlot& s = archetype_map_[slot];
        if (s.index == kInvalidIndex) {
            s.hash  = hash;
            s.index = index;
            return;
        }
        slot = (slot + 1) & (kArchetypeMapSlots - 1);
    }
    ENGINE_PANIC("archetype hash map full");
}

u32 World::map_find(const Signature& signature, u64 hash) const noexcept {
    u32 slot = static_cast<u32>(hash & (kArchetypeMapSlots - 1));
    for (u32 probe = 0; probe < kArchetypeMapSlots; ++probe) {
        const MapSlot& s = archetype_map_[slot];
        if (s.index == kInvalidIndex) {
            return kInvalidIndex;
        }
        if (s.hash == hash && archetypes_[s.index]->signature == signature) {
            return s.index;
        }
        slot = (slot + 1) & (kArchetypeMapSlots - 1);
    }
    return kInvalidIndex;
}

void World::move_entity(Entity entity, Archetype& dest, u32 extra_construct_id, const void* extra_data) {
    EntityRecord& rec = records_[entity.index()];
    Archetype& src    = *archetypes_[rec.location.archetype_index];
    const u32 src_chunk = rec.location.chunk_index;
    const u32 src_row   = rec.location.row;

    const u16 dst_row = archetype_allocate_row(dest, *memory_, entity);
    const u32 dst_chunk_index = static_cast<u32>(dest.chunk_count - 1);
    Chunk* dst_chunk = dest.chunks[dst_chunk_index];
    Chunk* src_chunk_ptr = src.chunks[src_chunk];

    for (u32 c = 0; c < dest.column_count; ++c) {
        const u32 cid = dest.columns[c].component_id;
        void* dst = dst_chunk->bytes + dest.columns[c].offset
                    + static_cast<usize>(dst_row) * dest.columns[c].size;
        const ChunkColumn* src_col = src.find_column(cid);
        if (src_col) {
            void* srcp = src_chunk_ptr->bytes + src_col->offset
                         + static_cast<usize>(src_row) * src_col->size;
            registry_.type(cid).copy(dst, srcp);
        } else if (cid == extra_construct_id && extra_data) {
            registry_.type(cid).copy(dst, extra_data);
        } else {
            registry_.type(cid).construct(dst);
        }
    }

    rec.location.archetype_index = dest.index;
    rec.location.chunk_index     = dst_chunk_index;
    rec.location.row             = dst_row;

    archetype_swap_remove(src, registry_, src_chunk, src_row, records_);
    archetype_release_empty_trailing_chunks(src, *memory_);
}

void World::add_component_blob(Entity entity, u32 component_id, const void* data) {
    EntityRecord& rec = record(entity);
    Archetype& src    = *archetypes_[rec.location.archetype_index];
    ENGINE_ASSERT(!src.signature.has(component_id), "entity already has this component");

    Signature next = src.signature;
    next.set(component_id);

    u32 dest_index = src.find_edge(component_id, true);
    if (dest_index == kInvalidIndex) {
        Archetype& dest = ensure_archetype(next);
        src.remember_edge(component_id, true, dest.index);
        dest.remember_edge(component_id, false, src.index);
        dest_index = dest.index;
    }
    move_entity(entity, *archetypes_[dest_index], component_id, data);
}

void World::remove_component_id(Entity entity, u32 component_id) {
    EntityRecord& rec = record(entity);
    Archetype& src    = *archetypes_[rec.location.archetype_index];
    ENGINE_ASSERT(src.signature.has(component_id), "entity missing this component");

    Signature next = src.signature;
    next.unset(component_id);

    u32 dest_index = src.find_edge(component_id, false);
    if (dest_index == kInvalidIndex) {
        Archetype& dest = ensure_archetype(next);
        src.remember_edge(component_id, false, dest.index);
        dest.remember_edge(component_id, true, src.index);
        dest_index = dest.index;
    }
    move_entity(entity, *archetypes_[dest_index], kInvalidIndex, nullptr);
}

} // namespace engine
