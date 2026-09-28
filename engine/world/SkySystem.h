#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"

namespace engine {

class World;

struct SkyComponent {
    float3 sun_direction;
    float3 sun_color;
    float3 moon_direction;
    float  time_of_day; // 0..24
    float  turbidity;   // 2 clear .. 10 hazy
    float3 zenith_color;
    float3 horizon_color;
};

struct AtmosphericScattering {
    // Rayleigh (blue) + Mie (white haze). Wavelengths ~ 680/550/440 nm mapped RGB.
    float3 calculateSkyColor(float3 view_dir, float3 sun_dir, float turbidity) const;
};

void UpdateSkySystem(World& world, float delta_time, CommandBuffer& cmd);

[[nodiscard]] SkyComponent* find_sky(World& world);

} // namespace engine
