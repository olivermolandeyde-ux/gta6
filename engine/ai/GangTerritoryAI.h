#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

inline constexpr u32 kGangNeutral = 0;
inline constexpr u32 kGangBallas  = 1;
inline constexpr u32 kGangGrove   = 2;
inline constexpr u32 kGangVagos   = 3;

struct GangTerritoryComponent {
    u32    territory_id;
    float3 center_position;
    float  radius_m;
    u32    controlling_gang_id;
    float  influence_level;
    u32    last_contested_time;
};

struct GangMemberComponent {
    u32    member_entity_id;
    u32    gang_id;
    u32    home_territory_id;
    float  aggression_toward_player;
    bool   is_patrolling;
    float3 patrol_route[8];
    u32    patrol_route_count;
    u32    weapon_entity_id;
};

struct GangMemberPoseComponent {
    float3 position_ws;
    u32    patrol_index;
};

struct GangTreasuryComponent {
    u32 gang_id;
    u32 cash_usd;
};

struct TurfWarEvent {
    u32    territory_id;
    u32    attacker_gang_id;
    u32    defender_gang_id;
    float3 battle_center;
    float  battle_radius_m;
};

struct ActiveTurfWarComponent {
    u32    territory_id;
    u32    attacker_gang_id;
    u32    defender_gang_id;
    float3 battle_center;
    float  battle_radius_m;
    u8     resolved;
    u32    winner_gang_id;
};

[[nodiscard]] Entity instantiate_gang_territory(World& world, const InstantiationRequest& request,
                                                const GangTerritoryComponent& territory);

[[nodiscard]] Entity instantiate_gang_member(World& world, const InstantiationRequest& request,
                                             const GangMemberComponent& member, float3 position_ws);

void UpdateGangTerritoryAISystem(World& world, float delta_time, const TurfWarEvent* events,
                                 u32 event_count, CommandBuffer& cmd);

} // namespace engine
