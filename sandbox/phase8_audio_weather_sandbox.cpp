#include "Engine.h"
#include "audio/AudioEngine.h"
#include "eco/WeatherSystem.h"
#include "vfx/ParticleSystem.h"

#include <cmath>
#include <cstdio>

int main() {
    using namespace engine;

    MemoryBudget budget{};
    budget.world_arena_bytes     = 32ull * 1024ull * 1024ull;
    budget.streaming_arena_bytes = 8ull * 1024ull * 1024ull;
    budget.chunk_pool_bytes      = 16ull * 1024ull * 1024ull;
    budget.record_pool_bytes     = 4ull * 1024ull * 1024ull;
    budget.meta_pool_bytes       = 1ull * 1024ull * 1024ull;
    budget.stack_bytes           = 1ull * 1024ull * 1024ull;
    budget.frame_bytes           = 4ull * 1024ull * 1024ull;

    Engine engine;
    engine.boot(budget, /*world_id=*/1);
    World& world = engine.world();

    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "phase8";

    AudioListenerComponent listener{};
    listener.entity_id        = 1;
    listener.position         = float3{0.f, 1.6f, 0.f};
    listener.velocity         = float3{20.f, 0.f, 0.f}; // drive-by
    listener.forward_vector   = float3{0.f, 0.f, 1.f};
    listener.up_vector        = float3{0.f, 1.f, 0.f};
    listener.is_inside_vehicle = true;
    (void)instantiate_audio_listener(world, req, listener);

    AudioEmitterComponent gun{};
    gun.sound_event_id          = 9001;
    gun.position                = float3{6.f, 1.6f, 10.f};
    gun.velocity                = float3{0.f, 0.f, 0.f};
    gun.volume_scalar           = 1.0f;
    gun.material_occlusion_mask = 1;
    Entity emitter = instantiate_audio_emitter(world, req, gun, /*source_hz=*/1200.f);

    AudioOccluderSlab wall{};
    wall.min_ws        = float3{-2.f, 0.f, 4.0f};
    wall.max_ws        = float3{8.f, 3.2f, 5.5f};
    wall.material_mask = 1;
    (void)instantiate_audio_occluder(world, req, wall);

    world.begin_frame(1);
    UpdateAudioSpatializationSystem(world, 1.f / 60.f, engine.memory().frame());
    const AudioMixResultComponent* mix = world.get<AudioMixResultComponent>(emitter);
    ENGINE_ASSERT(mix != nullptr, "mix missing");
    ENGINE_ASSERT(mix->observed_hz != 1200.f, "Doppler did not shift");
    ENGINE_ASSERT(mix->lowpass_scalar < 0.5f, "occlusion/interior low-pass missing");
    ENGINE_ASSERT(mix->occluded_volume < 0.5f, "wall did not occlude");

    WeatherZoneComponent zone{};
    zone.cell_id               = 0;
    zone.rain_intensity        = 1.0f;
    zone.wind_vector_x         = 18.0f;
    zone.wind_vector_z         = 0.0f;
    zone.ambient_temperature_c = 12.0f;
    (void)instantiate_weather_zone(world, req, zone);

    SurfaceMaterialComponent asphalt{};
    asphalt.mesh_id                   = 77;
    asphalt.base_friction_multiplier  = 1.0f;
    asphalt.current_wetness           = 0.0f;
    asphalt.shader_wetness_param_id   = 4;
    Entity surface = instantiate_surface_material(world, req, asphalt);

    for (u32 f = 0; f < 180; ++f) {
        world.begin_frame(2 + f);
        UpdateWeatherAndSurfaceSystem(world, 1.f / 60.f, world.frame_commands());
    }
    const SurfaceMaterialComponent* s = world.get<SurfaceMaterialComponent>(surface);
    const SurfaceFrictionApplyComponent* fr = world.get<SurfaceFrictionApplyComponent>(surface);
    ENGINE_ASSERT(s && fr, "surface missing");
    ENGINE_ASSERT(s->current_wetness > 0.99f, "wetness did not reach 1");
    ENGINE_ASSERT(std::fabs(fr->final_friction_multiplier - 0.6f) < 0.02f,
                  "friction did not drop 40%");

    ParticleEmitterComponent exhaust{};
    exhaust.emitter_type        = kParticleExhaust;
    exhaust.position            = float3{0.f, 0.4f, 0.f};
    exhaust.emission_direction  = float3{0.f, 0.5f, -2.0f};
    exhaust.spawn_rate_per_sec  = 40.f;
    exhaust.particle_lifetime   = 2.0f;
    exhaust.velocity_variance   = float3{0.1f, 0.1f, 0.1f};
    Entity pex = instantiate_particle_emitter(world, req, exhaust, 64);

    for (u32 f = 0; f < 30; ++f) {
        world.begin_frame(200 + f);
        UpdateParticleSystem(world, 1.f / 60.f, engine.memory().frame());
    }
    const ParticleBatchComponent* batch = world.get<ParticleBatchComponent>(pex);
    ENGINE_ASSERT(batch && batch->active_count > 0, "no exhaust particles");
    float max_vx = 0.f;
    for (u32 i = 0; i < batch->active_count; ++i) {
        if (batch->velocities[i].x > max_vx) {
            max_vx = batch->velocities[i].x;
        }
    }
    ENGINE_ASSERT(max_vx > 0.5f, "wind did not accelerate exhaust");

    std::printf("MICRO-PHASE 8 sandbox passed\n");
    std::printf("  Doppler observed Hz : %.2f (source 1200)\n", static_cast<double>(mix->observed_hz));
    std::printf("  occluded volume     : %.3f\n", static_cast<double>(mix->occluded_volume));
    std::printf("  lowpass scalar      : %.3f\n", static_cast<double>(mix->lowpass_scalar));
    std::printf("  wetness             : %.3f\n", static_cast<double>(s->current_wetness));
    std::printf("  final friction      : %.3f (base 1.0, -40%%)\n",
                static_cast<double>(fr->final_friction_multiplier));
    std::printf("  exhaust max vx      : %.3f (wind 18)\n", static_cast<double>(max_vx));

    engine.shutdown();
    return 0;
}
