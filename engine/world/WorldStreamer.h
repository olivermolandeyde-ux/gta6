#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/LinearAllocator.h"

namespace engine {

class World;

inline constexpr u32 kMaxResidentCells     = 32;
inline constexpr u32 kStreamerChunkHandles = 16;
inline constexpr float kDefaultCellSizeM   = 32.0f;

struct StreamObserverComponent {
    float3 world_pos;
};

struct WorldStreamerComponent {
    u32    player_entity_id;
    float3 player_world_pos;
    u32    stream_radius_cells;       // e.g., 3x3 grid around player
    float  stream_threshold_distance; // Distance before triggering load/unload
};

struct StreamingCellData {
    StreamingCellId cell_id;
    u32  archetype_chunk_handles[16]; // Handles to the ECS chunks belonging to this cell
    u32  entity_count;
    bool is_loaded;
    u32  arena_slot;
};

[[nodiscard]] Entity instantiate_world_streamer(World& world, const InstantiationRequest& request,
                                                Entity observer, u32 stream_radius_cells,
                                                float cell_size_m = kDefaultCellSizeM);

void UpdateWorldStreamerSystem(World& world, float delta_time, CommandBuffer& cmd);

[[nodiscard]] bool  streaming_cell_is_loaded(World& world, u32 cell_x, u32 cell_y);
[[nodiscard]] usize streaming_cell_arena_used(World& world, u32 cell_x, u32 cell_y);
[[nodiscard]] u32   streaming_cell_entity_count(World& world, u32 cell_x, u32 cell_y);
[[nodiscard]] float streaming_cell_size_m(World& world);

} // namespace engine
