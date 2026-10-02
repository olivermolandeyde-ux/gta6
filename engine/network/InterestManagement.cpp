#include "network/InterestManagement.h"

#include "core/Assert.h"
#include "ecs/World.h"

namespace engine {

namespace {

inline constexpr u32 kCellHashSlots = 1024;

[[nodiscard]] u32 pack_cell(u32 x, u32 y) noexcept {
    return (x * 73856093u) ^ (y * 19349663u);
}

struct CellHashSlot {
    u32 key;
    u32 cell_x;
    u32 cell_y;
    u32 head;  // index into packed entity array
    u32 count;
    u16 occupied;
};

struct PackedEnt {
    Entity entity;
    u32    next;
};

} // namespace

Entity attach_network_identity(World& world, Entity entity, u32 network_id, StreamingCellId cell,
                               bool player_controlled) {
    ENGINE_ASSERT(world.is_alive(entity), "identity attach stale");
    NetworkIdentityComponent ident{};
    ident.network_id           = network_id;
    ident.cell_id              = cell;
    ident.is_player_controlled = player_controlled;
    if (!world.has<NetworkIdentityComponent>(entity)) {
        world.add_component<NetworkIdentityComponent>(entity, ident);
    } else {
        *world.get<NetworkIdentityComponent>(entity) = ident;
    }
    if (!world.has<StreamingCellId>(entity)) {
        world.add_component<StreamingCellId>(entity, cell);
    } else {
        *world.get<StreamingCellId>(entity) = cell;
    }
    return entity;
}

AOIQueryResult CalculateAreaOfInterest(World& world, const NetworkEndpoint& client,
                                       u32 cell_radius, FrameAllocator& frame_alloc) {
    const u32 max_entities = world.entity_count();

    // Locate the peer so we know which cell they subscribe to.
    u32 home_x = 0;
    u32 home_y = 0;
    bool found = false;
    for (Entity e : world.query<NetworkPeerComponent>()) {
        const NetworkPeerComponent* peer = world.get<NetworkPeerComponent>(e);
        if (peer && endpoint_equal(peer->endpoint, client)) {
            home_x = peer->subscribe_cell_x;
            home_y = peer->subscribe_cell_y;
            found  = true;
            break;
        }
    }
    ENGINE_ASSERT(found, "AOI client endpoint unknown");

    CellHashSlot* table = frame_alloc.allocate_array<CellHashSlot>(kCellHashSlots);
    for (u32 i = 0; i < kCellHashSlots; ++i) {
        table[i].occupied = 0;
        table[i].count    = 0;
        table[i].head     = kInvalidIndex;
    }

    PackedEnt* packed = frame_alloc.allocate_array<PackedEnt>(max_entities);
    u32 packed_count  = 0;

    auto hash_insert = [&](u32 cx, u32 cy, Entity ent) {
        const u32 key = pack_cell(cx, cy);
        u32 slot      = key & (kCellHashSlots - 1);
        for (u32 probe = 0; probe < kCellHashSlots; ++probe) {
            CellHashSlot& s = table[slot];
            if (s.occupied == 0) {
                s.key      = key;
                s.cell_x   = cx;
                s.cell_y   = cy;
                s.head     = kInvalidIndex;
                s.count    = 0;
                s.occupied = 1;
            }
            if (s.occupied && s.cell_x == cx && s.cell_y == cy) {
                ENGINE_ASSERT(packed_count < max_entities, "AOI packed overflow");
                packed[packed_count].entity = ent;
                packed[packed_count].next   = s.head;
                s.head                      = packed_count;
                ++s.count;
                ++packed_count;
                return;
            }
            slot = (slot + 1) & (kCellHashSlots - 1);
        }
        ENGINE_PANIC("AOI cell hash full");
    };

    // Build once. Subsequent AOI queries for other clients can reuse this
    // table in the same frame; CalculateAreaOfInterest rebuilds because the
    // frame arena is the only legal scratch (L6).
    for (Entity e : world.query<NetworkIdentityComponent>()) {
        const NetworkIdentityComponent* id = world.get<NetworkIdentityComponent>(e);
        ENGINE_ASSERT(id != nullptr, "identity snapshot stale");
        hash_insert(id->cell_id.cell_x, id->cell_id.cell_y, e);
    }
    for (Entity e : world.query<StreamingCellId>()) {
        if (world.has<NetworkIdentityComponent>(e)) {
            continue;
        }
        const StreamingCellId* cid = world.get<StreamingCellId>(e);
        if (cid) {
            hash_insert(cid->cell_x, cid->cell_y, e);
        }
    }

    Entity* out = frame_alloc.allocate_array<Entity>(max_entities);
    u32 out_count = 0;

    auto gather_cell = [&](u32 cx, u32 cy) {
        const u32 key = pack_cell(cx, cy);
        u32 slot      = key & (kCellHashSlots - 1);
        for (u32 probe = 0; probe < kCellHashSlots; ++probe) {
            const CellHashSlot& s = table[slot];
            if (s.occupied == 0) {
                return;
            }
            if (s.cell_x == cx && s.cell_y == cy) {
                u32 node = s.head;
                while (node != kInvalidIndex) {
                    if (out_count < max_entities) {
                        out[out_count++] = packed[node].entity;
                    }
                    node = packed[node].next;
                }
                return;
            }
            slot = (slot + 1) & (kCellHashSlots - 1);
        }
    };

    const i32 r = static_cast<i32>(cell_radius);
    for (i32 dy = -r; dy <= r; ++dy) {
        for (i32 dx = -r; dx <= r; ++dx) {
            const i32 cx = static_cast<i32>(home_x) + dx;
            const i32 cy = static_cast<i32>(home_y) + dy;
            if (cx < 0 || cy < 0) {
                continue;
            }
            gather_cell(static_cast<u32>(cx), static_cast<u32>(cy));
        }
    }

    AOIQueryResult result{};
    result.entities = out;
    result.count    = out_count;
    return result;
}

bool aoi_contains(const AOIQueryResult& aoi, Entity entity) {
    for (u32 i = 0; i < aoi.count; ++i) {
        if (aoi.entities[i] == entity) {
            return true;
        }
    }
    return false;
}

} // namespace engine
