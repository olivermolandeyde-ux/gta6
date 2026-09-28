#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

// Interconnected city utilities. Links back to StreetLightComponent from Phase 3.
struct PowerGridNodeComponent {
    u32    node_id;
    float3 world_position;
    float  current_load; // 0.0 to 1.0
    float  max_capacity;
    bool   is_operational;
    u32    connected_lights[32]; // Array of StreetLight entity IDs powered by this node
    u32    connected_light_count;
};

struct PowerGridEvent {
    u32 damaged_node_id; // e.g., a player shot a transformer
};

[[nodiscard]] Entity instantiate_power_grid_node(World& world, const InstantiationRequest& request,
                                                 const PowerGridNodeComponent& node);

void UpdatePowerGridSystem(World& world, const PowerGridEvent* events, u32 event_count,
                           CommandBuffer& cmd);

} // namespace engine
