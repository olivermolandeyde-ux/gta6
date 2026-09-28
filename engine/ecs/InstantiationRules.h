#pragma once

#include "core/Assert.h"
#include "ecs/Entity.h"

namespace engine {

// =============================================================================
// INSTANTIATION LAWS — MICRO-PHASE 1
// =============================================================================
// These are not guidelines. The World API is the only legal way to bring a
// simulated object into existence. Break them and you re-introduce the hitch
// class this allocator/ECS exists to kill.
//
//  L1. No `new`, `delete`, `malloc`, `shared_ptr`, `unique_ptr` for entities
//      or components. The CRT heap is closed after MemorySystem::boot().
//
//  L2. An entity is a 64-bit generational handle. It is never a pointer.
//      Pointers to components are valid only until the next structural change
//      on THAT entity (add/remove component) or a swap-remove in its chunk.
//      Do not stash component pointers across frames.
//
//  L3. Components are POD-like data. They do not allocate. If a street lamp
//      needs a flicker timeline, it stores a handle into a flicker table that
//      lives in the world arena — not a std::vector on the component.
//
//  L4. InstantiationDomain::TransientFrame is ILLEGAL for entities. Frame
//      memory dies at begin_frame(). Entities must be PersistentWorld or
//      StreamingCell.
//
//  L5. StreamingCell entities MUST carry a StreamingCellId component so the
//      unloader can destroy them in bulk when the cell pages out. Persistent
//      entities MUST NOT carry StreamingCellId.
//
//  L6. Structural changes during chunk iteration invalidate the iterator.
//      Use CommandBuffer (deferred) or iterate a query snapshot allocated
//      from the frame arena.
//
//  L7. Prefabs are baked blobs: a Signature plus tightly packed component
//      bytes. Instantiating a prefab is memcpy into a chunk row, then a
//      patch of Entity-specific fields (transform, seed). No constructors
//      that hide allocations.
//
//  L8. One live World per simulation. Handles embed world_id; mixing handles
//      across worlds is a fatal assert, not a silent no-op.
// =============================================================================

struct StreamingCellId {
    u32 cell_x = 0;
    u32 cell_y = 0;
    u16 lod    = 0;
    u16 _pad   = 0;
};

struct InstantiationRequest {
    InstantiationDomain domain      = InstantiationDomain::PersistentWorld;
    StreamingCellId     cell        = {};
    const char*         debug_label = "unnamed_entity";
};

inline void validate_instantiation(const InstantiationRequest& request) {
    ENGINE_ASSERT(request.domain != InstantiationDomain::TransientFrame,
                  "L4 violated: entities cannot live in the frame allocator");
    ENGINE_ASSERT(request.debug_label != nullptr, "instantiation requires a debug label");
    if (request.domain == InstantiationDomain::StreamingCell) {
        ENGINE_ASSERT(request.cell.lod < 8, "streaming LOD out of range");
    }
}

} // namespace engine
