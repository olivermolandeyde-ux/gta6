#include "multiplayer/RaceMode.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"
#include "multiplayer/GameModeCore.h"
#include "network/NetworkCore.h"

namespace engine {

namespace {

[[nodiscard]] const RaceRuleComponent* rules_for(World& world, u32 session_id) {
    for (Entity e : world.query<RaceRuleComponent>()) {
        const RaceRuleComponent* r = world.get<RaceRuleComponent>(e);
        if (r && r->session_id == session_id) {
            return r;
        }
    }
    return nullptr;
}

void bump_cp(World& world, bool valid) {
    for (Entity e : world.query<MultiplayerTelemetryComponent>()) {
        MultiplayerTelemetryComponent* t = world.get<MultiplayerTelemetryComponent>(e);
        if (!t) {
            continue;
        }
        if (valid) {
            t->checkpoints_validated += 1;
        } else {
            t->cheats_rejected += 1;
        }
        break;
    }
}

void replicate_cp(World& world, u32 player_id, u32 index, float3 pos) {
    NetworkEndpoint server{};
    bool have = false;
    for (Entity e : world.query<NetworkPeerComponent>()) {
        const NetworkPeerComponent* p = world.get<NetworkPeerComponent>(e);
        if (p && p->is_authority) {
            server = p->endpoint;
            have   = true;
            break;
        }
    }
    if (!have) {
        return;
    }
    NetworkRPC rpc{};
    rpc.target_entity_network_id = player_id;
    rpc.rpc_type                 = kRpcRaceCheckpoint;
    rpc.payload_pos              = pos;
    (void)index;
    for (Entity e : world.query<NetworkPeerComponent>()) {
        const NetworkPeerComponent* p = world.get<NetworkPeerComponent>(e);
        if (!p || p->is_authority) {
            continue;
        }
        enqueue_network_rpc(world, rpc, server, p->endpoint);
        for (Entity t : world.query<MultiplayerTelemetryComponent>()) {
            MultiplayerTelemetryComponent* tel = world.get<MultiplayerTelemetryComponent>(t);
            if (tel) {
                tel->packets_replicated += 1;
            }
            break;
        }
    }
}

} // namespace

Entity instantiate_race_rules(World& world, const InstantiationRequest& request,
                              const RaceRuleComponent& rules) {
    validate_instantiation(request);
    return world.instantiate(request, rules);
}

void ProcessRaceEventsSystem(World& world, const CheckpointTriggerEvent* events, u32 event_count,
                             CommandBuffer& cmd) {
    (void)cmd;
    if (events == nullptr || event_count == 0) {
        return;
    }
    for (u32 i = 0; i < event_count; ++i) {
        const CheckpointTriggerEvent& ev = events[i];
        PlayerRaceStateComponent* rs = nullptr;
        PlayerSessionBindComponent* bind = nullptr;
        Entity player_e = kNullEntity;
        for (Entity e : world.query<PlayerRaceStateComponent, PlayerSessionBindComponent>()) {
            PlayerRaceStateComponent* cand = world.get<PlayerRaceStateComponent>(e);
            if (cand && cand->player_entity_id == ev.player_entity_id) {
                rs   = cand;
                bind = world.get<PlayerSessionBindComponent>(e);
                player_e = e;
                break;
            }
        }
        if (!rs || !bind) {
            bump_cp(world, false);
            continue;
        }
        const RaceRuleComponent* rules = rules_for(world, bind->session_id);
        if (!rules || rules->checkpoint_count == 0) {
            bump_cp(world, false);
            continue;
        }
        if (ev.checkpoint_index != rs->next_checkpoint_index
            || ev.checkpoint_index >= rules->checkpoint_count) {
            bump_cp(world, false);
            continue;
        }
        const float3 expected = rules->checkpoint_positions[rs->next_checkpoint_index];
        const float dist = float3_length(float3_sub(ev.trigger_position, expected));
        if (dist >= rules->checkpoint_radius_m) {
            // Teleport cheat: reject and snap back.
            bump_cp(world, false);
            for (Entity p : world.query<PlayerStateComponent>()) {
                if (p.index() != ev.player_entity_id) {
                    continue;
                }
                PlayerStateComponent* ps = world.get<PlayerStateComponent>(p);
                if (ps) {
                    ps->camera_position = rs->last_valid_position;
                }
            }
            continue;
        }
        rs->last_valid_position    = ev.trigger_position;
        rs->next_checkpoint_index += 1;
        if (rs->next_checkpoint_index >= rules->checkpoint_count) {
            rs->next_checkpoint_index = 0;
            rs->current_lap += 1;
            if (rs->best_lap_time_s <= 0.f || rs->lap_time_s < rs->best_lap_time_s) {
                rs->best_lap_time_s = rs->lap_time_s;
            }
            rs->lap_time_s = 0.f;
        }
        bump_cp(world, true);
        replicate_cp(world, ev.player_entity_id, rs->next_checkpoint_index, ev.trigger_position);
        (void)player_e;
    }
}

} // namespace engine
