#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

// Tempered architectural glass. Not a generic Destructible / Prop.
struct BreakableWindowComponent {
    u32   mesh_id_intact;
    u32   mesh_id_shattered;   // Pre-baked fractured mesh variant
    float structural_integrity; // 1 = intact, 0 = fully shattered
    u16   fracture_seed;
    u16   _pad;
};

// Pane pose is window-specific (center + normal + half-extents), not a
// generic Transform reuse. Used only for impact vs pane tests.
struct BreakableWindowPose {
    float3 center_ws;
    float3 normal_ws;
    float  half_width;
    float  half_height;
};

// Written once when integrity crosses zero. Presence is the shattered state.
struct WindowShatteredTag {
    u16 fracture_seed;
    u16 shard_count;
};

struct ImpactEvent {
    Entity target_entity;
    float3 impact_point;
    float3 impact_velocity;
    float  mass;
};

struct BreakableWindowSpawnDesc {
    u32    mesh_id_intact    = 0;
    u32    mesh_id_shattered = 0;
    u16    fracture_seed     = 1;
    float3 center_ws         = {0.f, 1.2f, 0.f};
    float3 normal_ws         = {0.f, 0.f, 1.f};
    float  half_width        = 0.6f;
    float  half_height       = 0.7f;
};

[[nodiscard]] Entity instantiate_breakable_window(World& world,
                                                  const InstantiationRequest& request,
                                                  const BreakableWindowSpawnDesc& desc);

void ProcessWindowImpactsSystem(World& world, const ImpactEvent* impacts, u32 impact_count,
                                CommandBuffer& cmd);

} // namespace engine
