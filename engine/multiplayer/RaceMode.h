#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

struct RaceRuleComponent {
    u32    session_id;
    u32    total_laps;
    float3 checkpoint_positions[32];
    float  checkpoint_radius_m;
    u32    checkpoint_count;
};

struct PlayerRaceStateComponent {
    u32    player_entity_id;
    u32    current_lap;
    u32    next_checkpoint_index;
    float  lap_time_s;
    float  best_lap_time_s;
    float3 last_valid_position;
};

struct CheckpointTriggerEvent {
    u32    player_entity_id;
    u32    checkpoint_index;
    float3 trigger_position;
};

[[nodiscard]] Entity instantiate_race_rules(World& world, const InstantiationRequest& request,
                                            const RaceRuleComponent& rules);

void ProcessRaceEventsSystem(World& world, const CheckpointTriggerEvent* events, u32 event_count,
                             CommandBuffer& cmd);

} // namespace engine
