#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

struct ProjectileComponent {
    u32    weapon_entity_id;
    u32    ammo_type_id;
    float3 position;
    float3 velocity;
    float  mass_kg;
    float  drag_coefficient;
    float  penetration_power;
    float  remaining_energy_j;
    float  time_alive_s;
    u32    material_hit; // 0=none, 1=flesh, 2=metal, 3=glass, 4=concrete
};

struct AmmoTypeDefinition {
    u32  type_id;
    char name[32];
    float muzzle_velocity_ms;
    float projectile_mass_kg;
    float drag_coefficient;
    float penetration_mm_rha;
    float damage_base;
    float damage_falloff_per_meter;
};

struct BallisticMaterialSlab {
    float3 min_ws;
    float3 max_ws;
    u32    material;       // 1-4
    float  thickness_mm;
    float  hardness;       // ricochet threshold
};

[[nodiscard]] Entity instantiate_ammo_type(World& world, const InstantiationRequest& request,
                                           const AmmoTypeDefinition& def);

[[nodiscard]] Entity spawn_projectile(World& world, const InstantiationRequest& request,
                                      const ProjectileComponent& projectile);

void UpdateBallisticsSystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                            CommandBuffer& cmd);

} // namespace engine
