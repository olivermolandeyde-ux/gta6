#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

struct DeathmatchRuleComponent {
    u32    session_id;
    u32    score_limit;
    float  time_limit_s;
    u32    respawn_delay_s;
    float3 spawn_points[16];
    u32    spawn_point_count;
};

struct KillEvent {
    u32    victim_entity_id;
    u32    killer_entity_id;
    u32    weapon_used_id;
    float3 kill_location;
};

[[nodiscard]] Entity instantiate_deathmatch_rules(World& world, const InstantiationRequest& request,
                                                  const DeathmatchRuleComponent& rules);

void ProcessDeathmatchEventsSystem(World& world, const KillEvent* events, u32 event_count,
                                   CommandBuffer& cmd);

} // namespace engine
