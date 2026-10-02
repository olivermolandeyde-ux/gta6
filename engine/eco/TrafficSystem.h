#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

// Highly specific logic for city traffic. No generic "AI Driver".
struct TrafficLaneNodeComponent {
    u32    node_id;
    float3 world_position;
    float3 forward_direction;
    float  speed_limit_m_s;
    u32    next_node_id;       // Graph traversal
    u32    left_lane_node_id;  // 0 if none
    u32    right_lane_node_id; // 0 if none
};

struct TrafficVehicleComponent {
    u32   vehicle_chassis_entity_id; // Link to Phase 4 physics
    u32   current_lane_node_id;
    float target_speed_m_s;
    float current_speed_m_s;
    float follow_distance_m;         // Radar distance to car in front
    u32   vehicle_in_front_entity_id; // 0 if empty road
};

struct TrafficVehiclePose {
    float3 position_ws;
    float3 heading_ws;
};

[[nodiscard]] Entity instantiate_traffic_lane(World& world, const InstantiationRequest& request,
                                              const TrafficLaneNodeComponent& node);

[[nodiscard]] Entity instantiate_traffic_vehicle(World& world, const InstantiationRequest& request,
                                                 u32 chassis_entity_id, u32 lane_node_id,
                                                 float3 position_ws, float speed_m_s);

void UpdateTrafficSystemSystem(World& world, float delta_time, FrameAllocator& frame_alloc);

} // namespace engine
