#include "eco/WeatherSystem.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "memory/FrameAllocator.h"
#include "memory/MemorySystem.h"
#include "physics/VehicleDynamics.h"

namespace engine {

Entity instantiate_weather_zone(World& world, const InstantiationRequest& request,
                                const WeatherZoneComponent& zone) {
    validate_instantiation(request);
    return world.instantiate(request, zone);
}

Entity instantiate_surface_material(World& world, const InstantiationRequest& request,
                                    const SurfaceMaterialComponent& surface) {
    validate_instantiation(request);
    SurfaceFrictionApplyComponent apply{};
    apply.final_friction_multiplier = surface.base_friction_multiplier;
    return world.instantiate(request, surface, apply);
}

void UpdateWeatherAndSurfaceSystem(World& world, float delta_time, CommandBuffer& cmd) {
    (void)cmd;
    ENGINE_ASSERT(delta_time >= 0.f && delta_time < 0.25f, "weather dt");

    FrameAllocator& frame = world.memory().frame();
    const u32 max_entities = world.entity_count();

    Entity* zones = frame.allocate_array<Entity>(max_entities);
    u32 n_zones = 0;
    for (Entity e : world.query<WeatherZoneComponent>()) {
        if (n_zones < max_entities) {
            zones[n_zones++] = e;
        }
    }

    float rain = 0.f;
    if (n_zones > 0) {
        WeatherZoneComponent* z = world.get<WeatherZoneComponent>(zones[0]);
        ENGINE_ASSERT(z != nullptr, "weather zone stale");
        rain = clampf(z->rain_intensity, 0.f, 1.f);
    }

    Entity* surfaces = frame.allocate_array<Entity>(max_entities);
    u32 n_surfaces = 0;
    for (Entity e : world.query<SurfaceMaterialComponent>()) {
        if (n_surfaces < max_entities) {
            surfaces[n_surfaces++] = e;
        }
    }

    for (u32 i = 0; i < n_surfaces; ++i) {
        SurfaceMaterialComponent* s = world.get<SurfaceMaterialComponent>(surfaces[i]);
        SurfaceFrictionApplyComponent* a = world.get<SurfaceFrictionApplyComponent>(surfaces[i]);
        ENGINE_ASSERT(s != nullptr, "surface stale");

        // Wetness chases rain intensity (asphalt soaks, then evaporates slowly).
        const float chase = (rain > s->current_wetness) ? 2.5f : 0.15f;
        s->current_wetness += (rain - s->current_wetness) * clampf(chase * delta_time, 0.f, 1.f);
        s->current_wetness = clampf(s->current_wetness, 0.f, 1.f);

        // final_friction = base * (1.0 - wetness * 0.4)  — 40% loss at full rain.
        const float final_mu = s->base_friction_multiplier * (1.0f - s->current_wetness * 0.4f);
        if (a) {
            a->final_friction_multiplier = final_mu;
        }
    }

    float applied_mu = 1.0f;
    if (n_surfaces > 0) {
        if (const SurfaceFrictionApplyComponent* a =
                world.get<SurfaceFrictionApplyComponent>(surfaces[0])) {
            applied_mu = a->final_friction_multiplier;
        }
    }

    for (Entity e : world.query<VehicleTireContactComponent>()) {
        VehicleTireContactComponent* tire = world.get<VehicleTireContactComponent>(e);
        if (tire) {
            tire->friction_mu = applied_mu;
        }
    }
}

} // namespace engine
