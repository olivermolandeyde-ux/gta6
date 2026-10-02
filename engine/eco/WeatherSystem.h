#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

struct WeatherZoneComponent {
    u32   cell_id; // Links to Phase 6 StreamingCellId
    float rain_intensity; // 0.0 to 1.0
    float wind_vector_x;
    float wind_vector_z;
    float ambient_temperature_c;
};

struct SurfaceMaterialComponent {
    u32   mesh_id;
    float base_friction_multiplier; // Dry friction (from Phase 4)
    float current_wetness;          // 0.0 to 1.0 (Dynamic state)
    u32   shader_wetness_param_id;  // Handle to update Phase 2 TireRubber.hlsl
};

struct SurfaceFrictionApplyComponent {
    float final_friction_multiplier;
};

[[nodiscard]] Entity instantiate_weather_zone(World& world, const InstantiationRequest& request,
                                              const WeatherZoneComponent& zone);

[[nodiscard]] Entity instantiate_surface_material(World& world, const InstantiationRequest& request,
                                                  const SurfaceMaterialComponent& surface);

void UpdateWeatherAndSurfaceSystem(World& world, float delta_time, CommandBuffer& cmd);

} // namespace engine
