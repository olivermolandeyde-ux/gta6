#include "Engine.h"
#include "render/BuildingGlPass.h"
#include "render/SdlGlWindow.h"
#include "render/TerrainGlPass.h"
#include "world/CityGenerator.h"
#include "world/CloudSystem.h"
#include "world/SkySystem.h"
#include "world/TerrainSystem.h"

#include <SDL.h>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <SDL_opengl.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Verification screenshot: raw framebuffer -> binary PPM (no encoder needed).
static void save_screenshot_ppm(const char* path, int w, int h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    engine::u8* px = static_cast<engine::u8*>(std::malloc((engine::usize)w * h * 3));
    if (!px) {
        return;
    }
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    FILE* f = std::fopen(path, "wb");
    if (f) {
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y) {
            std::fwrite(px + (engine::usize)y * w * 3, 1, (engine::usize)w * 3, f);
        }
        std::fclose(f);
    }
    std::free(px);
}

int main(int argc, char** argv) {
    using namespace engine;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    bool forever = true;
    bool bldg_row = false;
    bool xwalk = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--once") == 0) {
            forever = false;
        }
        if (std::strcmp(argv[i], "--bldg-row") == 0) {
            bldg_row = true;
        }
        if (std::strcmp(argv[i], "--xwalk") == 0) {
            xwalk = true;
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
    if (bldg_row) {
        buildings.verification_row = true;
        std::printf("[city] verification row enabled: 9 GLB variants at z=160 + screenshot\n");
        std::fflush(stdout);
    }
    buildings.buildMesh(world);

    std::printf("[city] press F to flip the car bodies 180 deg, G to flip the SUV alone\n");
    std::fflush(stdout);
    bool flip_key_down = false;
    bool suv_key_down  = false;
    u32 frames = 0;
    bool announced = false;
    u32 fps_t0 = SDL_GetTicks();
    while (!window.shouldClose) {
        window.pollEvents();

        const bool flip_now = window.isKeyDown('f') || window.isKeyDown('F');
        if (flip_now && !flip_key_down) {
            buildings.car_body_flip = (buildings.car_body_flip > 0.1f) ? 0.f : kCarPi;
            std::printf("[cars] Body yaw offset now %.0f deg%s\n",
                        static_cast<double>(buildings.car_body_flip * 180.f / kCarPi),
                        buildings.car_body_flip > 0.1f ? " (flipped)" : " (default)");
            std::fflush(stdout);
        }
        flip_key_down = flip_now;

        const bool suv_now = window.isKeyDown('g') || window.isKeyDown('G');
        if (suv_now && !suv_key_down) {
            buildings.car_suv_flip = (buildings.car_suv_flip > 0.1f) ? 0.f : kCarPi;
            std::printf("[cars] SUV yaw offset now %.0f deg%s\n",
                        static_cast<double>(buildings.car_suv_flip * 180.f / kCarPi),
                        buildings.car_suv_flip > 0.1f ? " (flipped)" : " (default)");
            std::fflush(stdout);
        }
        suv_key_down = suv_now;

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
        if (xwalk) {
            // Scripted intersection inspection: street-level first, then top-down.
            if (frames < 9) {
                terrain.cameraPos    = float3{1200.f, 7.f, 80.f};
                terrain.cameraTarget = float3{1200.f, 7.f, 140.f};
            } else {
                terrain.cameraPos    = float3{1200.f, 250.f, 121.f};
                terrain.cameraTarget = float3{1200.f, 5.f, 120.f};
            }
        }

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
                       sun, static_cast<float>(frames) * dt);
        if (bldg_row && frames == 6) {
            save_screenshot_ppm("build/bldg_row.ppm", terrain.width, terrain.height);
            std::printf("[city] screenshot: build/bldg_row.ppm (verification row, frame %u)\n",
                        frames);
            std::fflush(stdout);
        }
        if (xwalk && frames == 6) {
            save_screenshot_ppm("build/xwalk_street.ppm", terrain.width, terrain.height);
            std::printf("[city] screenshot: build/xwalk_street.ppm (street level, frame %u)\n",
                        frames);
            std::fflush(stdout);
        }
        if (xwalk && frames == 12) {
            save_screenshot_ppm("build/xwalk_top.ppm", terrain.width, terrain.height);
            std::printf("[city] screenshot: build/xwalk_top.ppm (top-down, frame %u)\n",
                        frames);
            std::fflush(stdout);
        }
        window.swap();
        ++frames;

        if ((frames % 60u) == 0u) {
            const u32 now = SDL_GetTicks();
            const u32 dt_ms = now > fps_t0 ? now - fps_t0 : 1u;
            const float fps = 60000.f / static_cast<float>(dt_ms);
            fps_t0 = now;
            std::printf("[perf] fps=%.1f frame=%u cam=(%.1f, %.1f, %.1f) tod=%.1f buildings=%u\n", fps,
                        frames, terrain.cameraPos.x, terrain.cameraPos.y, terrain.cameraPos.z, tod,
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
        if (xwalk && frames > 14) {
            break; // scripted shots done
        }
    }

    buildings.shutdown();
    terrain.shutdown();
    window.destroy();
    engine.shutdown();
    return 0;
}
