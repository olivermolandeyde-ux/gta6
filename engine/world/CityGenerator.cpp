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

constexpr u32 kPowerNode = 1;
constexpr float kFloorH  = 4.2f;
constexpr float kSetback = 4.0f;

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
    // Match GLSL: p3 = fract(vec3(p.x, p.y, p.x) * 0.1031)
    float p0 = x * 0.1031f;
    float p1 = z * 0.1031f;
    float p2 = x * 0.1031f;
    p0 -= std::floor(p0);
    p1 -= std::floor(p1);
    p2 -= std::floor(p2);
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
        px = px * 2.07f + 17.1f;
        pz = pz * 2.07f + 9.7f;
        a *= 0.5f;
    }
    return v;
}

float3 masonry_color(u32 seed, u32 district) {
    const float u0 = unit(mix32(seed));
    const float u1 = unit(mix32(seed ^ 0x9E3779B9u));
    if (district == kDistrictDowntown) {
        return float3{0.58f + u0 * 0.10f, 0.60f + u1 * 0.08f, 0.64f + u0 * 0.06f};
    }
    if (district == kDistrictIndustrial) {
        return float3{0.50f + u0 * 0.08f, 0.48f + u1 * 0.05f, 0.44f};
    }
    const float3 pal[4] = {
        {0.72f, 0.66f, 0.54f}, {0.78f, 0.74f, 0.66f}, {0.62f, 0.50f, 0.40f}, {0.70f, 0.70f, 0.68f},
    };
    return pal[seed % 4u];
}

u32 district_for_block(u32 bx, u32 bz) {
    const i32 dx = static_cast<i32>(bx) - 10;
    const i32 dz = static_cast<i32>(bz) - 10;
    const i32 cheb = dx < 0 ? -dx : dx;
    const i32 cz = dz < 0 ? -dz : dz;
    const i32 m = cheb > cz ? cheb : cz;
    if (m <= 3) {
        return kDistrictDowntown;
    }
    if (bx <= 2 || bz <= 2 || bx >= 17 || bz >= 17) {
        return kDistrictIndustrial;
    }
    return kDistrictResidential;
}

void spawn_front_window(World& world, const InstantiationRequest& req, const BuildingComponent& b,
                        u32* window_count) {
    BreakableWindowSpawnDesc w{};
    w.mesh_id_intact    = 100;
    w.mesh_id_shattered = 101;
    w.fracture_seed     = static_cast<u16>((b.building_id * 17u) & 0xFFFFu);
    w.center_ws         = float3{b.position.x, b.position.y + 1.5f, b.position.z - b.depth * 0.5f - 0.04f};
    w.normal_ws         = float3{0.f, 0.f, -1.f};
    w.half_width        = b.width * 0.22f;
    w.half_height       = 0.7f;
    (void)instantiate_breakable_window(world, req, w);
    ++(*window_count);
}

void spawn_building(World& world, const InstantiationRequest& req, CityGenerator* gen, float cx, float cz,
                    float width, float depth, u32 floors, u32 district, u32 seed) {
    const float gy = kCityPlateauY + 0.5f;
    BuildingComponent b{};
    b.building_id  = gen->buildings_spawned + 1;
    b.position     = float3{cx, gy, cz};
    b.width        = width;
    b.depth        = depth;
    b.height       = static_cast<float>(floors) * kFloorH;
    b.num_floors   = floors;
    b.window_rows  = floors;
    b.window_cols  = 6 + (floors > 20 ? 4 : 0);
    b.albedo_color = masonry_color(seed, district);
    b.roughness    = district == kDistrictDowntown ? 0.18f : 0.62f;
    b.district     = district;

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
    rc.material_id  = 21 + district;
    rc.transform_id = b.building_id;

    (void)world.instantiate(req, b, xf, rc);
    spawn_front_window(world, req, b, &gen->windows_spawned);
    ++gen->buildings_spawned;
}

} // namespace

float city_urban_mask(float x, float z) {
    const float dx = x < 0.f ? -x : (x > kCityExtentM ? x - kCityExtentM : 0.f);
    const float dz = z < 0.f ? -z : (z > kCityExtentM ? z - kCityExtentM : 0.f);
    const float outside = dx > dz ? dx : dz;
    float t = outside / 80.f;
    t       = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    t       = t * t * (3.f - 2.f * t);
    return 1.f - t;
}

float city_natural_terrain_height(float x, float z) {
    const float continent = fbm(x * 0.0022f, z * 0.0022f);
    const float rolling   = fbm(x * 0.008f, z * 0.008f);
    const float n         = vn(x * 0.0031f, z * 0.0031f);
    float ridge           = 1.f - std::fabs(n * 2.f - 1.f);
    ridge *= ridge;
    float t = (continent - 0.22f) / 0.36f;
    t       = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    t       = t * t * (3.f - 2.f * t);
    float h = 6.f + rolling * 42.f + ridge * 280.f + ridge * ridge * 360.f;
    h *= t;
    return h + 3.f;
}

float city_gpu_terrain_height(float x, float z) {
    const float natural = city_natural_terrain_height(x, z);
    const float w       = city_urban_mask(x, z);
    return natural * (1.f - w) + kCityPlateauY * w;
}

void CityGenerator::generateStreets(World& world, float3 city_center, float city_radius) {
    (void)city_radius;
    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "city_street";

    const float origin_x = city_center.x - kCityBlockPitch * static_cast<float>(kCityBlocks) * 0.5f;
    const float origin_z = city_center.z - kCityBlockPitch * static_cast<float>(kCityBlocks) * 0.5f;
    const float span     = kCityBlockPitch * static_cast<float>(kCityBlocks);
    streets_spawned      = 0;

    for (u32 i = 0; i <= kCityBlocks; ++i) {
        const float z = origin_z + static_cast<float>(i) * kCityBlockPitch;
        StreetComponent s{};
        s.street_id         = streets_spawned + 1;
        s.start             = float3{origin_x, kCityPlateauY, z};
        s.end               = float3{origin_x + span, kCityPlateauY, z};
        s.width             = kCityStreetWidth;
        s.has_sidewalk      = 1;
        s.has_street_lights = 1;
        TransformComponent xf{};
        xf.position[0] = city_center.x;
        xf.position[1] = kCityPlateauY;
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
    for (u32 i = 0; i <= kCityBlocks; ++i) {
        const float x = origin_x + static_cast<float>(i) * kCityBlockPitch;
        StreetComponent s{};
        s.street_id         = streets_spawned + 1;
        s.start             = float3{x, kCityPlateauY, origin_z};
        s.end               = float3{x, kCityPlateauY, origin_z + span};
        s.width             = kCityStreetWidth;
        s.has_sidewalk      = 1;
        s.has_street_lights = 1;
        TransformComponent xf{};
        xf.position[0] = x;
        xf.position[1] = kCityPlateauY;
        xf.position[2] = city_center.z;
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
    ENGINE_ASSERT(num_buildings <= 4000, "city building cap");
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

    const float origin_x = city_center.x - kCityBlockPitch * static_cast<float>(kCityBlocks) * 0.5f;
    const float origin_z = city_center.z - kCityBlockPitch * static_cast<float>(kCityBlocks) * 0.5f;
    buildings_spawned    = 0;
    lights_spawned       = 0;
    windows_spawned      = 0;

    const float lot = kCityBlockPitch - kCityStreetWidth - kSetback * 2.f;

    for (u32 bz = 0; bz < kCityBlocks; ++bz) {
        for (u32 bx = 0; bx < kCityBlocks; ++bx) {
            if (buildings_spawned >= num_buildings) {
                bx = kCityBlocks;
                bz = kCityBlocks;
                break;
            }
            const u32 district = district_for_block(bx, bz);
            const float lot_x0 =
                origin_x + static_cast<float>(bx) * kCityBlockPitch + kCityStreetWidth * 0.5f + kSetback;
            const float lot_z0 =
                origin_z + static_cast<float>(bz) * kCityBlockPitch + kCityStreetWidth * 0.5f + kSetback;
            const u32 seed = mix32(bx * 73856093u ^ bz * 19349663u ^ 83492791u);

            if (district == kDistrictDowntown) {
                const u32 s      = mix32(seed + 3u);
                const float bw   = lot * 0.92f;
                const float bd   = lot * 0.92f;
                const u32 floors = 15u + (s % 14u); // 63–118 m
                const float cx   = lot_x0 + lot * 0.5f;
                const float cz   = lot_z0 + lot * 0.5f;
                spawn_building(world, req, this, cx, cz, bw, bd, floors, district, s);
            } else if (district == kDistrictIndustrial) {
                const u32 s      = mix32(seed + 9u);
                const float bw   = lot * (0.72f + unit(s) * 0.18f);
                const float bd   = lot * (0.55f + unit(mix32(s + 3)) * 0.25f);
                const u32 floors = 5u + (s % 4u);
                const float cx   = lot_x0 + lot * 0.5f;
                const float cz   = lot_z0 + lot * 0.5f;
                spawn_building(world, req, this, cx, cz, bw, bd, floors, district, s);
            } else {
                const float gap = 2.0f;
                const float bw  = (lot - gap) * 0.5f;
                const float bd  = lot * (0.42f + unit(seed) * 0.18f);
                for (u32 ix = 0; ix < 2; ++ix) {
                    if (buildings_spawned >= num_buildings) {
                        break;
                    }
                    const u32 s      = mix32(seed + ix * 23u);
                    const u32 floors = 5u + (s % 6u);
                    const float cx   = lot_x0 + bw * 0.5f + static_cast<float>(ix) * (bw + gap);
                    const float cz   = lot_z0 + bd * 0.5f + unit(mix32(s + 5)) * (lot - bd) * 0.35f;
                    spawn_building(world, req, this, cx, cz, bw, bd, floors, district, s);
                }
            }
        }
    }

    InstantiationRequest lamp_req{};
    lamp_req.domain      = InstantiationDomain::PersistentWorld;
    lamp_req.debug_label = "city_lamp";
    for (u32 i = 0; i <= kCityBlocks; ++i) {
        for (u32 j = 0; j <= kCityBlocks; ++j) {
            const float x = origin_x + static_cast<float>(i) * kCityBlockPitch;
            const float z = origin_z + static_cast<float>(j) * kCityBlockPitch;
            const float y = kCityPlateauY;
            StreetLightSpawnDesc lamp{};
            lamp.light_handle        = 500u + lights_spawned;
            lamp.power_grid_node_id  = kPowerNode;
            lamp.flicker_probability = 0.01f;
            Entity e                 = instantiate_street_light(world, lamp_req, lamp);
            TransformComponent xf{};
            xf.position[0] = x + 8.5f;
            xf.position[1] = y;
            xf.position[2] = z + 8.5f;
            xf.rotation[3] = 1.f;
            xf.scale[0]    = 0.22f;
            xf.scale[1]    = 7.2f;
            xf.scale[2]    = 0.22f;
            RenderableComponent rc{};
            rc.mesh_id      = 402;
            rc.material_id  = 22;
            rc.transform_id = 500u + lights_spawned;
            world.add_component(e, xf);
            world.add_component(e, rc);
            ++lights_spawned;
        }
    }

    std::printf("[city] buildings=%u streets=%u lamps=%u windows=%u plateau=%.1f extent=%.0f\n",
                buildings_spawned, streets_spawned, lights_spawned, windows_spawned, kCityPlateauY,
                kCityExtentM);
    std::fflush(stdout);
}

} // namespace engine
