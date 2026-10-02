#include "Engine.h"
#include "render/RenderPipeline.h"

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

    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "pbr_prop";

    constexpr u32 kDraws = 128;
    for (u32 i = 0; i < kDraws; ++i) {
        TransformComponent xf{};
        xf.position[0] = static_cast<float>(i);
        xf.position[1] = 0.f;
        xf.position[2] = 0.f;
        xf.rotation[3] = 1.f;
        xf.scale[0] = xf.scale[1] = xf.scale[2] = 1.f;

        RenderableComponent r{};
        r.mesh_id      = 1;
        r.material_id  = 7;
        r.transform_id = i;

        (void)engine.world().instantiate(req, xf, r);
    }

    engine.world().begin_frame(1);
    RenderGeometryPass(engine.world(), engine.world().frame_commands(), engine.memory().frame());

    const u32 gpu_ops = engine.world().frame_commands().gpu_command_count();
    ENGINE_ASSERT(gpu_ops >= 2 + kDraws * 3, "geometry pass did not record draw stream");

    std::printf("MICRO-PHASE 2 sandbox passed\n");
    std::printf("  live entities : %u\n", engine.world().entity_count());
    std::printf("  gpu commands  : %u\n", gpu_ops);

    engine.shutdown();
    return 0;
}
