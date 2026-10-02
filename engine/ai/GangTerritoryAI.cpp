#include "ai/GangTerritoryAI.h"

#include "ai/AIDataLoader.h"
#include "ai/AdvancedNavigation.h"
#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"
#include "mission/ObjectiveTracker.h"

namespace engine {

namespace {

constexpr u32 kSnapMax = 256;

[[nodiscard]] bool player_in_radius(World& world, float3 center, float radius, float* out_rep) {
    for (Entity e : world.query<PlayerStateComponent>()) {
        const PlayerStateComponent* p = world.get<PlayerStateComponent>(e);
        if (!p) {
            continue;
        }
        if (out_rep) {
            *out_rep = p->wanted_level;
        }
        return float3_length(float3_sub(p->camera_position, center)) <= radius;
    }
    return false;
}

} // namespace

Entity instantiate_gang_territory(World& world, const InstantiationRequest& request,
                                  const GangTerritoryComponent& territory) {
    validate_instantiation(request);
    GangTreasuryComponent treasury{};
    treasury.gang_id  = territory.controlling_gang_id;
    treasury.cash_usd = 0;
    return world.instantiate(request, territory, treasury);
}

Entity instantiate_gang_member(World& world, const InstantiationRequest& request,
                               const GangMemberComponent& member, float3 position_ws) {
    validate_instantiation(request);
    GangMemberPoseComponent pose{};
    pose.position_ws  = position_ws;
    pose.patrol_index = 0;
    FleshHealthComponent hp{};
    hp.health     = 100.f;
    hp.max_health = 100.f;
    NavigationAgentComponent nav{};
    nav.agent_entity_id  = member.member_entity_id;
    nav.current_position = position_ws;
    nav.target_position  = member.patrol_route_count > 0 ? member.patrol_route[0] : position_ws;
    nav.max_speed_ms     = 1.6f;
    nav.navigation_mode  = 3;
    return world.instantiate(request, member, pose, hp, nav);
}

void UpdateGangTerritoryAISystem(World& world, float delta_time, const TurfWarEvent* events,
                                 u32 event_count, CommandBuffer& cmd) {
    ENGINE_ASSERT(delta_time >= 0.f, "gang dt");

    Entity members[kSnapMax];
    u32 n_mem = 0;
    for (Entity e : world.query<GangMemberComponent, GangMemberPoseComponent>()) {
        if (n_mem < kSnapMax) {
            members[n_mem++] = e;
        }
    }

    for (u32 i = 0; i < n_mem; ++i) {
        GangMemberComponent* m = world.get<GangMemberComponent>(members[i]);
        GangMemberPoseComponent* pose = world.get<GangMemberPoseComponent>(members[i]);
        ENGINE_ASSERT(m && pose, "member stale");
        const FleshHealthComponent* hp = world.get<FleshHealthComponent>(members[i]);
        if (hp && hp->health <= 0.f) {
            m->is_patrolling = false;
            continue;
        }
        if (!m->is_patrolling || m->patrol_route_count == 0) {
            continue;
        }
        const float3 wp = m->patrol_route[pose->patrol_index % m->patrol_route_count];
        const float3 delta = float3_sub(wp, pose->position_ws);
        const float dist = float3_length(delta);
        if (dist < 1.0f) {
            pose->patrol_index = (pose->patrol_index + 1) % m->patrol_route_count;
        } else {
            const float3 dir = float3_scale(delta, 1.0f / dist);
            const float step = min_of(1.6f * delta_time, dist);
            pose->position_ws = float3_add(pose->position_ws, float3_scale(dir, step));
        }
        NavigationAgentComponent* nav = world.get<NavigationAgentComponent>(members[i]);
        if (nav) {
            nav->current_position = pose->position_ws;
            nav->target_position  = wp;
            nav->navigation_mode  = 3;
        }
    }

    for (Entity e : world.query<GangTerritoryComponent>()) {
        GangTerritoryComponent* t = world.get<GangTerritoryComponent>(e);
        ENGINE_ASSERT(t != nullptr, "territory stale");
        u32 dead = 0;
        u32 live = 0;
        for (u32 i = 0; i < n_mem; ++i) {
            const GangMemberComponent* m = world.get<GangMemberComponent>(members[i]);
            const FleshHealthComponent* hp = world.get<FleshHealthComponent>(members[i]);
            if (!m || m->home_territory_id != t->territory_id || m->gang_id != t->controlling_gang_id) {
                continue;
            }
            if (hp && hp->health <= 0.f) {
                ++dead;
            } else {
                ++live;
            }
        }
        if (dead > 0) {
            t->influence_level = max_of(0.f, t->influence_level - 0.15f * static_cast<float>(dead));
        }
        float dummy = 0.f;
        if (player_in_radius(world, t->center_position, t->radius_m, &dummy)
            && t->controlling_gang_id != kGangNeutral) {
            for (u32 i = 0; i < n_mem; ++i) {
                GangMemberComponent* m = world.get<GangMemberComponent>(members[i]);
                if (m && m->home_territory_id == t->territory_id) {
                    m->aggression_toward_player =
                        min_of(1.f, m->aggression_toward_player + 0.25f * delta_time);
                }
            }
        }
        GangTreasuryComponent* tre = world.get<GangTreasuryComponent>(e);
        if (tre && t->influence_level > 0.5f && t->controlling_gang_id != kGangNeutral) {
            tre->gang_id = t->controlling_gang_id;
            tre->cash_usd += static_cast<u32>(t->influence_level * 10.0f * delta_time * 60.0f);
        }
        (void)live;
    }

    if (events && event_count > 0) {
        for (u32 i = 0; i < event_count; ++i) {
            const TurfWarEvent& ev = events[i];
            bool exists = false;
            for (Entity e : world.query<ActiveTurfWarComponent>()) {
                ActiveTurfWarComponent* w = world.get<ActiveTurfWarComponent>(e);
                if (w && w->territory_id == ev.territory_id && !w->resolved) {
                    exists = true;
                    break;
                }
            }
            if (exists) {
                continue;
            }
            ActiveTurfWarComponent war{};
            war.territory_id      = ev.territory_id;
            war.attacker_gang_id  = ev.attacker_gang_id;
            war.defender_gang_id  = ev.defender_gang_id;
            war.battle_center     = ev.battle_center;
            war.battle_radius_m   = ev.battle_radius_m;
            war.resolved          = 0;
            war.winner_gang_id    = 0;
            InstantiationRequest req{};
            req.domain      = InstantiationDomain::PersistentWorld;
            req.debug_label = "turf_war";
            (void)world.instantiate(req, war);
        }
    }

    Entity wars[kSnapMax];
    u32 n_war = 0;
    for (Entity e : world.query<ActiveTurfWarComponent>()) {
        if (n_war < kSnapMax) {
            wars[n_war++] = e;
        }
    }
    for (u32 w = 0; w < n_war; ++w) {
        ActiveTurfWarComponent* war = world.get<ActiveTurfWarComponent>(wars[w]);
        ENGINE_ASSERT(war != nullptr, "war stale");
        if (war->resolved) {
            continue;
        }
        u32 atk = 0;
        u32 def = 0;
        for (u32 i = 0; i < n_mem; ++i) {
            const GangMemberComponent* m = world.get<GangMemberComponent>(members[i]);
            const GangMemberPoseComponent* pose = world.get<GangMemberPoseComponent>(members[i]);
            const FleshHealthComponent* hp = world.get<FleshHealthComponent>(members[i]);
            if (!m || !pose) {
                continue;
            }
            if (hp && hp->health <= 0.f) {
                continue;
            }
            if (float3_length(float3_sub(pose->position_ws, war->battle_center)) > war->battle_radius_m) {
                continue;
            }
            if (m->gang_id == war->attacker_gang_id) {
                ++atk;
            } else if (m->gang_id == war->defender_gang_id) {
                ++def;
            }
        }
        if (atk + def == 0) {
            continue;
        }
        war->resolved = 1;
        war->winner_gang_id = atk >= def ? war->attacker_gang_id : war->defender_gang_id;
        for (Entity e : world.query<GangTerritoryComponent>()) {
            GangTerritoryComponent* t = world.get<GangTerritoryComponent>(e);
            if (!t || t->territory_id != war->territory_id) {
                continue;
            }
            t->controlling_gang_id = war->winner_gang_id;
            t->influence_level     = 0.85f;
            t->last_contested_time += 1;
        }
    }

    u32 controlled = 0;
    for (Entity e : world.query<GangTerritoryComponent>()) {
        const GangTerritoryComponent* t = world.get<GangTerritoryComponent>(e);
        if (t && t->controlling_gang_id != kGangNeutral && t->influence_level > 0.1f) {
            ++controlled;
        }
    }
    u32 wars_active = 0;
    for (Entity e : world.query<ActiveTurfWarComponent>()) {
        const ActiveTurfWarComponent* w = world.get<ActiveTurfWarComponent>(e);
        if (w && !w->resolved) {
            ++wars_active;
        }
        if (w && w->resolved) {
            ++wars_active; // count the battle that occurred
        }
    }
    for (Entity e : world.query<AdvancedAITelemetryComponent>()) {
        AdvancedAITelemetryComponent* tel = world.get<AdvancedAITelemetryComponent>(e);
        if (tel) {
            tel->territories_controlled = controlled;
            tel->turf_wars_active       = wars_active;
        }
        break;
    }
    (void)cmd;
}

} // namespace engine
