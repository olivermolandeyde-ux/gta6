#include "Engine.h"
#include "render/CubeMesh.h"
#include "render/InputSystem.h"
#include "render/MetalRenderer.h"
#include "render/MetalWindow.h"
#include "render/RenderPipeline.h"

#include <cstdio>
#include <cstring>

#if !defined(__APPLE__)
#include <unistd.h>
#endif

int main(int argc, char** argv) {
    using namespace engine;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    bool forever = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--once") == 0) {
            forever = false;
        }
    }

    MetalWindow window{};
    window.create("Leonida Engine - Visual Awakening", 1280, 720);

    MetalRenderer renderer{};
    renderer.init(window.getMetalLayer(), 1280, 720);

    InputSystem input{};
    std::memset(&input.currentState, 0, sizeof(input.currentState));
    std::memset(&input.previousState, 0, sizeof(input.previousState));

    MemoryBudget budget{};
    budget.world_arena_bytes     = 32ull * 1024ull * 1024ull;
    budget.streaming_arena_bytes = 8ull * 1024ull * 1024ull;
    budget.chunk_pool_bytes      = 16ull * 1024ull * 1024ull;
    budget.record_pool_bytes     = 4ull * 1024ull * 1024ull;
    budget.meta_pool_bytes       = 1ull * 1024ull * 1024ull;
    budget.stack_bytes           = 1ull * 1024ull * 1024ull;
    budget.frame_bytes           = 4ull * 1024ull * 1024ull;
    Engine engine;
    engine.boot(budget, 1);

    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "pbr_cube";
    RenderableComponent cube{};
    cube.mesh_id     = 1;
    cube.material_id = 1;
    cube.transform_id = 0;
    TransformComponent xf{};
    xf.position[0] = 0.f;
    xf.position[1] = 0.5f;
    xf.position[2] = 0.f;
    xf.rotation[3] = 1.f;
    xf.scale[0] = xf.scale[1] = xf.scale[2] = 1.f;
    (void)engine.world().instantiate(req, cube, xf);

    renderer.cameraPos = float3{0.f, 2.f, -5.f};

    u32 frames = 0;
    bool announced = false;
    while (!window.shouldClose) {
        window.pollEvents();
        input.update();

        float3 cameraPos = renderer.cameraPos;
        if (input.isKeyDown('W') || input.isKeyDown('w')) {
            cameraPos.z += 0.1f;
        }
        if (input.isKeyDown('S') || input.isKeyDown('s')) {
            cameraPos.z -= 0.1f;
        }
        if (input.isKeyDown('A') || input.isKeyDown('a')) {
            cameraPos.x -= 0.1f;
        }
        if (input.isKeyDown('D') || input.isKeyDown('d')) {
            cameraPos.x += 0.1f;
        }
        renderer.cameraPos = cameraPos;

        engine.world().begin_frame(frames);
        renderer.beginFrame();
        renderer.renderScene(engine.world(), engine.memory().frame());
        renderer.endFrame();
        ++frames;

        if (!announced && frames >= 8) {
            std::printf("VISUAL AWAKENING COMPLETE — A rotating PBR cube is now rendering on screen "
                        "with real-time lighting\n");
            std::fflush(stdout);
            announced = true;
            if (!forever) {
                break;
            }
        }
#if !defined(__APPLE__)
        usleep(16000);
#endif
        if (frames > 60u * 60u * 8u) {
            break;
        }
    }

    renderer.shutdown();
    window.destroy();
    engine.shutdown();
    return 0;
}
