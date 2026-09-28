#include "world/WorldStreamer.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "memory/MemorySystem.h"

#include <cstring>

namespace engine {

namespace {

struct WorldStreamBoard {
    float           cell_size_m;
    u32             world_id;
    u32             resident_count;
    StreamingCellData cells[kMaxResidentCells];
    LinearAllocator arenas[kMaxResidentCells];
    u8*             arena_base;
    usize           slice_bytes;
};

WorldStreamBoard* g_boards[8]{};

[[nodiscard]] WorldStreamBoard* board_for(u16 world_id) {
    ENGINE_ASSERT(world_id < 8, "world_id out of streamer board range");
    return g_boards[world_id];
}

[[nodiscard]] StreamingCellId cell_of(float3 pos, float cell_size) {
    StreamingCellId id{};
    const float x = pos.x / cell_size;
    const float z = pos.z / cell_size;
    id.cell_x = static_cast<u32>(x < 0.f ? 0 : x);
    id.cell_y = static_cast<u32>(z < 0.f ? 0 : z);
    id.lod    = 0;
    id._pad   = 0;
    return id;
}

[[nodiscard]] i32 chebyshev(u32 ax, u32 ay, u32 bx, u32 by) {
    const i32 dx = static_cast<i32>(ax) - static_cast<i32>(bx);
    const i32 dy = static_cast<i32>(ay) - static_cast<i32>(by);
    const i32 adx = dx < 0 ? -dx : dx;
    const i32 ady = dy < 0 ? -dy : dy;
    return adx > ady ? adx : ady;
}

[[nodiscard]] i32 find_loaded(WorldStreamBoard* board, u32 cx, u32 cy) {
    for (u32 i = 0; i < kMaxResidentCells; ++i) {
        if (board->cells[i].is_loaded && board->cells[i].cell_id.cell_x == cx
            && board->cells[i].cell_id.cell_y == cy) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

[[nodiscard]] i32 find_free_slot(WorldStreamBoard* board) {
    for (u32 i = 0; i < kMaxResidentCells; ++i) {
        if (!board->cells[i].is_loaded) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

void load_cell(World& world, WorldStreamBoard* board, StreamingCellId id) {
    const i32 slot = find_free_slot(board);
    ENGINE_ASSERT(slot >= 0, "resident cell cap reached");

    StreamingCellData& cell = board->cells[slot];
    LinearAllocator&   arena = board->arenas[slot];
    arena.reset();

    // Cell-local scratch lives in this slice of the streaming arena.
    // Prefab bake headers, density maps, and spawn seeds — never entity bytes
    // (those live in ECS chunks). Resetting the bump pointer is O(1) and
    // leaves zero fragmentation (L1).
    struct CellScratch {
        u32 magic;
        u32 seed;
        u32 cell_x;
        u32 cell_y;
        u32 spawn_count;
    };
    auto* scratch = static_cast<CellScratch*>(arena.allocate(sizeof(CellScratch), alignof(CellScratch)));
    scratch->magic       = 0xC0DEC311u;
    scratch->seed        = id.cell_x * 73856093u ^ id.cell_y * 19349663u;
    scratch->cell_x      = id.cell_x;
    scratch->cell_y      = id.cell_y;
    scratch->spawn_count = 2;

    InstantiationRequest req{};
    req.domain          = InstantiationDomain::StreamingCell;
    req.cell            = id;
    req.debug_label     = "stream_resident";

    struct StreamingResidentMarker {
        u32 seed;
        u32 ordinal;
    };

    u32 spawned = 0;
    for (u32 n = 0; n < scratch->spawn_count; ++n) {
        StreamingResidentMarker marker{};
        marker.seed    = scratch->seed;
        marker.ordinal = n;
        (void)world.instantiate(req, marker);
        ++spawned;
    }

    std::memset(cell.archetype_chunk_handles, 0, sizeof(cell.archetype_chunk_handles));
    cell.cell_id      = id;
    cell.entity_count = spawned;
    cell.is_loaded    = true;
    cell.arena_slot   = static_cast<u32>(slot);
    if (world.archetype_count() > 0) {
        cell.archetype_chunk_handles[0] = world.archetype_count() - 1;
    }
}

void unload_cell(World& world, WorldStreamBoard* board, u32 slot, CommandBuffer& cmd) {
    StreamingCellData& cell = board->cells[slot];
    ENGINE_ASSERT(cell.is_loaded, "unload of empty slot");

    const StreamingCellId id = cell.cell_id;
    FrameAllocator& frame = world.memory().frame();
    const u32 max_entities = world.entity_count();
    Entity* snap = frame.allocate_array<Entity>(max_entities);
    u32 count = 0;
    for (Entity e : world.query<StreamingCellId>()) {
        if (count < max_entities) {
            snap[count++] = e;
        }
    }
    for (u32 i = 0; i < count; ++i) {
        const StreamingCellId* cid = world.get<StreamingCellId>(snap[i]);
        if (cid && cid->cell_x == id.cell_x && cid->cell_y == id.cell_y) {
            cmd.destroy_entity(snap[i]);
        }
    }

    // Return the bump pointer to 0. The slice is reusable for the next
    // resident of this slot; no free-list, no holes, no CRT heap.
    board->arenas[slot].reset();
    ENGINE_ASSERT(board->arenas[slot].used() == 0, "cell arena failed to reset");

    cell.is_loaded    = false;
    cell.entity_count = 0;
    std::memset(cell.archetype_chunk_handles, 0, sizeof(cell.archetype_chunk_handles));
}

} // namespace

Entity instantiate_world_streamer(World& world, const InstantiationRequest& request,
                                  Entity observer, u32 stream_radius_cells, float cell_size_m) {
    validate_instantiation(request);
    ENGINE_ASSERT(world.is_alive(observer), "stream observer must be alive");
    ENGINE_ASSERT(cell_size_m > 1.f, "cell size");

    const u16 wid = world.world_id();
    ENGINE_ASSERT(wid < 8, "world_id");
    if (!g_boards[wid]) {
        Allocator world_alloc = world.memory().world_allocator();
        auto* board = static_cast<WorldStreamBoard*>(
            world_alloc.allocate(sizeof(WorldStreamBoard), alignof(WorldStreamBoard)));
        std::memset(board, 0, sizeof(WorldStreamBoard));
        board->cell_size_m = cell_size_m;
        board->world_id    = wid;

        // Carve the streaming arena into equal per-cell LinearAllocators.
        LinearAllocator& streaming = world.memory().streaming_arena();
        const usize slice = 256 * 1024;
        u8* base = static_cast<u8*>(streaming.allocate(slice * kMaxResidentCells, kCacheLineBytes));
        board->arena_base  = base;
        board->slice_bytes = slice;
        for (u32 i = 0; i < kMaxResidentCells; ++i) {
            board->arenas[i].bind(base + i * slice, slice, "cell_stream");
            board->cells[i].is_loaded = false;
        }
        g_boards[wid] = board;
    }

    WorldStreamerComponent streamer{};
    streamer.player_entity_id         = observer.index();
    streamer.player_world_pos         = float3{0.f, 0.f, 0.f};
    streamer.stream_radius_cells      = stream_radius_cells;
    streamer.stream_threshold_distance = cell_size_m * 0.15f;

    if (!world.has<StreamObserverComponent>(observer)) {
        StreamObserverComponent obs{};
        obs.world_pos = float3{0.f, 0.f, 0.f};
        world.add_component<StreamObserverComponent>(observer, obs);
    }

    return world.instantiate(request, streamer);
}

void UpdateWorldStreamerSystem(World& world, float delta_time, CommandBuffer& cmd) {
    (void)delta_time;
    WorldStreamBoard* board = board_for(world.world_id());
    ENGINE_ASSERT(board != nullptr, "streamer board missing");

    FrameAllocator& frame = world.memory().frame();
    const u32 max_entities = world.entity_count();
    Entity* snap = frame.allocate_array<Entity>(max_entities);
    u32 count = 0;
    for (Entity e : world.query<WorldStreamerComponent>()) {
        if (count < max_entities) {
            snap[count++] = e;
        }
    }
    if (count == 0) {
        return;
    }

    WorldStreamerComponent* streamer = world.get<WorldStreamerComponent>(snap[0]);
    ENGINE_ASSERT(streamer != nullptr, "streamer snapshot stale");

    // Resolve observer pose. Entity handle is rebuilt from the stored index
    // by scanning live observers (generation-safe, no stashed pointers).
    float3 player_pos = streamer->player_world_pos;
    for (Entity e : world.query<StreamObserverComponent>()) {
        if (e.index() == streamer->player_entity_id) {
            const StreamObserverComponent* obs = world.get<StreamObserverComponent>(e);
            if (obs) {
                player_pos = obs->world_pos;
            }
            break;
        }
    }
    streamer->player_world_pos = player_pos;

    const StreamingCellId home = cell_of(player_pos, board->cell_size_m);
    const u32 radius = streamer->stream_radius_cells;

    bool wanted[kMaxResidentCells]{};
    for (u32 i = 0; i < kMaxResidentCells; ++i) {
        if (!board->cells[i].is_loaded) {
            continue;
        }
        const i32 d = chebyshev(board->cells[i].cell_id.cell_x, board->cells[i].cell_id.cell_y,
                                home.cell_x, home.cell_y);
        wanted[i] = d <= static_cast<i32>(radius);
        if (!wanted[i]) {
            unload_cell(world, board, i, cmd);
        }
    }

    const i32 r = static_cast<i32>(radius);
    for (i32 dy = -r; dy <= r; ++dy) {
        for (i32 dx = -r; dx <= r; ++dx) {
            const i32 cx = static_cast<i32>(home.cell_x) + dx;
            const i32 cy = static_cast<i32>(home.cell_y) + dy;
            if (cx < 0 || cy < 0) {
                continue;
            }
            if (find_loaded(board, static_cast<u32>(cx), static_cast<u32>(cy)) >= 0) {
                continue;
            }
            StreamingCellId id{};
            id.cell_x = static_cast<u32>(cx);
            id.cell_y = static_cast<u32>(cy);
            load_cell(world, board, id);
        }
    }
}

bool streaming_cell_is_loaded(World& world, u32 cell_x, u32 cell_y) {
    WorldStreamBoard* board = board_for(world.world_id());
    if (!board) {
        return false;
    }
    return find_loaded(board, cell_x, cell_y) >= 0;
}

usize streaming_cell_arena_used(World& world, u32 cell_x, u32 cell_y) {
    WorldStreamBoard* board = board_for(world.world_id());
    if (!board) {
        return 0;
    }
    const i32 slot = find_loaded(board, cell_x, cell_y);
    if (slot < 0) {
        // Unloaded slots have been reset; report 0.
        for (u32 i = 0; i < kMaxResidentCells; ++i) {
            if (board->cells[i].cell_id.cell_x == cell_x && board->cells[i].cell_id.cell_y == cell_y
                && !board->cells[i].is_loaded) {
                return board->arenas[i].used();
            }
        }
        return 0;
    }
    return board->arenas[slot].used();
}

u32 streaming_cell_entity_count(World& world, u32 cell_x, u32 cell_y) {
    WorldStreamBoard* board = board_for(world.world_id());
    if (!board) {
        return 0;
    }
    const i32 slot = find_loaded(board, cell_x, cell_y);
    if (slot < 0) {
        return 0;
    }
    return board->cells[slot].entity_count;
}

float streaming_cell_size_m(World& world) {
    WorldStreamBoard* board = board_for(world.world_id());
    return board ? board->cell_size_m : kDefaultCellSizeM;
}

} // namespace engine
