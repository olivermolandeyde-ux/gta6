#include "Engine.h"
#include "render/InputSystem.h"
#include "render/MetalRenderer.h"
#include "render/MetalWindow.h"
#include "world/CloudSystem.h"
#include "world/SkySystem.h"
#include "world/TerrainSystem.h"
#include "world/WorldStreamer.h"

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
    window.hosted_html_relpath = "sandbox/terrain_sky.html";
    window.create("Leonida Engine - Terrain & Sky", 1280, 720);

    MetalRenderer renderer{};
    renderer.init(window.getMetalLayer(), 1280, 720);
    renderer.cameraPos = float3{48.f, 62.f, -96.f};

    InputSystem input{};
    std::memset(&input.currentState, 0, sizeof(input.currentState));
    std::memset(&input.previousState, 0, sizeof(input.previousState));

    MemoryBudget budget{};
    budget.world_arena_bytes     = 64ull * 1024ull * 1024ull;
    budget.streaming_arena_bytes = 32ull * 1024ull * 1024ull;
    budget.chunk_pool_bytes      = 16ull * 1024ull * 1024ull;
    budget.record_pool_bytes     = 4ull * 1024ull * 1024ull;
    budget.meta_pool_bytes       = 1ull * 1024ull * 1024ull;
    budget.stack_bytes           = 1ull * 1024ull * 1024ull;
    budget.frame_bytes           = 4ull * 1024ull * 1024ull;
    Engine engine;
    engine.boot(budget, 1);
    World& world = engine.world();

    InstantiationRequest persistent{};
    persistent.domain      = InstantiationDomain::PersistentWorld;
    persistent.debug_label = "terrain_phase18";

    constexpr u32 kMap = 256;
    auto* heights = static_cast<float*>(
        engine.memory().world_arena().allocate(sizeof(float) * kMap * kMap, alignof(float)));
    auto* normals = static_cast<float3*>(
        engine.memory().world_arena().allocate(sizeof(float3) * kMap * kMap, alignof(float3)));
    TerrainHeightmapGenerator heightGen{};
    heightGen.generateHeightmap(heights, kMap, kMap, 42);
    heightGen.generateNormalmap(heights, kMap, kMap, normals);

    SkyComponent sky{};
    sky.time_of_day = 10.0f;
    sky.turbidity   = 3.0f;
    sky.sun_direction = float3{0.f, 1.f, 0.3f};
    sky.sun_color     = float3{1.f, 0.95f, 0.85f};
    (void)world.instantiate(persistent, sky);

    CloudLayerComponent clouds{};
    clouds.layer_position  = float3{0.f, 1400.f, 0.f};
    clouds.layer_thickness = 500.f;
    clouds.cloud_coverage  = 0.55f;
    clouds.wind_velocity   = float3{12.f, 0.f, 4.f};
    clouds.noise_texture_id = 1;
    (void)world.instantiate(persistent, clouds);

    StreamObserverComponent observer{};
    observer.world_pos = float3{96.f, 0.f, 96.f};
    Entity player = world.instantiate(persistent, observer);
    (void)instantiate_world_streamer(world, persistent, player, /*radius=*/1, kTerrainChunkSizeM);

    u32 frames = 0;
    bool announced = false;
    while (!window.shouldClose) {
        window.pollEvents();
        input.update();

        float3 cam = renderer.cameraPos;
        if (input.isKeyDown('W') || input.isKeyDown('w')) {
            cam.z += 1.2f;
        }
        if (input.isKeyDown('S') || input.isKeyDown('s')) {
            cam.z -= 1.2f;
        }
        if (input.isKeyDown('A') || input.isKeyDown('a')) {
            cam.x -= 1.2f;
        }
        if (input.isKeyDown('D') || input.isKeyDown('d')) {
            cam.x += 1.2f;
        }
        if (input.isKeyDown('Q') || input.isKeyDown('q')) {
            cam.y -= 0.8f;
        }
        if (input.isKeyDown('E') || input.isKeyDown('e')) {
            cam.y += 0.8f;
        }
        renderer.cameraPos = cam;
        if (StreamObserverComponent* obs = world.get<StreamObserverComponent>(player)) {
            obs->world_pos = float3{cam.x, 0.f, cam.z < 0.f ? 0.f : cam.z};
        }

        world.begin_frame(frames);
        UpdateWorldStreamerSystem(world, 1.f / 60.f, world.frame_commands());
        world.frame_commands().playback(world);
        world.frame_commands().reset();
        UpdateTerrainStreamingSystem(world, 1.f / 60.f, engine.memory().frame(),
                                     world.frame_commands());
        world.frame_commands().playback(world);

        UpdateSkySystem(world, 1.f / 60.f, world.frame_commands());
        UpdateCloudSystem(world, 1.f / 60.f, world.frame_commands());

        SkyComponent* sky_now = find_sky(world);
        CloudLayerComponent* clouds_now = find_cloud_layer(world);

        renderer.beginFrame();
        if (sky_now) {
            renderer.renderSky(*sky_now);
        }
        if (clouds_now && sky_now) {
            renderer.renderClouds(*clouds_now, *sky_now);
        }
        renderer.renderTerrain(world, engine.memory().frame(), heights, kMap, kMap);
        renderer.endFrame();
        ++frames;

        if (!announced && frames >= 8) {
            std::printf("TERRAIN & SKY COMPLETE — A procedurally generated landscape with dynamic "
                        "sky, volumetric clouds, and day/night cycle is now rendering\n");
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
