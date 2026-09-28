#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

struct CoopMissionRuleComponent {
    u32 session_id;
    u32 mission_id;
    u8  player_difficulty_scale;
    u32 required_objectives;
};

struct PlayerReviveRequest {
    u32   downed_player_entity_id;
    u32   reviving_player_entity_id;
    float revive_progress;
};

struct CoopDownedComponent {
    u32   player_entity_id;
    bool  is_downed;
    float revive_progress;
};

[[nodiscard]] Entity instantiate_coop_rules(World& world, const InstantiationRequest& request,
                                            const CoopMissionRuleComponent& rules);

void ProcessCoopMissionSystem(World& world, float delta_time, const PlayerReviveRequest* requests,
                              u32 request_count, CommandBuffer& cmd);

} // namespace engine
