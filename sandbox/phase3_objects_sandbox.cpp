#include "Engine.h"
#include "objects/BreakableWindow.h"
#include "objects/StreetLight.h"
#include "objects/VehicleEngine.h"

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

    InstantiationRequest persistent{};
    persistent.domain      = InstantiationDomain::PersistentWorld;
    persistent.debug_label = "phase3";

    VehicleEngineSpawnDesc ice_desc{};
    ice_desc.throttle                    = 0.70f;
    ice_desc.exhaust_particle_emitter_id = 42;
    Entity ice = instantiate_vehicle_engine(engine.world(), persistent, ice_desc);

    BreakableWindowSpawnDesc glass_desc{};
    glass_desc.mesh_id_intact    = 100;
    glass_desc.mesh_id_shattered = 101;
    glass_desc.fracture_seed     = 0xBEEF;
    glass_desc.center_ws         = float3{0.f, 1.2f, 4.f};
    glass_desc.normal_ws         = float3{0.f, 0.f, 1.f};
    Entity window = instantiate_breakable_window(engine.world(), persistent, glass_desc);

    MunicipalPowerNodeSpawnDesc node_desc{};
    node_desc.node_id    = 17;
    node_desc.voltage_pu = 1.0f;
    (void)instantiate_municipal_power_node(engine.world(), persistent, node_desc);

    StreetLightSpawnDesc lamp_desc{};
    lamp_desc.light_handle        = 9;
    lamp_desc.power_grid_node_id  = 17;
    lamp_desc.flicker_probability = 0.0f;
    lamp_desc.wear_factor         = 0.1f;
    Entity lamp = instantiate_street_light(engine.world(), persistent, lamp_desc);

    StreetLightSpawnDesc dead_lamp_desc = lamp_desc;
    dead_lamp_desc.light_handle = 10;
    dead_lamp_desc.wear_factor  = 1.0f;
    Entity dead_lamp = instantiate_street_light(engine.world(), persistent, dead_lamp_desc);

    for (u32 f = 0; f < 120; ++f) {
        engine.world().begin_frame(f);
        UpdateVehicleEngineSystem(engine.world(), 1.0f / 60.0f, engine.memory().frame());
        UpdateStreetLightSystem(engine.world(), 1.0f / 60.0f, engine.world().frame_commands());
        engine.world().frame_commands().playback(engine.world());
    }

    const VehicleEngineComponent* ice_live = engine.world().get<VehicleEngineComponent>(ice);
    const VehicleExhaustPlumeComponent* plume =
        engine.world().get<VehicleExhaustPlumeComponent>(ice);
    ENGINE_ASSERT(ice_live != nullptr && plume != nullptr, "ICE missing");
    ENGINE_ASSERT(ice_live->current_rpm > 2000.f, "throttle did not raise RPM");
    ENGINE_ASSERT(ice_live->temperature_c > 25.f, "block did not heat");
    ENGINE_ASSERT(plume->soot_rate > 0.f, "exhaust plume silent");

    const StreetLightComponent* lit = engine.world().get<StreetLightComponent>(lamp);
    ENGINE_ASSERT(lit != nullptr && lit->current_voltage > 0.4f, "healthy lamp should be lit");
    ENGINE_ASSERT(engine.world().has<StreetLightOutageTag>(dead_lamp), "dead ballast should outage");

    ImpactEvent hit{};
    hit.target_entity   = window;
    hit.impact_point    = float3{0.f, 1.2f, 4.0f};
    hit.impact_velocity = float3{0.f, 0.f, -28.f};
    hit.mass            = 6.5f;

    engine.world().begin_frame(200);
    ProcessWindowImpactsSystem(engine.world(), &hit, 1, engine.world().frame_commands());
    engine.world().frame_commands().playback(engine.world());
    ENGINE_ASSERT(engine.world().has<WindowShatteredTag>(window), "tempered pane did not shatter");
    ENGINE_ASSERT(engine.world().get<BreakableWindowComponent>(window)->structural_integrity <= 0.f,
                  "integrity should be zero after shatter");

    std::printf("MICRO-PHASE 3 sandbox passed\n");
    std::printf("  ice rpm        : %.1f\n", static_cast<double>(ice_live->current_rpm));
    std::printf("  ice temp C     : %.2f\n", static_cast<double>(ice_live->temperature_c));
    std::printf("  ice wear       : %.6f\n", static_cast<double>(ice_live->wear_factor));
    std::printf("  exhaust soot   : %.1f\n", static_cast<double>(plume->soot_rate));
    std::printf("  lamp voltage   : %.3f\n", static_cast<double>(lit->current_voltage));
    std::printf("  window shards  : %u\n",
                static_cast<unsigned>(engine.world().get<WindowShatteredTag>(window)->shard_count));

    engine.shutdown();
    return 0;
}
