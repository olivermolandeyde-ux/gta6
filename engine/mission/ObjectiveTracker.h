#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

struct ObjectiveTrackerComponent {
    u32    mission_id;
    u32    objective_index;
    u8     tracker_type; // 0=entity_distance, 1=entity_dead, 2=item_collected, 3=vehicle_entered, 4=location_reached
    u32    target_entity_id;
    float3 target_location;
    float  trigger_distance_m;
    bool   is_triggered;
};

struct GotoObjectiveComponent {
    u32    mission_id;
    u32    objective_index;
    float3 target_location;
    float  radius_m;
    bool   is_completed;
};

struct KillObjectiveComponent {
    u32  mission_id;
    u32  objective_index;
    u32  target_entity_id;
    u32  required_count;
    u32  current_count;
    bool is_completed;
};

struct CollectObjectiveComponent {
    u32  mission_id;
    u32  objective_index;
    u32  item_type_id;
    u32  required_count;
    bool is_completed;
};

struct DeliverObjectiveComponent {
    u32    mission_id;
    u32    objective_index;
    u32    item_type_id;
    float3 dropoff_ws;
    float  radius_m;
    bool   is_completed;
};

struct EscortObjectiveComponent {
    u32    mission_id;
    u32    objective_index;
    u32    escort_entity_id;
    float3 destination_ws;
    float  radius_m;
    bool   is_completed;
};

struct StealVehicleObjectiveComponent {
    u32  mission_id;
    u32  objective_index;
    u32  chassis_entity_id;
    bool is_completed;
};

struct DestroyObjectiveComponent {
    u32  mission_id;
    u32  objective_index;
    u32  target_entity_id;
    bool is_completed;
};

struct ThreatenAimObjectiveComponent {
    u32   mission_id;
    u32   objective_index;
    u32   clerk_entity_id;
    float aim_dot_min;
    float max_range_m;
    bool  is_completed;
};

struct FleshHealthComponent {
    float health;
    float max_health;
};

struct StoreClerkPoseComponent {
    float3 position_ws;
};

struct MissionGiverPoseComponent {
    float3 position_ws;
};

[[nodiscard]] Entity instantiate_objective_tracker(World& world, const InstantiationRequest& request,
                                                   const ObjectiveTrackerComponent& tracker);

void UpdateObjectiveTrackerSystem(World& world, float delta_time, CommandBuffer& cmd);

} // namespace engine
