#include "world/CityGenerator.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "objects/BreakableWindow.h"
#include "objects/StreetLight.h"
#include "render/RenderPipeline.h"

#include <cmath>
#include <cstdio>

namespace engine {

namespace {

constexpr u32   kBlocks     = 10;
constexpr float kPitch      = 38.4f;
constexpr float kStreetW    = 10.f;
constexpr float kFloorH     = 3.2f;
constexpr u32   kPowerNode  = 1;

[[nodiscard]] u32 mix32(u32 x) noexcept {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

[[nodiscard]] float unit(u32 x) noexcept {
    return static_cast<float>(x >> 8) * (1.f / 16777216.f);
}

float hash21(float x, float z) {
    float p0 = x * 0.1031f;
    float p1 = z * 0.1031f;
    float p2 = x * 0.0973f + z * 0.033f;
    p0 = p0 - std::floor(p0);
    p1 = p1 - std::floor(p1);
    p2 = p2 - std::floor(p2);
    const float d = p0 * (p1 + 33.33f) + p1 * (p2 + 33.33f) + p2 * (p0 + 33.33f);
    p0 += d;
    p1 += d;
    p2 += d;
    const float r = (p0 + p1) * p2;
    return r - std::floor(r);
}

float vn(float x, float z) {
    const float ix = std::floor(x);
    const float iz = std::floor(z);
    const float fx = x - ix;
    const float fz = z - iz;
    const float u = fx * fx * (3.f - 2.f * fx);
    const float v = fz * fz * (3.f - 2.f * fz);
    const float a = hash21(ix, iz);
    const float b = hash21(ix + 1.f, iz);
    const float c = hash21(ix, iz + 1.f);
    const float d = hash21(ix + 1.f, iz + 1.f);
    return (a * (1.f - u) + b * u) * (1.f - v) + (c * (1.f - u) + d * u) * v;
}

float fbm(float x, float z) {
    float v = 0.f;
    float a = 0.5f;
    float px = x;
    float pz = z;
    for (u32 i = 0; i < 6; ++i) {
        v += a * vn(px, pz);
        const float nx = px * 2.07f + 17.1f;
        const float nz = pz * 2.07f + 9.7f;
        px = nx;
        pz = nz;
        a *= 0.5f;
    }
    return v;
}

float3 palette(u32 id) {
    const float3 pal[6] = {
        {0.72f, 0.62f, 0.48f}, {0.55f, 0.56f, 0.58f}, {0.78f, 0.48f, 0.36f},
        {0.46f, 0.50f, 0.54f}, {0.82f, 0.74f, 0.62f}, {0.38f, 0.40f, 0.44f},
    };
    return pal[id % 6u];
}

void spawn_facade_windows(World& world, const InstantiationRequest& req, const BuildingComponent& b,
                          u32* window_count) {
    const float3 n[4] = {{0.f, 0.f, -1.f}, {0.f, 0.f, 1.f}, {-1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
    for (u32 f = 0; f < 4; ++f) {
        BreakableWindowSpawnDesc w{};
        w.mesh_id_intact    = 100;
        w.mesh_id_shattered = 101;
        w.fracture_seed     = static_cast<u16>((b.building_id * 17u + f) & 0xFFFFu);
        const float hx = (f < 2) ? b.width * 0.35f : 0.15f;
        const float hz = (f < 2) ? 0.15f : b.depth * 0.35f;
        w.center_ws  = float3{b.position.x + n[f].x * (b.width * 0.5f + 0.05f),
                              b.position.y + 1.4f,
                              b.position.z + n[f].z * (b.depth * 0.5f + 0.05f)};
        w.normal_ws  = n[f];
        w.half_width  = (f < 2) ? hx : hz;
        w.half_height = 0.7f;
        (void)instantiate_breakable_window(world, req, w);
        ++(*window_count);
    }
}

} // namespace

float city_gpu_terrain_height(float x, float z) {
    const float continent = fbm(x * 0.0022f, z * 0.0022f);
    const float rolling   = fbm(x * 0.008f, z * 0.008f);
    const float n         = vn(x * 0.0031f, z * 0.0031f);
    float ridge           = 1.f - std::fabs(n * 2.f - 1.f);
    ridge *= ridge;
    float t = (continent - 0.22f) / 0.36f;
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    t = t * t * (3.f - 2.f * t);
    float h = 6.f + rolling * 42.f + ridge * 280.f + ridge * ridge * 360.f;
    h *= t;
    return h + 3.f;
}

void CityGenerator::generateStreets(World& world, float3 city_center, float city_radius) {
    (void)city_radius;
    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "city_street";

    const float origin_x = city_center.x - kPitch * static_cast<float>(kBlocks) * 0.5f;
    const float origin_z = city_center.z - kPitch * static_cast<float>(kBlocks) * 0.5f;
    streets_spawned = 0;

    for (u32 i = 0; i <= kBlocks; ++i) {
        const float z = origin_z + static_cast<float>(i) * kPitch;
        StreetComponent s{};
        s.street_id         = streets_spawned + 1;
        s.start             = float3{origin_x, city_gpu_terrain_height(origin_x, z), z};
        s.end               = float3{origin_x + kPitch * static_cast<float>(kBlocks),
                                     city_gpu_terrain_height(origin_x + kPitch * static_cast<float>(kBlocks), z), z};
        s.width             = kStreetW;
        s.has_sidewalk      = 1;
        s.has_street_lights = 1;
        TransformComponent xf{};
        xf.position[0] = (s.start.x + s.end.x) * 0.5f;
        xf.position[1] = (s.start.y + s.end.y) * 0.5f;
        xf.position[2] = z;
        xf.rotation[3] = 1.f;
        xf.scale[0] = xf.scale[1] = xf.scale[2] = 1.f;
        RenderableComponent rc{};
        rc.mesh_id      = 401;
        rc.material_id  = 20;
        rc.transform_id = s.street_id;
        (void)world.instantiate(req, s, xf, rc);
        ++streets_spawned;
    }
    for (u32 i = 0; i <= kBlocks; ++i) {
        const float x = origin_x + static_cast<float>(i) * kPitch;
        StreetComponent s{};
        s.street_id         = streets_spawned + 1;
        s.start             = float3{x, city_gpu_terrain_height(x, origin_z), origin_z};
        s.end               = float3{x, city_gpu_terrain_height(x, origin_z + kPitch * static_cast<float>(kBlocks)),
                                     origin_z + kPitch * static_cast<float>(kBlocks)};
        s.width             = kStreetW;
        s.has_sidewalk      = 1;
        s.has_street_lights = 1;
        TransformComponent xf{};
        xf.position[0] = x;
        xf.position[1] = (s.start.y + s.end.y) * 0.5f;
        xf.position[2] = (s.start.z + s.end.z) * 0.5f;
        xf.rotation[3] = 1.f;
        xf.scale[0] = xf.scale[1] = xf.scale[2] = 1.f;
        RenderableComponent rc{};
        rc.mesh_id      = 401;
        rc.material_id  = 20;
        rc.transform_id = s.street_id;
        (void)world.instantiate(req, s, xf, rc);
        ++streets_spawned;
    }
}

void CityGenerator::generateCity(World& world, float3 city_center, float city_radius,
                                 u32 num_buildings) {
    ENGINE_ASSERT(num_buildings <= 1000, "city building cap");
    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "city_lot";

    MunicipalPowerNodeSpawnDesc node{};
    node.node_id      = kPowerNode;
    node.feeder_id    = 1;
    node.voltage_pu   = 1.f;
    node.frequency_hz = 60.f;
    (void)instantiate_municipal_power_node(world, req, node);

    generateStreets(world, city_center, city_radius);

    const float origin_x = city_center.x - kPitch * static_cast<float>(kBlocks) * 0.5f;
    const float origin_z = city_center.z - kPitch * static_cast<float>(kBlocks) * 0.5f;
    buildings_spawned = 0;
    lights_spawned    = 0;
    windows_spawned   = 0;

    const u32 want = num_buildings < (kBlocks * kBlocks) ? num_buildings : (kBlocks * kBlocks);
    for (u32 bz = 0; bz < kBlocks && buildings_spawned < want; ++bz) {
        for (u32 bx = 0; bx < kBlocks && buildings_spawned < want; ++bx) {
            const u32 id = buildings_spawned + 1;
            const u32 h0 = mix32(id * 747796405u + 2891336453u);
            const u32 floors = 3u + (h0 % 18u);
            const float jitter_x = (unit(mix32(h0 ^ 0xA5A5A5A5u)) - 0.5f) * 4.f;
            const float jitter_z = (unit(mix32(h0 ^ 0x3C3C3C3Cu)) - 0.5f) * 4.f;
            const float cx = origin_x + (static_cast<float>(bx) + 0.5f) * kPitch + jitter_x;
            const float cz = origin_z + (static_cast<float>(bz) + 0.5f) * kPitch + jitter_z;
            const float lot = kPitch - kStreetW - 6.f;
            const float w   = 12.f + unit(mix32(h0 + 11u)) * 8.f;
            const float d   = 10.f + unit(mix32(h0 + 29u)) * 8.f;
            const float width = w > lot ? lot : w;
            const float depth = d > lot ? lot : d;
            const float gy    = city_gpu_terrain_height(cx, cz);

            BuildingComponent b{};
            b.building_id   = id;
            b.position      = float3{cx, gy, cz};
            b.width         = width;
            b.depth         = depth;
            b.height        = static_cast<float>(floors) * kFloorH;
            b.num_floors    = floors;
            b.window_rows   = floors;
            b.window_cols   = 6;
            b.albedo_color  = palette(h0);
            b.roughness     = 0.45f + unit(mix32(h0 + 7u)) * 0.35f;

            TransformComponent xf{};
            xf.position[0] = cx;
            xf.position[1] = gy;
            xf.position[2] = cz;
            xf.rotation[3] = 1.f;
            xf.scale[0]    = width;
            xf.scale[1]    = b.height;
            xf.scale[2]    = depth;

            RenderableComponent rc{};
            rc.mesh_id      = 400;
            rc.material_id  = 21;
            rc.transform_id = id;

            (void)world.instantiate(req, b, xf, rc);
            spawn_facade_windows(world, req, b, &windows_spawned);
            ++buildings_spawned;
        }
    }

    InstantiationRequest lamp_req{};
    lamp_req.domain      = InstantiationDomain::PersistentWorld;
    lamp_req.debug_label = "city_lamp";
    for (u32 i = 0; i <= kBlocks; ++i) {
        for (u32 j = 0; j <= kBlocks; ++j) {
            if (((i + j) & 1u) != 0u) {
                continue;
            }
            const float x = origin_x + static_cast<float>(i) * kPitch;
            const float z = origin_z + static_cast<float>(j) * kPitch;
            const float y = city_gpu_terrain_height(x, z);
            StreetLightSpawnDesc lamp{};
            lamp.light_handle       = 500u + lights_spawned;
            lamp.power_grid_node_id = kPowerNode;
            lamp.flicker_probability = 0.01f;
            Entity e = instantiate_street_light(world, lamp_req, lamp);
            TransformComponent xf{};
            xf.position[0] = x + 3.5f;
            xf.position[1] = y;
            xf.position[2] = z + 3.5f;
            xf.rotation[3] = 1.f;
            xf.scale[0] = 0.25f;
            xf.scale[1] = 6.f;
            xf.scale[2] = 0.25f;
            RenderableComponent rc{};
            rc.mesh_id      = 402;
            rc.material_id  = 22;
            rc.transform_id = 500u + lights_spawned;
            world.add_component(e, xf);
            world.add_component(e, rc);
            ++lights_spawned;
        }
    }

    std::printf("[city] buildings=%u streets=%u lamps=%u windows=%u center=(%.1f,%.1f,%.1f)\n",
                buildings_spawned, streets_spawned, lights_spawned, windows_spawned, city_center.x,
                city_center.y, city_center.z);
    std::fflush(stdout);
}

} // namespace engine
