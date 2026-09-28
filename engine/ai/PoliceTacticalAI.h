#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

struct PoliceOfficerComponent {
    u32    officer_entity_id;
    u32    squad_id;
    u8     role; // 0=point, 1=cover, 2=flanker, 3=sniper, 4=k9_handler
    float3 last_known_player_pos;
    float  aggression_level;
    u32    current_target_entity_id;
    bool   has_line_of_sight;
    float  accuracy_modifier;
    u32    weapon_entity_id;
    float  suppress_timer_s;
    bool   is_in_cover;
    float3 cover_position;
};

struct PoliceOfficerPoseComponent {
    float3 position_ws;
    float3 velocity_ws;
};

struct PoliceSquadComponent {
    u32    squad_id;
    u32    officer_ids[8];
    u32    officer_count;
    u32    target_player_entity_id;
    u8     squad_state; // 0=searching, 1=engaging, 2=flanking, 3=retreating
    float3 formation_center;
    float  squad_cohesion;
};

struct PoliceHelicopterComponent {
    u32    helicopter_entity_id;
    u32    target_player_entity_id;
    float3 pursuit_position;
    float  spotlight_intensity;
    bool   has_visual_on_player;
    float  altitude_m;
    float3 velocity;
};

struct K9UnitComponent {
    u32    dog_entity_id;
    u32    handler_entity_id;
    u32    target_player_entity_id;
    float3 last_known_scent_pos;
    float  scent_strength;
    bool   is_tracking;
    float  speed_multiplier;
};

struct TacticalCoverSlabComponent {
    float3 min_ws;
    float3 max_ws;
};

struct PlayerScentTrailComponent {
    float3 samples[16];
    float  strength[16];
    u32    count;
    u32    head;
};

struct PoliceDispatchCallComponent {
    float3 location_ws;
    u32    caller_entity_id;
    float  severity;
    u8     consumed;
};

[[nodiscard]] Entity instantiate_police_officer(World& world, const InstantiationRequest& request,
                                                const PoliceOfficerComponent& officer,
                                                float3 position_ws);

[[nodiscard]] Entity instantiate_police_squad(World& world, const InstantiationRequest& request,
                                              const PoliceSquadComponent& squad);

[[nodiscard]] Entity instantiate_police_helicopter(World& world, const InstantiationRequest& request,
                                                   const PoliceHelicopterComponent& heli,
                                                   float3 position_ws);

[[nodiscard]] Entity instantiate_k9_unit(World& world, const InstantiationRequest& request,
                                         const K9UnitComponent& k9, float3 position_ws);

void UpdatePoliceTacticalAISystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                                  CommandBuffer& cmd);

} // namespace engine
