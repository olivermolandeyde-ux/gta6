#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

struct NetworkEndpoint {
    u32 ip_address;
    u16 port;
};

struct PacketBuffer {
    u8* data; // Points into FrameAllocator
    u32 capacity;
    u32 write_offset;
    u32 read_offset;
};

inline constexpr u32 kRpcShoot           = 0;
inline constexpr u32 kRpcWindowShattered = 1;
inline constexpr u32 kRpcKillFeed        = 2;
inline constexpr u32 kRpcRaceCheckpoint  = 3;
inline constexpr u32 kRpcCoopRevive      = 4;

struct NetworkRPC {
    u32    target_entity_network_id;
    u32    rpc_type; // e.g., 0 = Shoot, 1 = WindowShattered
    float3 payload_pos;
};

struct NetworkPeerComponent {
    NetworkEndpoint endpoint;
    u8              is_authority; // 1 = server
    u8              is_client;
    u16             _pad;
    u32             subscribe_cell_x;
    u32             subscribe_cell_y;
};

struct ClientNetInboxComponent {
    u32 received_rpc_type[8];
    u32 received_count;
    u32 last_shattered_network_id;
};

[[nodiscard]] inline bool endpoint_equal(NetworkEndpoint a, NetworkEndpoint b) noexcept {
    return a.ip_address == b.ip_address && a.port == b.port;
}

[[nodiscard]] PacketBuffer make_packet_buffer(FrameAllocator& frame_alloc, u32 bytes);

void packet_write_u32(PacketBuffer& packet, u32 value);
void packet_write_f32(PacketBuffer& packet, float value);
[[nodiscard]] u32   packet_read_u32(PacketBuffer& packet);
[[nodiscard]] float packet_read_f32(PacketBuffer& packet);

void enqueue_network_rpc(World& world, const NetworkRPC& rpc, NetworkEndpoint src,
                         NetworkEndpoint dst);
u32  drain_network_rpc(World& world, NetworkRPC* out, NetworkEndpoint* src, NetworkEndpoint* dst,
                       u32 max_out);

[[nodiscard]] Entity instantiate_network_peer(World& world, const InstantiationRequest& request,
                                              NetworkEndpoint endpoint, bool authority,
                                              u32 cell_x, u32 cell_y);

void ProcessNetworkPackets(World& world, float delta_time, FrameAllocator& frame_alloc);

} // namespace engine
