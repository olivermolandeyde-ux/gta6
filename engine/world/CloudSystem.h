#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"

namespace engine {

class World;

struct CloudLayerComponent {
    float3 layer_position;  // Y = altitude
    float  layer_thickness;
    float  cloud_coverage;  // 0..1
    float3 wind_velocity;
    u32    noise_texture_id;
};

struct CloudRayRgba {
    float x, y, z, w;
};

struct CloudRayMarch {
    // Beer-Lambert: transmittance = exp(-extinction * distance)
    CloudRayRgba rayMarchClouds(float3 ray_origin, float3 ray_dir, float max_distance,
                                const CloudLayerComponent& clouds) const;
};

void UpdateCloudSystem(World& world, float delta_time, CommandBuffer& cmd);

[[nodiscard]] CloudLayerComponent* find_cloud_layer(World& world);

} // namespace engine
