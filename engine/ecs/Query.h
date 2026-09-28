#pragma once

#include "ecs/Archetype.h"
#include "ecs/Signature.h"

namespace engine {

// Query is a pair of signatures. Matching is "archetype.signature contains
// required AND none of excluded". Iteration is chunk-linear: the inner loop
// is a dense 0..count walk with SoA column pointers — the CPU's favourite
// pattern for 100k pedestrians.
struct Query {
    Signature required{};
    Signature excluded{};

    [[nodiscard]] bool matches(const Signature& archetype_sig) const noexcept {
        return archetype_sig.contains(required) && archetype_sig.none_of(excluded);
    }
};

class World;

class EntityQueryIterator {
public:
    EntityQueryIterator() = default;
    EntityQueryIterator(World* world, Query query, bool is_end);

    [[nodiscard]] Entity operator*() const noexcept { return value_; }
    EntityQueryIterator& operator++();
    [[nodiscard]] bool operator!=(const EntityQueryIterator& other) const noexcept;
    [[nodiscard]] bool operator==(const EntityQueryIterator& other) const noexcept {
        return !(*this != other);
    }

private:
    void seek();

    World* world_ = nullptr;
    Query  query_{};
    u32    arch_  = 0;
    u16    chunk_ = 0;
    u16    row_   = 0;
    bool   end_   = true;
    Entity value_{};
};

struct EntityQueryRange {
    World* world = nullptr;
    Query  query{};

    [[nodiscard]] EntityQueryIterator begin() const {
        return EntityQueryIterator(world, query, false);
    }
    [[nodiscard]] EntityQueryIterator end() const {
        return EntityQueryIterator(world, query, true);
    }
};

struct ChunkView {
    Archetype* archetype = nullptr;
    Chunk*     chunk     = nullptr;
    u16        count     = 0;

    [[nodiscard]] Entity entity(u16 row) const noexcept {
        return chunk->entities()[row];
    }

    template <typename T>
    [[nodiscard]] T* column(u32 component_id) const noexcept {
        const ChunkColumn* col = archetype->find_column(component_id);
        if (!col) {
            return nullptr;
        }
        return reinterpret_cast<T*>(chunk->bytes + col->offset);
    }
};

} // namespace engine
