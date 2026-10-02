#include "ai/AdvancedNavigation.h"

#include "ai/AIDataLoader.h"
#include "ai/CivilianRoutineAI.h"
#include "ai/GangTerritoryAI.h"
#include "ai/PoliceTacticalAI.h"
#include "core/Assert.h"
#include "ecs/World.h"

namespace engine {

namespace {

[[nodiscard]] float danger_cost(float3 pos, float3 center, float radius, float severity) {
    const float d = float3_length(float3_sub(pos, center));
    if (d >= radius) {
        return 0.f;
    }
    const float t = 1.0f - d / max_of(radius, 0.001f);
    return severity * t * t;
}

} // namespace

Entity instantiate_navigation_agent(World& world, const InstantiationRequest& request,
                                    const NavigationAgentComponent& agent) {
    validate_instantiation(request);
    return world.instantiate(request, agent);
}

Entity instantiate_danger_zone(World& world, const InstantiationRequest& request,
                               const DangerZoneComponent& zone) {
    validate_instantiation(request);
    return world.instantiate(request, zone);
}

void UpdateAdvancedNavigationSystem(World& world, float delta_time, FrameAllocator& frame_alloc) {
    ENGINE_ASSERT(delta_time >= 0.f, "nav dt");
    const u32 cap = max_of(1u, world.entity_count());

    Entity* zones = frame_alloc.allocate_array<Entity>(cap);
    u32 n_zone = 0;
    for (Entity e : world.query<DangerZoneComponent>()) {
        if (n_zone < cap) {
            zones[n_zone++] = e;
        }
    }

    Entity* nodes = frame_alloc.allocate_array<Entity>(cap);
    u32 n_node = 0;
    for (Entity e : world.query<StreetPathNodeComponent>()) {
        if (n_node < cap) {
            nodes[n_node++] = e;
        }
    }

    Entity* agents = frame_alloc.allocate_array<Entity>(cap);
    u32 n_ag = 0;
    for (Entity e : world.query<NavigationAgentComponent>()) {
        if (n_ag < cap) {
            agents[n_ag++] = e;
        }
    }

    u32 avoided = 0;
    for (u32 i = 0; i < n_ag; ++i) {
        NavigationAgentComponent* a = world.get<NavigationAgentComponent>(agents[i]);
        ENGINE_ASSERT(a != nullptr, "nav agent stale");

        float worst = 0.f;
        float3 worst_c{};
        float worst_r = 0.f;
        for (u32 z = 0; z < n_zone; ++z) {
            DangerZoneComponent* zone = world.get<DangerZoneComponent>(zones[z]);
            if (!zone) {
                continue;
            }
            if (zone->duration_s > 0.f) {
                zone->time_created += delta_time;
            }
            const float c = danger_cost(a->current_position, zone->center_position, zone->radius_m,
                                        zone->severity);
            if (c > worst) {
                worst   = c;
                worst_c = zone->center_position;
                worst_r = zone->radius_m;
            }
        }
        a->is_avoiding_danger = worst > 0.05f;
        if (a->is_avoiding_danger) {
            a->danger_position = worst_c;
            a->danger_radius_m = worst_r;
            if (a->navigation_mode != 3) {
                a->navigation_mode = 2;
            }
            ++avoided;
        }

        float3 desired = a->target_position;
        if (a->path_node_count > 0 && a->current_path_index < a->path_node_count) {
            const u32 nid = a->path_node_ids[a->current_path_index];
            bool found = false;
            float3 node_pos = desired;
            for (u32 n = 0; n < n_node; ++n) {
                const StreetPathNodeComponent* pn = world.get<StreetPathNodeComponent>(nodes[n]);
                if (pn && pn->node_id == nid) {
                    node_pos = pn->position_ws;
                    found    = true;
                    break;
                }
            }
            if (found) {
                const float node_danger = danger_cost(node_pos, worst_c, worst_r, worst);
                if (node_danger > 0.2f) {
                    a->current_path_index =
                        min_of(a->current_path_index + 1, max_of(1u, a->path_node_count) - 1);
                } else {
                    desired = node_pos;
                    if (float3_length(float3_sub(a->current_position, node_pos)) < 1.2f) {
                        a->current_path_index =
                            min_of(a->current_path_index + 1, a->path_node_count);
                    }
                }
            }
        }

        float3 steer = float3_sub(desired, a->current_position);
        if (a->is_avoiding_danger) {
            const float3 away = float3_sub(a->current_position, a->danger_position);
            const float ad = float3_length(away);
            if (ad > 1e-4f) {
                const float push = (a->danger_radius_m + 2.0f - ad);
                if (push > 0.f) {
                    steer = float3_add(steer, float3_scale(away, (push * 4.0f) / ad));
                }
            }
        }

        // Crowd separation
        for (u32 j = 0; j < n_ag; ++j) {
            if (j == i) {
                continue;
            }
            const NavigationAgentComponent* o = world.get<NavigationAgentComponent>(agents[j]);
            if (!o) {
                continue;
            }
            const float3 rel = float3_sub(a->current_position, o->current_position);
            const float d = float3_length(rel);
            if (d > 0.05f && d < 1.6f) {
                steer = float3_add(steer, float3_scale(rel, (1.6f - d) / d));
            }
        }

        const float slen = float3_length(steer);
        float3 vel{0.f, 0.f, 0.f};
        if (slen > 1e-4f) {
            const float speed = a->navigation_mode == 2 ? a->max_speed_ms * 1.6f
                                : a->navigation_mode == 1 ? a->max_speed_ms * 1.25f
                                                          : a->max_speed_ms;
            vel = float3_scale(steer, speed / slen);
        }
        a->current_velocity = vel;
        a->current_position =
            float3_add(a->current_position, float3_scale(vel, delta_time));

        PoliceOfficerPoseComponent* pop = world.get<PoliceOfficerPoseComponent>(agents[i]);
        if (pop && a->navigation_mode == 2) {
            pop->position_ws = a->current_position;
            pop->velocity_ws = vel;
        }
        CivilianPoseComponent* cp = world.get<CivilianPoseComponent>(agents[i]);
        if (cp) {
            cp->position_ws = a->current_position;
        }
        GangMemberPoseComponent* gp = world.get<GangMemberPoseComponent>(agents[i]);
        if (gp && a->is_avoiding_danger) {
            gp->position_ws = a->current_position;
        }
    }

    for (Entity e : world.query<AdvancedAITelemetryComponent>()) {
        AdvancedAITelemetryComponent* t = world.get<AdvancedAITelemetryComponent>(e);
        if (t) {
            t->danger_zones_avoided = avoided;
        }
        break;
    }
}

} // namespace engine
