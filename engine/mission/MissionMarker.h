#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

struct MissionMarkerComponent {
    u32    mission_id;
    u32    objective_index;
    float3 world_position;
    u8     marker_type; // 0=blip, 1=3d_icon, 2=checkpoint
    u32    icon_id;
    float3 color_rgb;
    bool   is_visible;
    float  pulse_frequency_hz;
};

[[nodiscard]] Entity instantiate_mission_marker(World& world, const InstantiationRequest& request,
                                                const MissionMarkerComponent& marker);

void UpdateMissionMarkerSystem(World& world, float delta_time, FrameAllocator& frame_alloc);

} // namespace engine
