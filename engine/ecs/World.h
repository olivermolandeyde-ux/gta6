#pragma once

#include "core/Assert.h"
#include "ecs/Archetype.h"
#include "ecs/CommandBuffer.h"
#include "ecs/ComponentRegistry.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "ecs/Query.h"

namespace engine {

class MemorySystem;

inline constexpr u32 kArchetypeMapSlots = 8192;

class World {
public:
    void boot(MemorySystem& memory, u16 world_id, u32 entity_capacity = kMaxEntitiesHint);
    void shutdown();
    void begin_frame(u64 frame_index);

    [[nodiscard]] MemorySystem&       memory() noexcept { return *memory_; }
    [[nodiscard]] const MemorySystem& memory() const noexcept { return *memory_; }
    [[nodiscard]] ComponentRegistry&  registry() noexcept { return registry_; }
    [[nodiscard]] u16                 world_id() const noexcept { return world_id_; }
    [[nodiscard]] u32                 live_entity_count() const noexcept { return live_entities_; }
    [[nodiscard]] u32                 entity_count() const noexcept { return live_entities_; }
    [[nodiscard]] u32                 archetype_count() const noexcept { return archetype_count_; }
    [[nodiscard]] u64                 frame_index() const noexcept { return frame_index_; }

    [[nodiscard]] bool is_alive(Entity entity) const noexcept;
    [[nodiscard]] EntityRecord&       record(Entity entity);
    [[nodiscard]] const EntityRecord& record(Entity entity) const;

    // Bare entity, no components. Prefer the typed instantiate() below.
    [[nodiscard]] Entity create_empty(const InstantiationRequest& request);

    void destroy(Entity entity);

    template <typename T>
    void add_component(Entity entity, const T& value);

    template <typename T>
    void remove_component(Entity entity);

    template <typename T>
    [[nodiscard]] T* get(Entity entity);

    template <typename T>
    [[nodiscard]] const T* get(Entity entity) const;

    template <typename T>
    [[nodiscard]] bool has(Entity entity) const;

    template <typename... Cs>
    [[nodiscard]] Entity instantiate(const InstantiationRequest& request, const Cs&... components);

    template <typename Fn>
    void for_each_chunk(const Query& query, Fn&& fn);

    template <typename... Cs>
    [[nodiscard]] EntityQueryRange query();

    [[nodiscard]] Archetype* find_archetype(const Signature& signature) const noexcept;
    [[nodiscard]] Archetype& archetype_at(u32 index) noexcept;

    void add_component_blob(Entity entity, u32 component_id, const void* data);
    void remove_component_id(Entity entity, u32 component_id);

    [[nodiscard]] CommandBuffer& frame_commands() noexcept { return frame_commands_; }

private:
    struct MapSlot {
        u64 hash;
        u32 index;
        u32 _pad;
    };

    [[nodiscard]] u32  acquire_index();
    void               release_index(u32 index);
    [[nodiscard]] Archetype& ensure_archetype(const Signature& signature);
    void               move_entity(Entity entity, Archetype& dest, u32 extra_construct_id, const void* extra_data);
    void               map_insert(u64 hash, u32 index);
    [[nodiscard]] u32  map_find(const Signature& signature, u64 hash) const noexcept;

    MemorySystem*      memory_ = nullptr;
    ComponentRegistry  registry_{};
    u16                world_id_ = 0;
    u64                frame_index_ = 0;

    Archetype*         archetypes_[kMaxArchetypes]{};
    u32                archetype_count_ = 0;
    MapSlot            archetype_map_[kArchetypeMapSlots]{};

    EntityRecord*      records_ = nullptr;
    u32*               free_indices_ = nullptr;
    u32                entity_capacity_ = 0;
    u32                free_count_ = 0;
    u32                next_index_ = 1; // 0 reserved for kNullEntity
    u32                live_entities_ = 0;

    CommandBuffer      frame_commands_{};
    void*              cmd_block_ = nullptr;
    void*              cmd_payload_ = nullptr;
    void*              cmd_gpu_ = nullptr;
    void*              cmd_gpu_payload_ = nullptr;
};

} // namespace engine

// ---------------------------------------------------------------------------
// Template implementations. Kept next to the class so call sites stay readable.
// ---------------------------------------------------------------------------

namespace engine {

template <typename T>
void World::add_component(Entity entity, const T& value) {
    const u32 id = registry_.id_of<T>();
    add_component_blob(entity, id, &value);
}

template <typename T>
void World::remove_component(Entity entity) {
    const u32 id = registry_.id_of<T>();
    remove_component_id(entity, id);
}

template <typename T>
T* World::get(Entity entity) {
    const EntityRecord& rec = record(entity);
    Archetype& arch = *archetypes_[rec.location.archetype_index];
    const ChunkColumn* col = arch.find_column(registry_.id_of<T>());
    if (!col) {
        return nullptr;
    }
    Chunk* chunk = arch.chunks[rec.location.chunk_index];
    return reinterpret_cast<T*>(chunk->bytes + col->offset
                                + static_cast<usize>(rec.location.row) * col->size);
}

template <typename T>
const T* World::get(Entity entity) const {
    return const_cast<World*>(this)->get<T>(entity);
}

template <typename T>
bool World::has(Entity entity) const {
    const EntityRecord& rec = record(entity);
    const Archetype& arch = *archetypes_[rec.location.archetype_index];
    return arch.signature.has(const_cast<ComponentRegistry&>(registry_).id_of<T>());
}

template <typename... Cs>
Entity World::instantiate(const InstantiationRequest& request, const Cs&... components) {
    validate_instantiation(request);
    Entity entity = create_empty(request);
    (add_component<Cs>(entity, components), ...);
    if (request.domain == InstantiationDomain::StreamingCell) {
        add_component<StreamingCellId>(entity, request.cell);
    }
    return entity;
}

template <typename... Cs>
EntityQueryRange World::query() {
    Query q{};
    q.required.clear();
    q.excluded.clear();
    (q.required.set(registry_.id_of<Cs>()), ...);
    return EntityQueryRange{this, q};
}

template <typename Fn>
void World::for_each_chunk(const Query& query, Fn&& fn) {
    for (u32 a = 0; a < archetype_count_; ++a) {
        Archetype* arch = archetypes_[a];
        if (!query.matches(arch->signature)) {
            continue;
        }
        for (u16 c = 0; c < arch->chunk_count; ++c) {
            Chunk* chunk = arch->chunks[c];
            if (chunk->header().count == 0) {
                continue;
            }
            ChunkView view{};
            view.archetype = arch;
            view.chunk     = chunk;
            view.count     = chunk->header().count;
            fn(view);
        }
    }
}

} // namespace engine
