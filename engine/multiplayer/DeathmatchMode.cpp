#include "multiplayer/DeathmatchMode.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"
#include "multiplayer/GameModeCore.h"
#include "network/NetworkCore.h"

namespace engine {

namespace {

[[nodiscard]] Entity by_index_player(World& world, u32 index) {
    for (Entity e : world.query<PlayerStateComponent>()) {
        if (e.index() == index) {
            return e;
        }
    }
    return kNullEntity;
}

[[nodiscard]] Entity stats_for(World& world, u32 player_index) {
    for (Entity e : world.query<PlayerSessionStatsComponent>()) {
        const PlayerSessionStatsComponent* s = world.get<PlayerSessionStatsComponent>(e);
        if (s && s->player_entity_id == player_index) {
            return e;
        }
    }
    return kNullEntity;
}

[[nodiscard]] bool session_active(World& world, u32 session_id) {
    for (Entity e : world.query<GameSessionComponent>()) {
        const GameSessionComponent* s = world.get<GameSessionComponent>(e);
        if (s && s->session_id == session_id) {
            return s->session_state == kSessionActive;
        }
    }
    return false;
}

void bump_kills(World& world, bool valid) {
    for (Entity e : world.query<MultiplayerTelemetryComponent>()) {
        MultiplayerTelemetryComponent* t = world.get<MultiplayerTelemetryComponent>(e);
        if (!t) {
            continue;
        }
        if (valid) {
            t->kills_validated += 1;
        } else {
            t->cheats_rejected += 1;
        }
        break;
    }
}

void replicate_kill(World& world, const KillEvent& ev) {
    NetworkEndpoint server{};
    bool have_server = false;
    for (Entity e : world.query<NetworkPeerComponent>()) {
        const NetworkPeerComponent* p = world.get<NetworkPeerComponent>(e);
        if (p && p->is_authority) {
            server = p->endpoint;
            have_server = true;
            break;
        }
    }
    if (!have_server) {
        return;
    }
    NetworkRPC rpc{};
    rpc.target_entity_network_id = ev.victim_entity_id;
    rpc.rpc_type                 = kRpcKillFeed;
    rpc.payload_pos              = ev.kill_location;
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

Entity instantiate_deathmatch_rules(World& world, const InstantiationRequest& request,
                                    const DeathmatchRuleComponent& rules) {
    validate_instantiation(request);
    return world.instantiate(request, rules);
}

void ProcessDeathmatchEventsSystem(World& world, const KillEvent* events, u32 event_count,
                                   CommandBuffer& cmd) {
    (void)cmd;
    if (events == nullptr || event_count == 0) {
        return;
    }
    constexpr float kMaxKillRangeM = 80.0f;

    for (u32 i = 0; i < event_count; ++i) {
        const KillEvent& ev = events[i];
        if (ev.killer_entity_id == ev.victim_entity_id) {
            bump_kills(world, false);
            continue;
        }
        Entity killer = by_index_player(world, ev.killer_entity_id);
        Entity victim = by_index_player(world, ev.victim_entity_id);
        if (!world.is_alive(killer) || !world.is_alive(victim)) {
            bump_kills(world, false);
            continue;
        }
        const PlayerStateComponent* kp = world.get<PlayerStateComponent>(killer);
        PlayerStateComponent* vp       = world.get<PlayerStateComponent>(victim);
        if (!kp || !vp || vp->health <= 0.f) {
            bump_kills(world, false);
            continue;
        }

        Entity ks = stats_for(world, ev.killer_entity_id);
        Entity vs = stats_for(world, ev.victim_entity_id);
        const PlayerSessionBindComponent* kb =
            world.is_alive(ks) ? world.get<PlayerSessionBindComponent>(ks) : nullptr;
        const PlayerSessionBindComponent* vb =
            world.is_alive(vs) ? world.get<PlayerSessionBindComponent>(vs) : nullptr;
        if (!kb || !vb || kb->session_id != vb->session_id || !session_active(world, kb->session_id)) {
            bump_kills(world, false);
            continue;
        }

        // Server validation: range + line-of-sight (open-world: no occluder => LOS).
        const float dist = float3_length(float3_sub(kp->camera_position, vp->camera_position));
        const float shot = float3_length(float3_sub(ev.kill_location, vp->camera_position));
        if (dist > kMaxKillRangeM || shot > kMaxKillRangeM) {
            bump_kills(world, false);
            continue;
        }
        const float3 to_v = float3_sub(vp->camera_position, kp->camera_position);
        const float3 aim  = float3_normalize_or(kp->camera_forward, float3{0.f, 0.f, 1.f});
        const float3 dir  = float3_normalize_or(to_v, aim);
        if (float3_dot(aim, dir) < 0.25f) {
            bump_kills(world, false);
            continue;
        }

        PlayerSessionStatsComponent* kst = world.get<PlayerSessionStatsComponent>(ks);
        PlayerSessionStatsComponent* vst = world.get<PlayerSessionStatsComponent>(vs);
        ENGINE_ASSERT(kst && vst, "stats");
        kst->kills += 1;
        kst->score += 1;
        vst->deaths += 1;
        vp->health = 0.f;
        u32 delay = 3;
        for (Entity r : world.query<DeathmatchRuleComponent>()) {
            const DeathmatchRuleComponent* rules = world.get<DeathmatchRuleComponent>(r);
            if (rules && rules->session_id == kb->session_id) {
                delay = rules->respawn_delay_s;
            }
        }
        vst->respawn_timer_s = static_cast<float>(delay);
        bump_kills(world, true);
        replicate_kill(world, ev);
        (void)ev.weapon_used_id;
    }
}

} // namespace engine
