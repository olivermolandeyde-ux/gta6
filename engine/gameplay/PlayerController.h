#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

struct PlayerStateComponent {
    float health;
    float max_health;
    float armor;
    float max_armor;
    float stamina;
    float max_stamina;
    float wanted_level;
    u32   current_weapon_entity_id;
    u32   current_vehicle_entity_id;
    float3 camera_position;
    float3 camera_forward;
    float  move_speed_penalty;
};

struct DamageEventComponent {
    u32   source_entity_id;
    u32   target_entity_id;
    float damage_amount;
    u32   damage_type; // 0=bullet, 1=explosion, 2=melee, 3=fall, 4=vehicle
    float3 hit_position;
    float3 hit_normal;
    u32   body_part; // 0=torso, 1=head, 2=limb
};

[[nodiscard]] Entity instantiate_player(World& world, const InstantiationRequest& request,
                                        const PlayerStateComponent& state);

void enqueue_damage(World& world, const DamageEventComponent& evt);

void UpdatePlayerController(World& world, float delta_time, CommandBuffer& cmd,
                            FrameAllocator& frame_alloc);

} // namespace engine
