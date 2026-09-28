#include "network/NetworkCore.h"
#include "network/InterestManagement.h"
#include "network/StateReplication.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "memory/MemorySystem.h"
#include "memory/PoolAllocator.h"

#include <cstring>

namespace engine {

namespace {

inline constexpr u32 kMaxQueuedRpc = 256;

struct alignas(8) RpcSlot {
    NetworkRPC      rpc;
    NetworkEndpoint src;
    NetworkEndpoint dst;
    u32             next;
};

struct NetFabric {
    PoolAllocator rpc_pool;
    RpcSlot*      slots;
    u32           head;
    u32           count;
    u32           cap;
    bool          booted;
};

NetFabric g_fabric[8]{};

[[nodiscard]] NetFabric& fabric(World& world) {
    const u16 id = world.world_id();
    ENGINE_ASSERT(id < 8, "world_id");
    NetFabric& f = g_fabric[id];
    if (!f.booted) {
        Allocator world_alloc = world.memory().world_allocator();
        const usize bytes = sizeof(RpcSlot) * kMaxQueuedRpc;
        void* mem = world_alloc.allocate(bytes, alignof(RpcSlot));
        f.rpc_pool.bind(mem, bytes, sizeof(RpcSlot), alignof(RpcSlot), "rpc_queue");
        f.slots   = static_cast<RpcSlot*>(mem);
        f.head    = kInvalidIndex;
        f.count   = 0;
        f.cap     = kMaxQueuedRpc;
        f.booted  = true;
    }
    return f;
}

} // namespace

PacketBuffer make_packet_buffer(FrameAllocator& frame_alloc, u32 bytes) {
    PacketBuffer p{};
    p.data         = static_cast<u8*>(frame_alloc.allocate(bytes, 16));
    p.capacity     = bytes;
    p.write_offset = 0;
    p.read_offset  = 0;
    return p;
}

void packet_write_u32(PacketBuffer& packet, u32 value) {
    ENGINE_ASSERT(packet.write_offset + 4 <= packet.capacity, "packet overflow");
    std::memcpy(packet.data + packet.write_offset, &value, 4);
    packet.write_offset += 4;
}

void packet_write_f32(PacketBuffer& packet, float value) {
    ENGINE_ASSERT(packet.write_offset + 4 <= packet.capacity, "packet overflow");
    std::memcpy(packet.data + packet.write_offset, &value, 4);
    packet.write_offset += 4;
}

u32 packet_read_u32(PacketBuffer& packet) {
    ENGINE_ASSERT(packet.read_offset + 4 <= packet.write_offset, "packet underrun");
    u32 value = 0;
    std::memcpy(&value, packet.data + packet.read_offset, 4);
    packet.read_offset += 4;
    return value;
}

float packet_read_f32(PacketBuffer& packet) {
    ENGINE_ASSERT(packet.read_offset + 4 <= packet.write_offset, "packet underrun");
    float value = 0.f;
    std::memcpy(&value, packet.data + packet.read_offset, 4);
    packet.read_offset += 4;
    return value;
}

void enqueue_network_rpc(World& world, const NetworkRPC& rpc, NetworkEndpoint src,
                         NetworkEndpoint dst) {
    NetFabric& f = fabric(world);
    auto* slot = static_cast<RpcSlot*>(f.rpc_pool.allocate());
    slot->rpc  = rpc;
    slot->src  = src;
    slot->dst  = dst;
    slot->next = f.head;
    f.head     = static_cast<u32>(slot - f.slots);
    ++f.count;
}

u32 drain_network_rpc(World& world, NetworkRPC* out, NetworkEndpoint* src, NetworkEndpoint* dst,
                      u32 max_out) {
    NetFabric& f = fabric(world);
    u32 n = 0;
    while (f.head != kInvalidIndex && n < max_out) {
        RpcSlot* slot = f.slots + f.head;
        const u32 next = slot->next;
        out[n]  = slot->rpc;
        src[n]  = slot->src;
        dst[n]  = slot->dst;
        f.head  = next;
        f.rpc_pool.deallocate(slot);
        --f.count;
        ++n;
    }
    return n;
}

Entity instantiate_network_peer(World& world, const InstantiationRequest& request,
                                NetworkEndpoint endpoint, bool authority, u32 cell_x, u32 cell_y) {
    validate_instantiation(request);
    NetworkPeerComponent peer{};
    peer.endpoint         = endpoint;
    peer.is_authority     = authority ? 1 : 0;
    peer.is_client        = authority ? 0 : 1;
    peer._pad             = 0;
    peer.subscribe_cell_x = cell_x;
    peer.subscribe_cell_y = cell_y;

    ClientNetInboxComponent inbox{};
    inbox.received_count             = 0;
    inbox.last_shattered_network_id  = 0;
    for (u32 i = 0; i < 8; ++i) {
        inbox.received_rpc_type[i] = 0;
    }

    StreamingCellId cell{};
    cell.cell_x = cell_x;
    cell.cell_y = cell_y;
    cell.lod    = 0;
    cell._pad   = 0;

    NetworkIdentityComponent ident{};
    ident.network_id           = endpoint.port;
    ident.cell_id              = cell;
    ident.is_player_controlled = !authority;

    ClientPredictionComponent pred{};
    pred.last_acknowledged_input_sequence = 0;
    pred.predicted_position               = float3{static_cast<float>(cell_x) * 32.f, 0.f,
                                     static_cast<float>(cell_y) * 32.f};
    pred.predicted_velocity               = float3{0.f, 0.f, 0.f};

    ServerStateComponent srv{};
    srv.server_position     = pred.predicted_position;
    srv.server_velocity     = float3{0.f, 0.f, 0.f};
    srv.server_sequence     = 0;
    srv.interpolation_alpha = 0.f;

    return world.instantiate(request, peer, inbox, ident, pred, srv, cell);
}

void ProcessNetworkPackets(World& world, float delta_time, FrameAllocator& frame_alloc) {
    (void)delta_time;
    NetworkRPC rpcs[64];
    NetworkEndpoint srcs[64];
    NetworkEndpoint dsts[64];
    const u32 n = drain_network_rpc(world, rpcs, srcs, dsts, 64);

    PacketBuffer packet = make_packet_buffer(frame_alloc, 2048);
    packet_write_u32(packet, n);
    for (u32 i = 0; i < n; ++i) {
        packet_write_u32(packet, srcs[i].ip_address);
        packet_write_u32(packet, srcs[i].port);
        packet_write_u32(packet, dsts[i].ip_address);
        packet_write_u32(packet, dsts[i].port);
        packet_write_u32(packet, rpcs[i].rpc_type);
        packet_write_u32(packet, rpcs[i].target_entity_network_id);
        packet_write_f32(packet, rpcs[i].payload_pos.x);
        packet_write_f32(packet, rpcs[i].payload_pos.y);
        packet_write_f32(packet, rpcs[i].payload_pos.z);
    }

    // Re-queue deserialized datagrams so replication can consume them this frame.
    // FrameAllocator packet bytes die at begin_frame; the RPC pool is the durable queue.
    packet.read_offset = 0;
    const u32 count    = packet_read_u32(packet);
    for (u32 i = 0; i < count; ++i) {
        NetworkEndpoint src{};
        NetworkEndpoint dst{};
        src.ip_address = packet_read_u32(packet);
        src.port       = static_cast<u16>(packet_read_u32(packet));
        dst.ip_address = packet_read_u32(packet);
        dst.port       = static_cast<u16>(packet_read_u32(packet));
        NetworkRPC rpc{};
        rpc.rpc_type                  = packet_read_u32(packet);
        rpc.target_entity_network_id  = packet_read_u32(packet);
        rpc.payload_pos.x             = packet_read_f32(packet);
        rpc.payload_pos.y             = packet_read_f32(packet);
        rpc.payload_pos.z             = packet_read_f32(packet);
        enqueue_network_rpc(world, rpc, src, dst);
    }
}

} // namespace engine
