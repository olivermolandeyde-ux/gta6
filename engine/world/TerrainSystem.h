#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

inline constexpr float kTerrainChunkSizeM = 64.f;
inline constexpr u32   kTerrainLod0Verts  = 128;
inline constexpr u32   kTerrainLod1Verts  = 64;
inline constexpr u32   kTerrainLod2Verts  = 32;

struct TerrainChunkComponent {
    u32    chunk_id;
    u32    chunk_x;
    u32    chunk_z;
    u32    lod_level; // 0 = 128, 1 = 64, 2 = 32
    float3 origin_world_pos;
    float  chunk_size_m;
    u32    heightmap_texture_id;
    u32    splatmap_texture_id;
    u32    normalmap_texture_id;
};

struct TerrainHeightmapGenerator {
    u32 seed = 42;

    // height = sum(amplitude[i] * noise(frequency[i] * x, frequency[i] * z))
    // plus ridged mountains and thermal erosion. out_heights is caller-owned
    // (world/streaming arena) — never CRT.
    void generateHeightmap(float* out_heights, u32 width, u32 height, u32 seed);
    void generateNormalmap(const float* heights, u32 width, u32 height, float3* out_normals);
};

void UpdateTerrainStreamingSystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                                  CommandBuffer& cmd);

[[nodiscard]] float terrain_sample_height(const float* heights, u32 width, u32 height, float x,
                                          float z);
[[nodiscard]] u32   terrain_resident_chunk_count(World& world);

} // namespace engine
