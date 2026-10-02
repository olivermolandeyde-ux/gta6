#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

inline constexpr u32 kPedestrianStateCalm    = 0;
inline constexpr u32 kPedestrianStateAlerted = 1;
inline constexpr u32 kPedestrianStateFleeing = 2;

// Civilian reaction brain. Not a generic AIController / Character.
struct PedestrianBehaviorComponent {
    u32    state_id;              // 0=Calm, 1=Alerted, 2=Fleeing
    float  fear_level;            // 0.0 to 1.0
    float3 threat_position;       // Last known position of danger
    u32    destination_node_id;   // Handle to navigation mesh node to flee to
    float  reaction_delay_timer;  // Simulates human shock before fleeing
};

struct PedestrianWorldPose {
    float3 position_ws;
    float3 facing_ws;
};

// Gunshot / engine backfire / crash. Pedestrians sample these by handle, not pointer.
struct PedestrianThreatStimulus {
    float3 position_ws;
    float  loudness;   // 0..1
    float  radius_m;
};

struct PedestrianNavNodeComponent {
    u32    node_id;
    float3 position_ws;
};

struct PedestrianFleeingTag {
    u32   destination_node_id;
    float fear_at_trigger;
};

struct PedestrianSpawnDesc {
    float3 position_ws          = {0.f, 0.f, 0.f};
    float3 facing_ws            = {0.f, 0.f, 1.f};
    float  reaction_delay_s     = 0.35f;
    u32    destination_node_id  = 0;
};

[[nodiscard]] Entity instantiate_pedestrian(World& world, const InstantiationRequest& request,
                                            const PedestrianSpawnDesc& desc);

[[nodiscard]] Entity instantiate_pedestrian_threat(World& world, const InstantiationRequest& request,
                                                   const PedestrianThreatStimulus& stimulus);

[[nodiscard]] Entity instantiate_pedestrian_nav_node(World& world, const InstantiationRequest& request,
                                                     u32 node_id, float3 position_ws);

void UpdatePedestrianAISystem(World& world, float delta_time, CommandBuffer& cmd,
                              FrameAllocator& frame_alloc);

} // namespace engine
