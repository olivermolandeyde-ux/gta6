#include "network/StateReplication.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "objects/BreakableWindow.h"

namespace engine {

namespace {

inline constexpr u32 kMaxPendingInputs = 32;

struct PendingInput {
    NetworkEndpoint     src;
    ClientInputCommand  cmd;
    u32                 occupied;
};

PendingInput g_inputs[8][kMaxPendingInputs]{};

[[nodiscard]] Entity find_peer(World& world, NetworkEndpoint ep) {
    for (Entity e : world.query<NetworkPeerComponent>()) {
        const NetworkPeerComponent* p = world.get<NetworkPeerComponent>(e);
        if (p && endpoint_equal(p->endpoint, ep)) {
            return e;
        }
    }
    return kNullEntity;
}

[[nodiscard]] Entity find_by_network_id(World& world, u32 net_id) {
    for (Entity e : world.query<NetworkIdentityComponent>()) {
        const NetworkIdentityComponent* id = world.get<NetworkIdentityComponent>(e);
        if (id && id->network_id == net_id) {
            return e;
        }
    }
    return kNullEntity;
}

void inbox_push(ClientNetInboxComponent* inbox, u32 rpc_type, u32 net_id) {
    if (inbox->received_count < 8) {
        inbox->received_rpc_type[inbox->received_count++] = rpc_type;
    }
    if (rpc_type == kRpcWindowShattered) {
        inbox->last_shattered_network_id = net_id;
    }
}

void deliver_rpc_to_aoi(World& world, const NetworkRPC& rpc, NetworkEndpoint src,
                        FrameAllocator& frame_alloc) {
    Entity target = find_by_network_id(world, rpc.target_entity_network_id);
    StreamingCellId cell{};
    if (world.is_alive(target)) {
        if (const NetworkIdentityComponent* id = world.get<NetworkIdentityComponent>(target)) {
            cell = id->cell_id;
        } else if (const StreamingCellId* cid = world.get<StreamingCellId>(target)) {
            cell = *cid;
        }
    }

    for (Entity e : world.query<NetworkPeerComponent, ClientNetInboxComponent>()) {
        NetworkPeerComponent* peer = world.get<NetworkPeerComponent>(e);
        ClientNetInboxComponent* inbox = world.get<ClientNetInboxComponent>(e);
        if (!peer || !inbox || peer->is_authority) {
            continue;
        }
        const i32 dx = static_cast<i32>(peer->subscribe_cell_x) - static_cast<i32>(cell.cell_x);
        const i32 dy = static_cast<i32>(peer->subscribe_cell_y) - static_cast<i32>(cell.cell_y);
        const i32 adx = dx < 0 ? -dx : dx;
        const i32 ady = dy < 0 ? -dy : dy;
        const i32 cheb = adx > ady ? adx : ady;
        if (cheb <= 1) {
            inbox_push(inbox, rpc.rpc_type, rpc.target_entity_network_id);
        }
        (void)src;
        (void)frame_alloc;
    }
}

} // namespace

void submit_client_input(World& world, NetworkEndpoint client, const ClientInputCommand& input) {
    const u16 wid = world.world_id();
    ENGINE_ASSERT(wid < 8, "world_id");
    for (u32 i = 0; i < kMaxPendingInputs; ++i) {
        if (g_inputs[wid][i].occupied == 0) {
            g_inputs[wid][i].src      = client;
            g_inputs[wid][i].cmd      = input;
            g_inputs[wid][i].occupied = 1;
            return;
        }
    }
    ENGINE_PANIC("input queue full");
}

void UpdateStateReplicationSystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                                  CommandBuffer& cmd) {
    ENGINE_ASSERT(delta_time >= 0.f, "net dt");
    const u16 wid = world.world_id();

    NetworkRPC rpcs[64];
    NetworkEndpoint srcs[64];
    NetworkEndpoint dsts[64];
    const u32 incoming = drain_network_rpc(world, rpcs, srcs, dsts, 64);

    for (u32 i = 0; i < incoming; ++i) {
        if (rpcs[i].rpc_type == kRpcShoot) {
            Entity target = find_by_network_id(world, rpcs[i].target_entity_network_id);
            if (!world.is_alive(target) || !world.has<BreakableWindowComponent>(target)) {
                continue; // anti-cheat: unknown or non-window target
            }
            const BreakableWindowPose* pose = world.get<BreakableWindowPose>(target);
            ImpactEvent hit{};
            hit.target_entity   = target;
            hit.impact_point    = pose ? pose->center_ws : rpcs[i].payload_pos;
            hit.impact_velocity = float3{0.f, 0.f, -32.f};
            hit.mass            = 8.0f;
            ProcessWindowImpactsSystem(world, &hit, 1, cmd);

            NetworkRPC shattered{};
            shattered.target_entity_network_id = rpcs[i].target_entity_network_id;
            shattered.rpc_type                 = kRpcWindowShattered;
            shattered.payload_pos              = hit.impact_point;
            deliver_rpc_to_aoi(world, shattered, srcs[i], frame_alloc);
        } else if (rpcs[i].rpc_type == kRpcWindowShattered) {
            deliver_rpc_to_aoi(world, rpcs[i], srcs[i], frame_alloc);
        }
    }

    // Server authority: consume client inputs, predict locally, reconcile.
    for (u32 i = 0; i < kMaxPendingInputs; ++i) {
        if (!g_inputs[wid][i].occupied) {
            continue;
        }
        PendingInput inp = g_inputs[wid][i];
        g_inputs[wid][i].occupied = 0;

        Entity peer = find_peer(world, inp.src);
        if (!world.is_alive(peer)) {
            continue;
        }
        ServerStateComponent*     srv  = world.get<ServerStateComponent>(peer);
        ClientPredictionComponent* pred = world.get<ClientPredictionComponent>(peer);
        ENGINE_ASSERT(srv && pred, "peer missing replication columns");

        // predicted_position = server_position + (client_input_velocity * delta_time)
        pred->predicted_velocity = inp.cmd.velocity;
        pred->predicted_position =
            float3_add(srv->server_position, float3_scale(inp.cmd.velocity, delta_time));

        const float max_speed = 12.0f;
        const float speed     = float3_length(inp.cmd.velocity);
        if (speed > max_speed + 0.5f) {
            // Anti-cheat: clamp and snap the client back to the last server pose.
            pred->predicted_position = srv->server_position;
            pred->predicted_velocity = float3{0.f, 0.f, 0.f};
        } else {
            srv->server_position = pred->predicted_position;
            srv->server_velocity = pred->predicted_velocity;
        }
        srv->server_sequence += 1;
        pred->last_acknowledged_input_sequence = inp.cmd.sequence;

        const float err = float3_length(float3_sub(pred->predicted_position, srv->server_position));
        if (err > 0.35f) {
            // Server reconciliation: snap client prediction to authority.
            pred->predicted_position = srv->server_position;
            pred->predicted_velocity = srv->server_velocity;
        }

        srv->interpolation_alpha = clampf(delta_time * 12.0f, 0.f, 1.f);

        if (inp.cmd.shoot_network_id != 0) {
            NetworkRPC shoot{};
            shoot.target_entity_network_id = inp.cmd.shoot_network_id;
            shoot.rpc_type                 = kRpcShoot;
            shoot.payload_pos              = inp.cmd.shoot_point;
            NetworkPeerComponent* p = world.get<NetworkPeerComponent>(peer);
            NetworkEndpoint dst{};
            for (Entity s : world.query<NetworkPeerComponent>()) {
                const NetworkPeerComponent* sp = world.get<NetworkPeerComponent>(s);
                if (sp && sp->is_authority) {
                    dst = sp->endpoint;
                    break;
                }
            }
            enqueue_network_rpc(world, shoot, p ? p->endpoint : inp.src, dst);
        }
    }

    (void)frame_alloc;
    (void)cmd;
}

} // namespace engine
