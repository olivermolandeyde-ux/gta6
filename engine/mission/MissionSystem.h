#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

inline constexpr u32 kMissionNotStarted = 0;
inline constexpr u32 kMissionActive     = 1;
inline constexpr u32 kMissionCompleted  = 2;
inline constexpr u32 kMissionFailed     = 3;

struct MissionDefinitionComponent {
    u32    mission_id;
    char   name[64];
    char   description[256];
    u32    giver_entity_id;
    float3 start_location;
    u32    prerequisite_mission_id;
    u32    reward_money;
    u32    reward_reputation;
    u32    reward_weapon_type_id;
    bool   is_repeatable;
    u32    difficulty_level;
};

struct MissionStateComponent {
    u32   mission_id;
    u32   current_state;
    u32   current_objective_index;
    float time_started;
    float time_limit_s;
    bool  is_timer_running;
};

struct MissionObjectiveComponent {
    u32    mission_id;
    u32    objective_index;
    u8     objective_type; // 0=goto, 1=kill, 2=collect, 3=deliver, 4=escort, 5=steal_vehicle, 6=destroy
    u32    target_entity_id;
    float3 target_location;
    u32    required_count;
    u32    current_count;
    bool   is_completed;
    char   description[128];
};

// Dedicated per-mission-type tag (no generic Mission base).
struct ConvenienceStoreRobberyTag {
    u32 register_cash_usd;
    u32 cop_heat_add;
};

struct MissionGiverInteractComponent {
    u8 requested;
};

struct MissionSimClockComponent {
    float world_time_s;
};

struct MissionTelemetryComponent {
    u32 missions_loaded;
    u32 state_transitions;
    u32 objectives_completed;
    u32 dialog_nodes_traversed;
    u32 rewards_money;
    i32 rewards_reputation;
};

[[nodiscard]] Entity instantiate_mission(World& world, const InstantiationRequest& request,
                                         const MissionDefinitionComponent& def,
                                         const MissionStateComponent& state);

void request_mission_accept(World& world, Entity mission);

void UpdateMissionSystem(World& world, float delta_time, CommandBuffer& cmd);

} // namespace engine
