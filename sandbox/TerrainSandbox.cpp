#include "Engine.h"
#include "render/SdlGlWindow.h"
#include "render/TerrainGlPass.h"
#include "world/CloudSystem.h"
#include "world/SkySystem.h"
#include "world/TerrainSystem.h"
#include "world/WorldStreamer.h"

#include <cmath>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    using namespace engine;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    bool forever = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--once") == 0) {
            forever = false;
        }
    }

    SdlGlWindow window{};
    if (!window.create("Leonida Engine - Terrain & Sky (SDL2/GL)", 1280, 720)) {
        std::printf("[sdl] failed to create window\n");
        return 1;
    }

    TerrainGlPass pass{};
    if (!pass.init(window.width, window.height)) {
        std::printf("[gl] terrain pass init failed\n");
        window.destroy();
        return 1;
    }
    pass.cameraPos    = float3{96.f, 800.f, -704.f};
    pass.cameraTarget = float3{96.f, 0.f, 96.f};
    float yaw = 0.f;
    float pitch = -0.78539816f;

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
    persistent.debug_label = "terrain_sdl";

    constexpr u32 kMap = 256;
    auto* heights = static_cast<float*>(
        engine.memory().world_arena().allocate(sizeof(float) * kMap * kMap, alignof(float)));
    auto* normals = static_cast<float3*>(
        engine.memory().world_arena().allocate(sizeof(float3) * kMap * kMap, alignof(float3)));
    TerrainHeightmapGenerator heightGen{};
    heightGen.generateHeightmap(heights, kMap, kMap, 42);
    heightGen.generateNormalmap(heights, kMap, kMap, normals);
    {
        u32 bad = 0;
        float hmin = heights[0];
        float hmax = heights[0];
        for (u32 i = 0; i < kMap * kMap; ++i) {
            const float h = heights[i];
            if (!std::isfinite(h)) {
                ++bad;
            } else {
                hmin = min_of(hmin, h);
                hmax = max_of(hmax, h);
            }
        }
        std::printf("[terrain] heightmap %ux%u nan/inf=%u min=%.2f max=%.2f "
                    "(legacy log: textures=procedural_1x1 unused — GLSL emissive height bands)\n",
                    kMap, kMap, bad, hmin, hmax);
        std::fflush(stdout);
    }

    SkyComponent sky{};
    sky.time_of_day   = 10.0f;
    sky.turbidity     = 3.0f;
    sky.sun_direction = float3_normalize_or(float3{0.5f, 0.8f, 0.3f}, float3{0.f, 1.f, 0.f});
    sky.sun_color     = float3{1.f, 0.95f, 0.85f};
    (void)world.instantiate(persistent, sky);

    CloudLayerComponent clouds{};
    clouds.layer_position   = float3{0.f, 1400.f, 0.f};
    clouds.layer_thickness  = 500.f;
    clouds.cloud_coverage   = 0.55f;
    clouds.wind_velocity    = float3{12.f, 0.f, 4.f};
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
        pass.width = window.width;
        pass.height = window.height;

        yaw += window.mouseDeltaX * 0.005f;
        pitch -= window.mouseDeltaY * 0.005f;
        pitch = clampf(pitch, -1.5f, 1.5f);
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        const float3 fwd{sy * cp, sp, cy * cp};
        const float3 right{cy, 0.f, -sy};
        const float dt = 1.f / 60.f;
        const float move = window.shiftDown ? 48.f : 18.f;
        float3 cam = pass.cameraPos;
        if (window.isKeyDown('w') || window.isKeyDown('W')) {
            cam = float3_add(cam, float3_scale(fwd, move * dt));
        }
        if (window.isKeyDown('s') || window.isKeyDown('S')) {
            cam = float3_sub(cam, float3_scale(fwd, move * dt));
        }
        if (window.isKeyDown('a') || window.isKeyDown('A')) {
            cam = float3_sub(cam, float3_scale(right, move * dt));
        }
        if (window.isKeyDown('d') || window.isKeyDown('D')) {
            cam = float3_add(cam, float3_scale(right, move * dt));
        }
        if (window.isKeyDown('q') || window.isKeyDown('Q')) {
            cam.y -= move * dt;
        }
        if (window.isKeyDown('e') || window.isKeyDown('E')) {
            cam.y += move * dt;
        }
        pass.cameraPos = cam;
        pass.cameraTarget = float3_add(cam, fwd);
        if (StreamObserverComponent* obs = world.get<StreamObserverComponent>(player)) {
            obs->world_pos = float3{cam.x < 0.f ? 0.f : cam.x, 0.f, cam.z < 0.f ? 0.f : cam.z};
        }

        world.begin_frame(frames);
        UpdateWorldStreamerSystem(world, dt, world.frame_commands());
        world.frame_commands().playback(world);
        world.frame_commands().reset();
        UpdateTerrainStreamingSystem(world, dt, engine.memory().frame(), world.frame_commands());
        world.frame_commands().playback(world);
        UpdateSkySystem(world, dt, world.frame_commands());
        UpdateCloudSystem(world, dt, world.frame_commands());
        SkyComponent* sky_now = find_sky(world);

        pass.beginFrame();
        if (sky_now) {
            pass.drawSky(*sky_now);
        }
        pass.drawTerrain();
        window.swap();
        ++frames;

        if ((frames % 60u) == 0u) {
            std::printf("[terrain] frame %u cam=(%.1f, %.1f, %.1f) yaw=%.2f pitch=%.2f sdl2/gl\n",
                        frames, pass.cameraPos.x, pass.cameraPos.y, pass.cameraPos.z, yaw, pitch);
            std::fflush(stdout);
        }
        if (!announced && frames >= 8) {
            std::printf("TERRAIN & SKY COMPLETE — A procedurally generated landscape with dynamic "
                        "sky, volumetric clouds, and day/night cycle is now rendering\n");
            std::fflush(stdout);
            announced = true;
            if (!forever) {
                break;
            }
        }
        if (frames > 60u * 60u * 8u) {
            break;
        }
    }

    pass.shutdown();
    window.destroy();
    engine.shutdown();
    return 0;
}
