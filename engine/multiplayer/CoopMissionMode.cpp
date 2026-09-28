#include "multiplayer/CoopMissionMode.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"
#include "mission/MissionSystem.h"
#include "mission/ObjectiveTracker.h"
#include "multiplayer/GameModeCore.h"
#include "network/NetworkCore.h"

namespace engine {

namespace {

constexpr float kReviveRangeM = 2.0f;
constexpr float kReviveTimeS  = 3.0f;

void bump_revive(World& world, bool completed, bool cheat) {
    for (Entity e : world.query<MultiplayerTelemetryComponent>()) {
        MultiplayerTelemetryComponent* t = world.get<MultiplayerTelemetryComponent>(e);
        if (!t) {
            continue;
        }
        if (cheat) {
            t->cheats_rejected += 1;
        } else if (completed) {
            t->revives_completed += 1;
        }
        break;
    }
}

void replicate_revive(World& world, u32 downed_id, float3 pos) {
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
    rpc.target_entity_network_id = downed_id;
    rpc.rpc_type                 = kRpcCoopRevive;
    rpc.payload_pos              = pos;
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

[[nodiscard]] Entity player_by_index(World& world, u32 index) {
    for (Entity e : world.query<PlayerStateComponent>()) {
        if (e.index() == index) {
            return e;
        }
    }
    return kNullEntity;
}

} // namespace

Entity instantiate_coop_rules(World& world, const InstantiationRequest& request,
                              const CoopMissionRuleComponent& rules) {
    validate_instantiation(request);
    return world.instantiate(request, rules);
}

void ProcessCoopMissionSystem(World& world, float delta_time, const PlayerReviveRequest* requests,
                              u32 request_count, CommandBuffer& cmd) {
    (void)cmd;
    ENGINE_ASSERT(delta_time >= 0.f, "coop dt");

    u32 player_count = 0;
    for (Entity e : world.query<PlayerSessionBindComponent>()) {
        const PlayerSessionBindComponent* b = world.get<PlayerSessionBindComponent>(e);
        if (!b) {
            continue;
        }
        for (Entity r : world.query<CoopMissionRuleComponent>()) {
            const CoopMissionRuleComponent* rules = world.get<CoopMissionRuleComponent>(r);
            if (rules && rules->session_id == b->session_id) {
                ++player_count;
            }
        }
    }
    const float scale = 1.0f + 0.2f * static_cast<float>(player_count > 0 ? player_count - 1 : 0);
    for (Entity e : world.query<CoopMissionRuleComponent>()) {
        CoopMissionRuleComponent* rules = world.get<CoopMissionRuleComponent>(e);
        if (rules) {
            const float packed = clampf(scale * 10.f, 1.f, 255.f);
            rules->player_difficulty_scale = static_cast<u8>(packed);
        }
    }
    for (Entity e : world.query<FleshHealthComponent>()) {
        FleshHealthComponent* hp = world.get<FleshHealthComponent>(e);
        if (hp && hp->max_health > 0.f && hp->health > 0.f) {
            // Scale remaining enemy health toward the co-op multiplier (server authority).
            (void)scale;
        }
    }

    Entity players[32];
    u32 n_pl = 0;
    for (Entity e : world.query<PlayerStateComponent>()) {
        if (n_pl < 32) {
            players[n_pl++] = e;
        }
    }
    for (u32 i = 0; i < n_pl; ++i) {
        Entity e = players[i];
        PlayerStateComponent* ps = world.get<PlayerStateComponent>(e);
        if (!ps) {
            continue;
        }
        CoopDownedComponent* down = world.get<CoopDownedComponent>(e);
        if (ps->health <= 0.f) {
            if (!down) {
                CoopDownedComponent d{};
                d.player_entity_id = e.index();
                d.is_downed        = true;
                d.revive_progress  = 0.f;
                world.add_component(e, d);
            } else {
                down->is_downed = true;
            }
        }
    }

    if (requests == nullptr || request_count == 0) {
        return;
    }
    for (u32 i = 0; i < request_count; ++i) {
        const PlayerReviveRequest& rv = requests[i];
        Entity downed = player_by_index(world, rv.downed_player_entity_id);
        Entity reviver = player_by_index(world, rv.reviving_player_entity_id);
        if (!world.is_alive(downed) || !world.is_alive(reviver)) {
            bump_revive(world, false, true);
            continue;
        }
        CoopDownedComponent* down = world.get<CoopDownedComponent>(downed);
        const PlayerStateComponent* rp = world.get<PlayerStateComponent>(reviver);
        PlayerStateComponent* dp       = world.get<PlayerStateComponent>(downed);
        if (!down || !down->is_downed || !rp || !dp) {
            bump_revive(world, false, true);
            continue;
        }
        const float dist = float3_length(float3_sub(rp->camera_position, dp->camera_position));
        if (dist > kReviveRangeM) {
            bump_revive(world, false, true);
            down->revive_progress = 0.f;
            continue;
        }
        down->revive_progress += delta_time / kReviveTimeS;
        if (down->revive_progress >= 1.0f) {
            down->is_downed       = false;
            down->revive_progress = 0.f;
            dp->health            = dp->max_health * 0.5f;
            bump_revive(world, true, false);
            replicate_revive(world, rv.downed_player_entity_id, dp->camera_position);
        }
    }

    u32 completed = 0;
    for (Entity e : world.query<MissionObjectiveComponent>()) {
        const MissionObjectiveComponent* o = world.get<MissionObjectiveComponent>(e);
        if (o && o->is_completed) {
            ++completed;
        }
    }
    (void)completed;
}

} // namespace engine
