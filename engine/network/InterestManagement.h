#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"
#include "network/NetworkCore.h"

namespace engine {

class World;

// Area of Interest (AOI) system. Ties directly into Phase 6's StreamingCellId.
// We DO NOT send entities to a client unless they are in the same or adjacent streaming cells.
struct NetworkIdentityComponent {
    u32             network_id;
    StreamingCellId cell_id; // Replicated from Phase 1/6
    bool            is_player_controlled;
};

struct AOIQueryResult {
    Entity* entities; // Allocated in FrameAllocator
    u32     count;
};

[[nodiscard]] Entity attach_network_identity(World& world, Entity entity, u32 network_id,
                                             StreamingCellId cell, bool player_controlled);

// For a given client, queries entities whose StreamingCellId is within cell_radius.
// Uses a cell-key hash built once per call (O(N) build, O(radius²) gather) — not a
// per-entity world scan per neighbour cell.
AOIQueryResult CalculateAreaOfInterest(World& world, const NetworkEndpoint& client,
                                       u32 cell_radius, FrameAllocator& frame_alloc);

[[nodiscard]] bool aoi_contains(const AOIQueryResult& aoi, Entity entity);

} // namespace engine
