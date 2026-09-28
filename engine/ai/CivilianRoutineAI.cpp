#include "ai/CivilianRoutineAI.h"

#include "ai/AIDataLoader.h"
#include "ai/AdvancedNavigation.h"
#include "ai/PoliceTacticalAI.h"
#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"

namespace engine {

namespace {

[[nodiscard]] u32 activity_for_hour(const ScheduleDefinition& def, float hour) {
    float h = hour;
    while (h < 0.f) {
        h += 24.f;
    }
    while (h >= 24.f) {
        h -= 24.f;
    }
    for (u32 i = 0; i < def.activity_count; ++i) {
        const float a = def.activities[i].start_hour;
        const float b = def.activities[i].end_hour;
        if (a <= b) {
            if (h >= a && h < b) {
                return i;
            }
        } else {
            if (h >= a || h < b) {
                return i;
            }
        }
    }
    return 0;
}

} // namespace

Entity instantiate_schedule_definition(World& world, const InstantiationRequest& request,
                                       const ScheduleDefinition& def) {
    validate_instantiation(request);
    return world.instantiate(request, def);
}

Entity instantiate_civilian_routine(World& world, const InstantiationRequest& request,
                                    const CivilianScheduleComponent& schedule,
                                    const CivilianMemoryComponent& memory,
                                    const CivilianRelationshipComponent& rel, float3 position_ws) {
    validate_instantiation(request);
    CivilianPoseComponent pose{};
    pose.position_ws = position_ws;
    NavigationAgentComponent nav{};
    nav.agent_entity_id  = schedule.civilian_entity_id;
    nav.current_position = position_ws;
    nav.target_position  = schedule.current_destination;
    nav.max_speed_ms     = 1.4f;
    nav.navigation_mode  = 0;
    return world.instantiate(request, schedule, memory, rel, pose, nav);
}

void UpdateCivilianRoutineAISystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                                   CommandBuffer& cmd) {
    ENGINE_ASSERT(delta_time >= 0.f, "civilian dt");
    const u32 cap = max_of(1u, world.entity_count());

    CityClockComponent* clock = nullptr;
    for (Entity e : world.query<CityClockComponent>()) {
        clock = world.get<CityClockComponent>(e);
        break;
    }
    float hour = 8.0f;
    float stamp = 0.f;
    if (clock) {
        const float scale = clock->time_scale > 0.f ? clock->time_scale : 1.f;
        clock->world_time_s += delta_time * scale;
        clock->hour_of_day += (delta_time * scale) / 3600.f;
        while (clock->hour_of_day >= 24.f) {
            clock->hour_of_day -= 24.f;
        }
        hour  = clock->hour_of_day;
        stamp = clock->world_time_s;
    }

    Entity* defs = frame_alloc.allocate_array<Entity>(cap);
    u32 n_def = 0;
    for (Entity e : world.query<ScheduleDefinition>()) {
        if (n_def < cap) {
            defs[n_def++] = e;
        }
    }

    Entity* crimes = frame_alloc.allocate_array<Entity>(cap);
    u32 n_crime = 0;
    for (Entity e : world.query<StreetCrimeStimulusComponent>()) {
        if (n_crime < cap) {
            crimes[n_crime++] = e;
        }
    }

    Entity* civs = frame_alloc.allocate_array<Entity>(cap);
    u32 n_civ = 0;
    for (Entity e :
         world.query<CivilianScheduleComponent, CivilianMemoryComponent, CivilianPoseComponent>()) {
        if (n_civ < cap) {
            civs[n_civ++] = e;
        }
    }

    float3 player_pos{};
    u32 player_id = 0;
    for (Entity e : world.query<PlayerStateComponent>()) {
        const PlayerStateComponent* p = world.get<PlayerStateComponent>(e);
        if (p) {
            player_pos = p->camera_position;
            player_id  = e.index();
        }
        break;
    }

    u32 on_schedule = 0;
    u32 witnessed   = 0;
    u32 called      = 0;

    for (u32 i = 0; i < n_civ; ++i) {
        Entity ce = civs[i];
        CivilianScheduleComponent* sch = world.get<CivilianScheduleComponent>(ce);
        CivilianMemoryComponent* mem   = world.get<CivilianMemoryComponent>(ce);
        CivilianPoseComponent* pose    = world.get<CivilianPoseComponent>(ce);
        CivilianRelationshipComponent* rel = world.get<CivilianRelationshipComponent>(ce);
        ENGINE_ASSERT(sch && mem && pose, "civilian stale");

        const ScheduleDefinition* def = nullptr;
        for (u32 d = 0; d < n_def; ++d) {
            const ScheduleDefinition* cand = world.get<ScheduleDefinition>(defs[d]);
            if (cand && cand->schedule_id == sch->schedule_id) {
                def = cand;
                break;
            }
        }
        if (def && def->activity_count > 0) {
            const u32 idx = activity_for_hour(*def, hour);
            if (idx != sch->current_activity_index) {
                sch->current_activity_index = idx;
                sch->activity_start_time    = stamp;
            }
            sch->current_destination = def->activities[idx].location;
            ++on_schedule;
        }

        const float3 dest = sch->current_destination;
        const float dist = float3_length(float3_sub(dest, pose->position_ws));
        sch->is_at_destination = dist < 1.5f;
        NavigationAgentComponent* nav = world.get<NavigationAgentComponent>(ce);
        if (nav) {
            nav->target_position  = dest;
            nav->current_position = pose->position_ws;
            nav->navigation_mode  = sch->is_at_destination ? 0 : 0;
            if (nav->is_avoiding_danger) {
                nav->navigation_mode = 2;
            }
        }

        // Memory decay
        u32 keep = 0;
        CivilianMemoryComponent::Memory tmp[16]{};
        for (u32 m = 0; m < mem->memory_count && m < 16; ++m) {
            mem->memories[m].severity *= max_of(0.f, 1.0f - 0.02f * delta_time);
            if (mem->memories[m].severity >= 0.05f) {
                tmp[keep++] = mem->memories[m];
            }
        }
        mem->memory_count = keep;
        for (u32 m = 0; m < keep; ++m) {
            mem->memories[m] = tmp[m];
        }

        for (u32 c = 0; c < n_crime; ++c) {
            const StreetCrimeStimulusComponent* crime =
                world.get<StreetCrimeStimulusComponent>(crimes[c]);
            if (!crime) {
                continue;
            }
            const float cd = float3_length(float3_sub(pose->position_ws, crime->position_ws));
            if (cd > 25.0f) {
                continue;
            }
            bool already = false;
            for (u32 m = 0; m < mem->memory_count; ++m) {
                if (mem->memories[m].event_type == crime->event_type
                    && mem->memories[m].perpetrator_entity_id == crime->perpetrator_entity_id) {
                    already = true;
                    mem->memories[m].severity =
                        max_of(mem->memories[m].severity, crime->severity);
                }
            }
            if (!already && mem->memory_count < 16) {
                CivilianMemoryComponent::Memory rec{};
                rec.timestamp              = stamp;
                rec.event_type             = crime->event_type;
                rec.event_location         = crime->position_ws;
                rec.perpetrator_entity_id  = crime->perpetrator_entity_id;
                rec.severity               = crime->severity;
                mem->memories[mem->memory_count++] = rec;
            }
            if (rel && crime->perpetrator_entity_id == player_id) {
                rel->knows_player_identity = true;
                rel->reputation_with_player = max_of(-100.f, rel->reputation_with_player - 40.f);
                rel->is_hostile = rel->reputation_with_player < -50.f;
            }
        }

        bool saw = false;
        float max_sev = 0.f;
        for (u32 m = 0; m < mem->memory_count; ++m) {
            if (mem->memories[m].severity > 0.01f) {
                saw = true;
            }
            max_sev = max_of(max_sev, mem->memories[m].severity);
        }
        if (saw) {
            ++witnessed;
        }

        bool already_called = world.has<CivilianCalledPoliceTag>(ce);
        if (!already_called && max_sev > 0.7f && mem->trust_in_authority > 0.5f) {
            PoliceDispatchCallComponent call{};
            call.location_ws      = pose->position_ws;
            call.caller_entity_id = sch->civilian_entity_id;
            call.severity         = max_sev;
            call.consumed         = 0;
            cmd.add_component(ce, world.registry().id_of<CivilianCalledPoliceTag>(),
                              CivilianCalledPoliceTag{sch->civilian_entity_id, 0});
            InstantiationRequest req{};
            req.domain      = InstantiationDomain::PersistentWorld;
            req.debug_label = "police_dispatch";
            (void)world.instantiate(req, call);
            ++called;
            for (Entity pe : world.query<PlayerStateComponent>()) {
                PlayerStateComponent* p = world.get<PlayerStateComponent>(pe);
                if (p) {
                    p->wanted_level = min_of(5.f, p->wanted_level + 1.0f);
                }
                break;
            }
        } else if (already_called) {
            ++called;
        }
        (void)player_pos;
    }

    for (Entity e : world.query<AdvancedAITelemetryComponent>()) {
        AdvancedAITelemetryComponent* t = world.get<AdvancedAITelemetryComponent>(e);
        if (t) {
            t->civilians_on_schedule      = on_schedule;
            t->civilians_witnessed_crime  = witnessed;
            t->civilians_called_police    = called;
        }
        break;
    }
}

} // namespace engine
