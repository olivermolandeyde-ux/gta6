#include "world/TerrainSystem.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "render/RenderPipeline.h"
#include "world/WorldStreamer.h"

#include <cmath>
#include <cstring>

namespace engine {

namespace {

float hash01(u32 n) {
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return static_cast<float>(n >> 8) * (1.f / 16777216.f);
}

float value_noise(float x, float z, u32 seed) {
    const i32 ix = static_cast<i32>(std::floor(x));
    const i32 iz = static_cast<i32>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fz = z - static_cast<float>(iz);
    const float u = fx * fx * (3.f - 2.f * fx);
    const float v = fz * fz * (3.f - 2.f * fz);
    const auto at = [seed](i32 i, i32 j) {
        u32 n = seed;
        n ^= static_cast<u32>(i) * 374761393u;
        n ^= static_cast<u32>(j) * 668265263u;
        return hash01(n);
    };
    const float a = at(ix, iz);
    const float b = at(ix + 1, iz);
    const float c = at(ix, iz + 1);
    const float d = at(ix + 1, iz + 1);
    const float ab = a * (1.f - u) + b * u;
    const float cd = c * (1.f - u) + d * u;
    return ab * (1.f - v) + cd * v;
}

float fbm(float x, float z, u32 seed, u32 octaves) {
    float h = 0.f;
    float amp = 0.5f;
    float freq = 1.f;
    float nrm = 0.f;
    for (u32 i = 0; i < octaves; ++i) {
        h += amp * value_noise(x * freq, z * freq, seed + i * 19u);
        nrm += amp;
        amp *= 0.5f;
        freq *= 2.07f;
    }
    return nrm > 0.f ? h / nrm : 0.f;
}

float ridge_n(float x, float z, u32 seed) {
    const float n = value_noise(x, z, seed);
    float r = 1.f - std::fabs(n * 2.f - 1.f);
    return r * r;
}

void thermal_erode(float* h, u32 w, u32 ht, u32 iters) {
    const float talus = 0.018f;
    for (u32 it = 0; it < iters; ++it) {
        for (u32 z = 1; z + 1 < ht; ++z) {
            for (u32 x = 1; x + 1 < w; ++x) {
                const u32 i = z * w + x;
                float max_dh = 0.f;
                u32 nj = i;
                const u32 nbs[4] = {i - 1, i + 1, i - w, i + w};
                for (u32 k = 0; k < 4; ++k) {
                    const float dh = h[i] - h[nbs[k]];
                    if (dh > max_dh) {
                        max_dh = dh;
                        nj = nbs[k];
                    }
                }
                if (max_dh > talus) {
                    const float mv = (max_dh - talus) * 0.22f;
                    h[i] -= mv;
                    h[nj] += mv;
                }
            }
        }
    }
}

u32 lod_for_distance(float dist_m) {
    if (dist_m < 90.f) {
        return 0;
    }
    if (dist_m < 180.f) {
        return 1;
    }
    return 2;
}

bool chunk_exists(World& world, u32 cx, u32 cz) {
    for (Entity e : world.query<TerrainChunkComponent>()) {
        const TerrainChunkComponent* c = world.get<TerrainChunkComponent>(e);
        if (c && c->chunk_x == cx && c->chunk_z == cz) {
            return true;
        }
    }
    return false;
}

} // namespace

void TerrainHeightmapGenerator::generateHeightmap(float* out_heights, u32 width, u32 height,
                                                  u32 gen_seed) {
    ENGINE_ASSERT(out_heights != nullptr, "heightmap dest");
    ENGINE_ASSERT(width >= 8 && height >= 8, "heightmap size");
    seed = gen_seed;
    for (u32 z = 0; z < height; ++z) {
        for (u32 x = 0; x < width; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(width - 1);
            const float fz = static_cast<float>(z) / static_cast<float>(height - 1);
            const float wx = fx * 512.f;
            const float wz = fz * 512.f;
            const float continent = fbm(wx * 0.0022f, wz * 0.0022f, seed, 5);
            const float rolling = fbm(wx * 0.008f, wz * 0.008f, seed + 3, 5);
            const float ridge = ridge_n(wx * 0.0031f, wz * 0.0031f, seed + 11);
            float h = 6.f + rolling * 42.f + ridge * 280.f + ridge * ridge * 360.f;
            const float land = clampf((continent - 0.22f) / 0.36f, 0.f, 1.f);
            h *= land;
            h += 3.f;
            out_heights[z * width + x] = h;
        }
    }
    thermal_erode(out_heights, width, height, 6);
}

void TerrainHeightmapGenerator::generateNormalmap(const float* heights, u32 width, u32 height,
                                                  float3* out_normals) {
    ENGINE_ASSERT(heights != nullptr && out_normals != nullptr, "normalmap dest");
    for (u32 z = 0; z < height; ++z) {
        for (u32 x = 0; x < width; ++x) {
            const u32 xl = x > 0 ? x - 1 : x;
            const u32 xr = x + 1 < width ? x + 1 : x;
            const u32 zb = z > 0 ? z - 1 : z;
            const u32 zf = z + 1 < height ? z + 1 : z;
            const float dx = heights[z * width + xr] - heights[z * width + xl];
            const float dz = heights[zf * width + x] - heights[zb * width + x];
            const float step = 512.f / static_cast<float>(width - 1);
            out_normals[z * width + x] =
                float3_normalize_or(float3{-dx, 2.f * step, -dz}, float3{0.f, 1.f, 0.f});
        }
    }
}

float terrain_sample_height(const float* heights, u32 width, u32 height, float x, float z) {
    ENGINE_ASSERT(heights != nullptr, "sample heights");
    const float u = clampf(x, 0.f, 1.f) * static_cast<float>(width - 1);
    const float v = clampf(z, 0.f, 1.f) * static_cast<float>(height - 1);
    const u32 x0 = static_cast<u32>(u);
    const u32 z0 = static_cast<u32>(v);
    const u32 x1 = min_of(x0 + 1, width - 1);
    const u32 z1 = min_of(z0 + 1, height - 1);
    const float tx = u - static_cast<float>(x0);
    const float tz = v - static_cast<float>(z0);
    const float a = heights[z0 * width + x0];
    const float b = heights[z0 * width + x1];
    const float c = heights[z1 * width + x0];
    const float d = heights[z1 * width + x1];
    return (a * (1.f - tx) + b * tx) * (1.f - tz) + (c * (1.f - tx) + d * tx) * tz;
}

u32 terrain_resident_chunk_count(World& world) {
    u32 n = 0;
    for (Entity e : world.query<TerrainChunkComponent>()) {
        (void)e;
        ++n;
    }
    return n;
}

void UpdateTerrainStreamingSystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                                  CommandBuffer& cmd) {
    (void)delta_time;
    float3 observer = float3{0.f, 0.f, 0.f};
    bool has_obs = false;
    for (Entity e : world.query<StreamObserverComponent>()) {
        const StreamObserverComponent* o = world.get<StreamObserverComponent>(e);
        if (o) {
            observer = o->world_pos;
            has_obs = true;
            break;
        }
    }
    if (!has_obs) {
        return;
    }

    const float cell_m = streaming_cell_size_m(world);
    const u32 home_x = static_cast<u32>(observer.x < 0.f ? 0.f : observer.x / cell_m);
    const u32 home_z = static_cast<u32>(observer.z < 0.f ? 0.f : observer.z / cell_m);

    const u32 max_e = world.entity_count();
    Entity* snap = frame_alloc.allocate_array<Entity>(max_e);
    u32 nsnap = 0;
    for (Entity e : world.query<TerrainChunkComponent>()) {
        if (nsnap < max_e) {
            snap[nsnap++] = e;
        }
    }
    for (u32 i = 0; i < nsnap; ++i) {
        TerrainChunkComponent* c = world.get<TerrainChunkComponent>(snap[i]);
        if (!c) {
            continue;
        }
        if (!streaming_cell_is_loaded(world, c->chunk_x, c->chunk_z)) {
            cmd.destroy_entity(snap[i]);
            continue;
        }
        const float3 d = float3_sub(observer, c->origin_world_pos);
        c->lod_level = lod_for_distance(float3_length(d));
    }

    // Spawn a terrain chunk for every resident WorldStreamer cell (Phase 6).
    for (i32 dz = -3; dz <= 3; ++dz) {
        for (i32 dx = -3; dx <= 3; ++dx) {
            const i32 cx = static_cast<i32>(home_x) + dx;
            const i32 cz = static_cast<i32>(home_z) + dz;
            if (cx < 0 || cz < 0) {
                continue;
            }
            const u32 ux = static_cast<u32>(cx);
            const u32 uz = static_cast<u32>(cz);
            if (!streaming_cell_is_loaded(world, ux, uz)) {
                continue;
            }
            if (chunk_exists(world, ux, uz)) {
                continue;
            }
            InstantiationRequest req{};
            req.domain = InstantiationDomain::StreamingCell;
            req.cell.cell_x = ux;
            req.cell.cell_y = uz;
            req.debug_label = "terrain_chunk";

            TerrainChunkComponent tc{};
            tc.chunk_id = ux * 73856093u ^ uz * 19349663u;
            tc.chunk_x = ux;
            tc.chunk_z = uz;
            tc.lod_level = lod_for_distance(std::sqrt(static_cast<float>(dx * dx + dz * dz)) * cell_m);
            tc.origin_world_pos = float3{static_cast<float>(ux) * cell_m, 0.f,
                                         static_cast<float>(uz) * cell_m};
            tc.chunk_size_m = kTerrainChunkSizeM;
            tc.heightmap_texture_id = 100u + (ux & 15u);
            tc.splatmap_texture_id = 200u + (uz & 15u);
            tc.normalmap_texture_id = 300u + ((ux + uz) & 15u);

            RenderableComponent rc{};
            rc.mesh_id = 3;
            rc.material_id = 10;
            rc.transform_id = 0;

            TransformComponent xf{};
            xf.position[0] = tc.origin_world_pos.x;
            xf.position[1] = 0.f;
            xf.position[2] = tc.origin_world_pos.z;
            xf.rotation[3] = 1.f;
            xf.scale[0] = xf.scale[1] = xf.scale[2] = 1.f;

            (void)world.instantiate(req, tc, rc, xf);
        }
    }
}

} // namespace engine
