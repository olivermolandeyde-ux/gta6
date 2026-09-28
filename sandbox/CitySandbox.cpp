#include "Engine.h"
#include "render/BuildingGlPass.h"
#include "render/SdlGlWindow.h"
#include "render/TerrainGlPass.h"
#include "world/CityGenerator.h"
#include "world/CloudSystem.h"
#include "world/SkySystem.h"
#include "world/TerrainSystem.h"

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
    if (!window.create("Leonida Engine - City (SDL2/GL)", 1280, 720)) {
        std::printf("[sdl] failed to create window\n");
        return 1;
    }

    TerrainGlPass terrain{};
    if (!terrain.init(window.width, window.height)) {
        std::printf("[gl] terrain pass init failed\n");
        window.destroy();
        return 1;
    }
    BuildingGlPass buildings{};
    if (!buildings.init()) {
        std::printf("[gl] building pass init failed\n");
        terrain.shutdown();
        window.destroy();
        return 1;
    }

    const float3 city_center{kCityCenterM, 0.f, kCityCenterM};
    terrain.cameraPos        = float3{kCityCenterM, 8.f, 130.f};
    terrain.cameraTarget     = float3{kCityCenterM, 18.f, 280.f};
    float yaw                = 0.f;
    float pitch              = 0.22f;
    std::printf("[city] STREET LEVEL cam=(%.1f, %.1f, %.1f) pitch=+%.2f looking up the avenue\n",
                terrain.cameraPos.x, terrain.cameraPos.y, terrain.cameraPos.z, pitch);
    std::fflush(stdout);

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
    persistent.debug_label = "city_phase19";

    constexpr u32 kMap = 256;
    auto* heights = static_cast<float*>(
        engine.memory().world_arena().allocate(sizeof(float) * kMap * kMap, alignof(float)));
    auto* normals = static_cast<float3*>(
        engine.memory().world_arena().allocate(sizeof(float3) * kMap * kMap, alignof(float3)));
    TerrainHeightmapGenerator heightGen{};
    heightGen.generateHeightmap(heights, kMap, kMap, 42);
    heightGen.generateNormalmap(heights, kMap, kMap, normals);
    std::printf("[city] heightmap 256² sample(1200,1200)=%.2f gpu_ht=%.2f plateau=%.1f\n",
                terrain_sample_height(heights, kMap, kMap, 1200.f, 1200.f),
                city_gpu_terrain_height(1200.f, 1200.f), kCityPlateauY);
    (void)normals;

    SkyComponent sky{};
    sky.time_of_day   = 10.5f;
    sky.turbidity     = 3.0f;
    sky.sun_direction = float3_normalize_or(float3{0.45f, 0.75f, 0.35f}, float3{0.f, 1.f, 0.f});
    sky.sun_color     = float3{1.f, 0.95f, 0.85f};
    (void)world.instantiate(persistent, sky);

    CloudLayerComponent clouds{};
    clouds.layer_position   = float3{0.f, 1400.f, 0.f};
    clouds.layer_thickness  = 500.f;
    clouds.cloud_coverage   = 0.4f;
    clouds.wind_velocity    = float3{12.f, 0.f, 4.f};
    clouds.noise_texture_id = 1;
    (void)world.instantiate(persistent, clouds);

    // City is 1.9 km — fully resident. WorldStreamer (Phase 6) asserts once the
    // resident-cell cap is exceeded; do not tick it here.
    CityGenerator cityGen{};
    cityGen.buildings_spawned = cityGen.streets_spawned = 0;
    cityGen.lights_spawned = cityGen.windows_spawned = 0;
    cityGen.generateCity(world, city_center, kCityCenterM, 900);
    buildings.buildMesh(world);

    u32 frames = 0;
    bool announced = false;
    while (!window.shouldClose) {
        window.pollEvents();
        terrain.width  = window.width;
        terrain.height = window.height;

        if (frames >= 8) {
            yaw -= window.mouseDeltaX * 0.005f;
            pitch -= window.mouseDeltaY * 0.005f;
            pitch = clampf(pitch, -0.6f, 1.2f);
        }
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        const float3 fwd{sy * cp, sp, cy * cp};
        const float3 right{cy, 0.f, -sy};
        const float dt = 1.f / 60.f;
        const float move = window.shiftDown ? 36.f : 12.f;
        float3 cam = terrain.cameraPos;
        if (window.isKeyDown('w') || window.isKeyDown('W')) {
            cam = float3_add(cam, float3_scale(fwd, move * dt));
        }
        if (window.isKeyDown('s') || window.isKeyDown('S')) {
            cam = float3_sub(cam, float3_scale(fwd, move * dt));
        }
        if (window.isKeyDown('a') || window.isKeyDown('A')) {
            cam = float3_add(cam, float3_scale(right, move * dt));
        }
        if (window.isKeyDown('d') || window.isKeyDown('D')) {
            cam = float3_sub(cam, float3_scale(right, move * dt));
        }
        if (window.isKeyDown('q') || window.isKeyDown('Q')) {
            cam.y -= move * dt;
        }
        if (window.isKeyDown('e') || window.isKeyDown('E')) {
            cam.y += move * dt;
        }
        terrain.cameraPos    = cam;
        terrain.cameraTarget = float3_add(cam, fwd);

        world.begin_frame(frames);
        UpdateSkySystem(world, dt, world.frame_commands());
        UpdateCloudSystem(world, dt, world.frame_commands());
        SkyComponent* sky_now = find_sky(world);
        if (sky_now) {
            if (window.isKeyDown('t') || window.isKeyDown('T')) {
                sky_now->time_of_day += dt * 4.f;
                if (sky_now->time_of_day >= 24.f) {
                    sky_now->time_of_day -= 24.f;
                }
            }
            if (window.isKeyDown('g') || window.isKeyDown('G')) {
                sky_now->time_of_day -= dt * 4.f;
                if (sky_now->time_of_day < 0.f) {
                    sky_now->time_of_day += 24.f;
                }
            }
        }

        terrain.beginFrame();
        if (sky_now) {
            terrain.drawSky(*sky_now);
        }
        terrain.drawTerrain();
        const float tod = sky_now ? sky_now->time_of_day : 21.f;
        const float3 sun = sky_now ? sky_now->sun_direction : float3{0.5f, 0.8f, 0.3f};
        buildings.draw(world, terrain.cameraPos, terrain.cameraTarget, terrain.width, terrain.height, tod,
                       sun);
        window.swap();
        ++frames;

        if ((frames % 60u) == 0u) {
            std::printf("[city] frame %u cam=(%.1f, %.1f, %.1f) tod=%.1f buildings=%u\n", frames,
                        terrain.cameraPos.x, terrain.cameraPos.y, terrain.cameraPos.z, tod,
                        cityGen.buildings_spawned);
            std::fflush(stdout);
        }
        if (!announced && frames >= 8) {
            std::printf("CITY GENERATION COMPLETE — %u procedural buildings with streets and window "
                        "lighting are now rendering on the terrain\n",
                        cityGen.buildings_spawned);
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

    buildings.shutdown();
    terrain.shutdown();
    window.destroy();
    engine.shutdown();
    return 0;
}
