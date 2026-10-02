#include "ai/PoliceTacticalAI.h"

#include "ai/AIDataLoader.h"
#include "ai/AdvancedNavigation.h"
#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"

#include <cmath>

namespace engine {

namespace {

constexpr u32 kSnapMax = 256;

[[nodiscard]] bool ray_aabb(float3 o, float3 d, float3 mn, float3 mx, float max_t) {
    float tmin = 0.f;
    float tmax = max_t;
    const float orig[3] = {o.x, o.y, o.z};
    const float dir[3]  = {d.x, d.y, d.z};
    const float b0[3]   = {mn.x, mn.y, mn.z};
    const float b1[3]   = {mx.x, mx.y, mx.z};
    for (u32 a = 0; a < 3; ++a) {
        if (std::fabs(dir[a]) < 1e-8f) {
            if (orig[a] < b0[a] || orig[a] > b1[a]) {
                return false;
            }
            continue;
        }
        const float inv = 1.0f / dir[a];
        float t0 = (b0[a] - orig[a]) * inv;
        float t1 = (b1[a] - orig[a]) * inv;
        if (t0 > t1) {
            const float tmp = t0;
            t0 = t1;
            t1 = tmp;
        }
        tmin = t0 > tmin ? t0 : tmin;
        tmax = t1 < tmax ? t1 : tmax;
        if (tmax < tmin) {
            return false;
        }
    }
    return tmax >= 0.f && tmin <= max_t;
}

[[nodiscard]] bool player_pose(World& world, float3* pos, u32* index) {
    for (Entity e : world.query<PlayerStateComponent>()) {
        const PlayerStateComponent* p = world.get<PlayerStateComponent>(e);
        if (!p) {
            continue;
        }
        if (pos) {
            *pos = p->camera_position;
        }
        if (index) {
            *index = e.index();
        }
        return true;
    }
    return false;
}

void integrate_pose(float3* pos, float3* vel, float3 target, float speed, float dt) {
    const float3 delta = float3_sub(target, *pos);
    const float dist = float3_length(delta);
    if (dist < 0.15f) {
        *vel = float3{0.f, 0.f, 0.f};
        return;
    }
    const float3 dir = float3_scale(delta, 1.0f / dist);
    const float step = min_of(speed * dt, dist);
    *pos = float3_add(*pos, float3_scale(dir, step));
    *vel = float3_scale(dir, speed);
}

} // namespace

Entity instantiate_police_officer(World& world, const InstantiationRequest& request,
                                  const PoliceOfficerComponent& officer, float3 position_ws) {
    validate_instantiation(request);
    PoliceOfficerPoseComponent pose{};
    pose.position_ws = position_ws;
    pose.velocity_ws = float3{0.f, 0.f, 0.f};
    NavigationAgentComponent nav{};
    nav.agent_entity_id   = officer.officer_entity_id;
    nav.current_position  = position_ws;
    nav.target_position   = position_ws;
    nav.max_speed_ms      = officer.role == 2 ? 5.2f : 4.0f;
    nav.navigation_mode   = 1;
    return world.instantiate(request, officer, pose, nav);
}

Entity instantiate_police_squad(World& world, const InstantiationRequest& request,
                                const PoliceSquadComponent& squad) {
    validate_instantiation(request);
    return world.instantiate(request, squad);
}

Entity instantiate_police_helicopter(World& world, const InstantiationRequest& request,
                                     const PoliceHelicopterComponent& heli, float3 position_ws) {
    validate_instantiation(request);
    PoliceHelicopterComponent h = heli;
    h.pursuit_position = float3{position_ws.x, h.altitude_m, position_ws.z};
    return world.instantiate(request, h);
}

Entity instantiate_k9_unit(World& world, const InstantiationRequest& request,
                           const K9UnitComponent& k9, float3 position_ws) {
    validate_instantiation(request);
    PoliceOfficerPoseComponent pose{};
    pose.position_ws = position_ws;
    NavigationAgentComponent nav{};
    nav.agent_entity_id  = k9.dog_entity_id;
    nav.current_position = position_ws;
    nav.target_position  = position_ws;
    nav.max_speed_ms     = 6.5f * max_of(k9.speed_multiplier, 0.5f);
    nav.navigation_mode  = 1;
    return world.instantiate(request, k9, pose, nav);
}

void UpdatePoliceTacticalAISystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                                  CommandBuffer& cmd) {
    (void)cmd;
    ENGINE_ASSERT(delta_time >= 0.f, "police dt");
    const u32 cap = max_of(1u, world.entity_count());

    float3 player_pos{};
    u32 player_id = 0;
    const bool have_player = player_pose(world, &player_pos, &player_id);

    PlayerScentTrailComponent* trail = nullptr;
    for (Entity e : world.query<PlayerScentTrailComponent>()) {
        trail = world.get<PlayerScentTrailComponent>(e);
        break;
    }
    if (trail && have_player) {
        for (u32 i = 0; i < 16; ++i) {
            trail->strength[i] = max_of(0.f, trail->strength[i] - 0.15f * delta_time);
        }
        trail->head = (trail->head + 1u) & 15u;
        trail->samples[trail->head] = player_pos;
        trail->strength[trail->head] = 1.0f;
        if (trail->count < 16) {
            trail->count += 1;
        }
    }

    Entity* covers = frame_alloc.allocate_array<Entity>(cap);
    u32 n_cover = 0;
    for (Entity e : world.query<TacticalCoverSlabComponent>()) {
        if (n_cover < cap) {
            covers[n_cover++] = e;
        }
    }

    Entity* officers = frame_alloc.allocate_array<Entity>(cap);
    u32 n_off = 0;
    for (Entity e : world.query<PoliceOfficerComponent, PoliceOfficerPoseComponent>()) {
        if (n_off < cap) {
            officers[n_off++] = e;
        }
    }

    Entity* squads = frame_alloc.allocate_array<Entity>(cap);
    u32 n_squad = 0;
    for (Entity e : world.query<PoliceSquadComponent>()) {
        if (n_squad < cap) {
            squads[n_squad++] = e;
        }
    }

    u32 flanking = 0;
    for (u32 s = 0; s < n_squad; ++s) {
        PoliceSquadComponent* squad = world.get<PoliceSquadComponent>(squads[s]);
        ENGINE_ASSERT(squad != nullptr, "squad stale");
        float3 centroid{};
        u32 live = 0;
        for (u32 i = 0; i < n_off; ++i) {
            PoliceOfficerComponent* off = world.get<PoliceOfficerComponent>(officers[i]);
            PoliceOfficerPoseComponent* pose = world.get<PoliceOfficerPoseComponent>(officers[i]);
            if (!off || !pose || off->squad_id != squad->squad_id) {
                continue;
            }
            centroid = float3_add(centroid, pose->position_ws);
            ++live;
        }
        if (live > 0) {
            centroid = float3_scale(centroid, 1.0f / static_cast<float>(live));
        }
        squad->formation_center = centroid;
        squad->officer_count    = live;
        squad->squad_cohesion   = clampf(static_cast<float>(live) / 8.0f, 0.f, 1.f);
        const float agg = clampf(0.25f + 0.12f * static_cast<float>(live), 0.f, 1.f);
        if (have_player && live >= 2) {
            squad->squad_state = 2; // flanking
            squad->target_player_entity_id = player_id;
        } else if (have_player) {
            squad->squad_state = 1;
        }

        const float3 to_player = have_player ? float3_sub(player_pos, centroid)
                                             : float3{0.f, 0.f, 1.f};
        const float3 fwd = float3_normalize_or(float3{to_player.x, 0.f, to_player.z},
                                               float3{0.f, 0.f, 1.f});
        const float3 right = float3_cross(float3{0.f, 1.f, 0.f}, fwd);

        u32 flank_slot = 0;
        for (u32 i = 0; i < n_off; ++i) {
            PoliceOfficerComponent* off = world.get<PoliceOfficerComponent>(officers[i]);
            PoliceOfficerPoseComponent* pose = world.get<PoliceOfficerPoseComponent>(officers[i]);
            if (!off || !pose || off->squad_id != squad->squad_id) {
                continue;
            }
            off->aggression_level = agg;
            off->current_target_entity_id = player_id;
            if (have_player) {
                off->last_known_player_pos = player_pos;
            }

            const float3 delta = float3_sub(player_pos, pose->position_ws);
            const float dist = float3_length(delta);
            const float3 dir = float3_normalize_or(delta, fwd);
            bool blocked = false;
            for (u32 c = 0; c < n_cover; ++c) {
                const TacticalCoverSlabComponent* slab =
                    world.get<TacticalCoverSlabComponent>(covers[c]);
                if (slab && ray_aabb(pose->position_ws, dir, slab->min_ws, slab->max_ws, dist)) {
                    blocked = true;
                    off->cover_position = float3_scale(float3_add(slab->min_ws, slab->max_ws), 0.5f);
                    break;
                }
            }
            off->has_line_of_sight = have_player && !blocked;
            off->accuracy_modifier =
                clampf((off->has_line_of_sight ? 1.0f : 0.55f) * (1.0f - 0.2f * agg), 0.5f, 1.0f);

            float3 goal = player_pos;
            if (off->role == 2) {
                const float side = (flank_slot & 1u) ? 1.0f : -1.0f;
                const float depth = 4.0f + 2.0f * static_cast<float>(flank_slot / 2u);
                goal = float3_add(player_pos, float3_add(float3_scale(right, side * 12.0f),
                                                         float3_scale(fwd, -depth)));
                ++flank_slot;
            } else if (off->role == 1) {
                if (n_cover > 0) {
                    const TacticalCoverSlabComponent* slab =
                        world.get<TacticalCoverSlabComponent>(covers[0]);
                    if (slab) {
                        goal = float3_add(float3_scale(float3_add(slab->min_ws, slab->max_ws), 0.5f),
                                          float3_scale(right, -2.0f));
                    }
                }
                off->is_in_cover = blocked;
            } else if (off->role == 3) {
                goal = float3_add(player_pos, float3_scale(fwd, -28.0f));
            } else {
                goal = float3_add(player_pos, float3_scale(fwd, -6.0f));
            }
            off->cover_position = goal;
            if (!off->has_line_of_sight && off->role == 1) {
                off->suppress_timer_s = 0.f;
            } else if (off->has_line_of_sight) {
                off->suppress_timer_s += delta_time;
            }

            const float speed = off->role == 2 ? 5.2f : 4.0f;
            integrate_pose(&pose->position_ws, &pose->velocity_ws, goal, speed, delta_time);
            NavigationAgentComponent* nav = world.get<NavigationAgentComponent>(officers[i]);
            if (nav) {
                nav->current_position = pose->position_ws;
                nav->target_position  = goal;
                nav->current_velocity = pose->velocity_ws;
                nav->navigation_mode  = 1;
            }
            if (off->role == 2) {
                const float3 to_goal = float3_sub(goal, pose->position_ws);
                const float3 moved   = pose->velocity_ws;
                if (float3_dot(to_goal, moved) >= 0.f || float3_length(to_goal) < 6.0f) {
                    ++flanking;
                }
            }
        }
    }

    for (Entity e : world.query<K9UnitComponent, PoliceOfficerPoseComponent>()) {
        K9UnitComponent* k9 = world.get<K9UnitComponent>(e);
        PoliceOfficerPoseComponent* pose = world.get<PoliceOfficerPoseComponent>(e);
        ENGINE_ASSERT(k9 && pose, "k9 stale");
        float3 best = k9->last_known_scent_pos;
        float best_s = 0.f;
        if (trail) {
            for (u32 i = 0; i < 16; ++i) {
                if (trail->strength[i] > best_s) {
                    best_s = trail->strength[i];
                    best   = trail->samples[i];
                }
            }
        }
        if (have_player && best_s < 0.2f) {
            best = player_pos;
            best_s = 0.35f;
        }
        k9->last_known_scent_pos = best;
        k9->scent_strength       = max_of(0.f, best_s);
        k9->is_tracking          = k9->scent_strength > 0.08f;
        k9->target_player_entity_id = player_id;
        const float speed = 6.5f * max_of(k9->speed_multiplier, 0.5f);
        integrate_pose(&pose->position_ws, &pose->velocity_ws, best, speed, delta_time);
        NavigationAgentComponent* nav = world.get<NavigationAgentComponent>(e);
        if (nav) {
            nav->current_position = pose->position_ws;
            nav->target_position  = best;
            nav->current_velocity = pose->velocity_ws;
        }
    }

    u32 heli_pursuing = 0;
    for (Entity e : world.query<PoliceHelicopterComponent>()) {
        PoliceHelicopterComponent* h = world.get<PoliceHelicopterComponent>(e);
        ENGINE_ASSERT(h != nullptr, "heli stale");
        h->altitude_m = clampf(h->altitude_m, 50.f, 100.f);
        if (!have_player) {
            continue;
        }
        const float3 want{player_pos.x, h->altitude_m, player_pos.z};
        const float3 delta = float3_sub(want, h->pursuit_position);
        const float dist = float3_length(delta);
        const float3 dir = float3_normalize_or(delta, float3{0.f, 0.f, 1.f});
        const float step = min_of(38.0f * delta_time, dist);
        h->pursuit_position = float3_add(h->pursuit_position, float3_scale(dir, step));
        h->velocity = float3_scale(dir, 38.0f);
        const float xz = std::sqrt((h->pursuit_position.x - player_pos.x) * (h->pursuit_position.x - player_pos.x)
                                   + (h->pursuit_position.z - player_pos.z) * (h->pursuit_position.z - player_pos.z));
        h->has_visual_on_player = xz < 90.0f;
        h->spotlight_intensity  = h->has_visual_on_player ? 1.0f : 0.25f;
        h->target_player_entity_id = player_id;
        if (h->has_visual_on_player) {
            ++heli_pursuing;
        }
    }

    for (Entity e : world.query<AdvancedAITelemetryComponent>()) {
        AdvancedAITelemetryComponent* t = world.get<AdvancedAITelemetryComponent>(e);
        if (!t) {
            continue;
        }
        t->police_squads_spawned = n_squad;
        t->officers_flanking     = flanking;
        u32 k9n = 0;
        for (Entity k : world.query<K9UnitComponent>()) {
            const K9UnitComponent* k9 = world.get<K9UnitComponent>(k);
            if (k9 && k9->is_tracking) {
                ++k9n;
            }
        }
        t->k9_tracking            = k9n;
        t->helicopters_pursuing   = heli_pursuing;
        break;
    }
}

} // namespace engine
