#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

struct NavigationAgentComponent {
    u32    agent_entity_id;
    float3 current_position;
    float3 target_position;
    float3 current_velocity;
    float  max_speed_ms;
    u8     navigation_mode; // 0=walking, 1=running, 2=fleeing, 3=patrolling
    bool   is_avoiding_danger;
    float3 danger_position;
    float  danger_radius_m;
    u32    path_node_ids[32];
    u32    path_node_count;
    u32    current_path_index;
};

struct DangerZoneComponent {
    u32    zone_id;
    float3 center_position;
    float  radius_m;
    u8     danger_type; // 0=shootout, 1=fire, 2=police_activity, 3=gang_territory
    float  severity;
    float  time_created;
    float  duration_s;
};

struct StreetPathNodeComponent {
    u32    node_id;
    float3 position_ws;
    u32    neighbor_ids[4];
    u32    neighbor_count;
};

[[nodiscard]] Entity instantiate_navigation_agent(World& world, const InstantiationRequest& request,
                                                  const NavigationAgentComponent& agent);

[[nodiscard]] Entity instantiate_danger_zone(World& world, const InstantiationRequest& request,
                                             const DangerZoneComponent& zone);

void UpdateAdvancedNavigationSystem(World& world, float delta_time, FrameAllocator& frame_alloc);

} // namespace engine
