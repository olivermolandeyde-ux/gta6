#include "Engine.h"

#include <cstdio>

// Sandbox components exist only to prove the allocator + archetype pipeline.
// They are data, not "generic props". Real StreetLight / VehicleEngine types
// arrive in MICRO-PHASE 3 and will each own dedicated columns + systems.

namespace sandbox {

struct WorldTransform {
    engine::f32 position[3];
    engine::f32 quat[4];
    engine::f32 scale[3];
};

struct LinearVelocity {
    engine::f32 meters_per_second[3];
};

struct ThermalState {
    engine::f32 kelvin;
    engine::f32 watts_exchange;
};

static WorldTransform make_transform(engine::f32 x, engine::f32 y, engine::f32 z) {
    WorldTransform t{};
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    t.quat[0] = 0.f;
    t.quat[1] = 0.f;
    t.quat[2] = 0.f;
    t.quat[3] = 1.f;
    t.scale[0] = t.scale[1] = t.scale[2] = 1.f;
    return t;
}

void integrate_velocity(engine::World& world, engine::f32 dt) {
    engine::Query query{};
    query.required.set(world.registry().id_of<WorldTransform>());
    query.required.set(world.registry().id_of<LinearVelocity>());

    world.for_each_chunk(query, [&](const engine::ChunkView& view) {
        auto* transforms = view.column<WorldTransform>(world.registry().id_of<WorldTransform>());
        auto* velocities = view.column<LinearVelocity>(world.registry().id_of<LinearVelocity>());
        for (engine::u16 row = 0; row < view.count; ++row) {
            transforms[row].position[0] += velocities[row].meters_per_second[0] * dt;
            transforms[row].position[1] += velocities[row].meters_per_second[1] * dt;
            transforms[row].position[2] += velocities[row].meters_per_second[2] * dt;
        }
    });
}

} // namespace sandbox

int main() {
    using namespace engine;

    MemoryBudget budget{};
    budget.world_arena_bytes     = 32ull * 1024ull * 1024ull;
    budget.streaming_arena_bytes = 16ull * 1024ull * 1024ull;
    budget.chunk_pool_bytes      = 32ull * 1024ull * 1024ull;
    budget.record_pool_bytes     = 8ull * 1024ull * 1024ull;
    budget.meta_pool_bytes       = 2ull * 1024ull * 1024ull;
    budget.stack_bytes           = 1ull * 1024ull * 1024ull;
    budget.frame_bytes           = 2ull * 1024ull * 1024ull;

    Engine engine;
    engine.boot(budget, /*world_id=*/1);
    engine.scheduler().add({"integrate_velocity", PipelineStage::Simulation, &sandbox::integrate_velocity});

    InstantiationRequest persistent{};
    persistent.domain      = InstantiationDomain::PersistentWorld;
    persistent.debug_label = "thermal_probe";

    sandbox::WorldTransform xf = sandbox::make_transform(10.f, 0.f, 0.f);
    sandbox::LinearVelocity vel{};
    vel.meters_per_second[0] = 2.f;
    vel.meters_per_second[1] = 0.f;
    vel.meters_per_second[2] = 0.f;
    sandbox::ThermalState heat{};
    heat.kelvin = 310.15f;
    heat.watts_exchange = 0.f;

    Entity probe = engine.world().instantiate(persistent, xf, vel, heat);
    ENGINE_ASSERT(engine.world().is_alive(probe), "instantiate failed");
    ENGINE_ASSERT(engine.world().has<sandbox::ThermalState>(probe), "thermal column missing");

    InstantiationRequest streamed{};
    streamed.domain          = InstantiationDomain::StreamingCell;
    streamed.debug_label     = "cell_prop_batch";
    streamed.cell.cell_x     = 12;
    streamed.cell.cell_y     = 7;

    constexpr u32 kBatch = 4096;
    Entity first_streamed = kNullEntity;
    for (u32 i = 0; i < kBatch; ++i) {
        sandbox::WorldTransform t = sandbox::make_transform(static_cast<f32>(i), 0.f, 0.f);
        Entity e = engine.world().instantiate(streamed, t);
        if (i == 0) {
            first_streamed = e;
        }
    }

    ENGINE_ASSERT(engine.world().live_entity_count() == kBatch + 1, "live count mismatch after instantiate");

    const u16 gen_before = first_streamed.generation();
    engine.world().destroy(first_streamed);
    ENGINE_ASSERT(!engine.world().is_alive(first_streamed), "stale handle still alive");

    Entity recycled = engine.world().instantiate(streamed, sandbox::make_transform(0.f, 0.f, 0.f));
    ENGINE_ASSERT(recycled.index() == first_streamed.index(), "free list did not recycle index");
    ENGINE_ASSERT(recycled.generation() != gen_before, "generation did not bump");

    engine.world().remove_component<sandbox::ThermalState>(probe);
    ENGINE_ASSERT(!engine.world().has<sandbox::ThermalState>(probe), "remove_component failed");
    engine.world().add_component<sandbox::ThermalState>(probe, sandbox::ThermalState{});

    for (u32 f = 0; f < 30; ++f) {
        engine.tick(1.0f / 60.0f);
    }

    const auto* xf_after = engine.world().get<sandbox::WorldTransform>(probe);
    ENGINE_ASSERT(xf_after != nullptr, "transform missing after ticks");
    ENGINE_ASSERT(xf_after->position[0] > 10.f, "integrate_velocity did not run");

    std::printf("MICRO-PHASE 1 sandbox passed\n");
    std::printf("  live entities : %u\n", engine.world().live_entity_count());
    std::printf("  archetypes    : %u\n", engine.world().archetype_count());
    std::printf("  frames        : %llu\n", static_cast<unsigned long long>(engine.frame()));
    std::printf("  probe x       : %.4f\n", static_cast<double>(xf_after->position[0]));
    std::printf("  committed VM  : %zu bytes\n", engine.memory().pages().committed_bytes());
    std::printf("  chunk blocks  : %zu\n", engine.memory().chunk_pool().allocated_blocks());

    engine.shutdown();
    return 0;
}
