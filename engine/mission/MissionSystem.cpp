#include "mission/MissionSystem.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"
#include "mission/DialogSystem.h"
#include "mission/ObjectiveTracker.h"
#include "mission/RewardSystem.h"

namespace engine {

namespace {

constexpr u32 kSnapMax = 256;

[[nodiscard]] bool prereq_complete(World& world, u32 prereq_id) {
    if (prereq_id == 0) {
        return true;
    }
    for (Entity e : world.query<MissionStateComponent>()) {
        const MissionStateComponent* s = world.get<MissionStateComponent>(e);
        if (s && s->mission_id == prereq_id) {
            return s->current_state == kMissionCompleted;
        }
    }
    return false;
}

[[nodiscard]] u32 objective_total(World& world, u32 mission_id) {
    u32 n = 0;
    for (Entity e : world.query<MissionObjectiveComponent>()) {
        const MissionObjectiveComponent* o = world.get<MissionObjectiveComponent>(e);
        if (o && o->mission_id == mission_id) {
            ++n;
        }
    }
    return n;
}

[[nodiscard]] bool objective_done(World& world, u32 mission_id, u32 index) {
    for (Entity e : world.query<MissionObjectiveComponent>()) {
        const MissionObjectiveComponent* o = world.get<MissionObjectiveComponent>(e);
        if (o && o->mission_id == mission_id && o->objective_index == index) {
            return o->is_completed;
        }
    }
    return false;
}

void activate_dialog(World& world, u32 dialog_id) {
    for (Entity e : world.query<DialogStateComponent>()) {
        DialogStateComponent* st = world.get<DialogStateComponent>(e);
        if (!st || st->active_dialog_id != dialog_id) {
            continue;
        }
        st->is_active = true;
        if (st->current_node_id == 0) {
            st->current_node_id = 1;
        }
        st->elapsed_time_s = 0.f;
    }
}

void bump_transition(World& world) {
    for (Entity e : world.query<MissionTelemetryComponent>()) {
        MissionTelemetryComponent* t = world.get<MissionTelemetryComponent>(e);
        if (t) {
            t->state_transitions += 1;
        }
        break;
    }
}

} // namespace

Entity instantiate_mission(World& world, const InstantiationRequest& request,
                           const MissionDefinitionComponent& def,
                           const MissionStateComponent& state) {
    validate_instantiation(request);
    MissionGiverInteractComponent interact{};
    interact.requested = 0;
    if (def.mission_id == 1201) {
        ConvenienceStoreRobberyTag tag{};
        tag.register_cash_usd = 500;
        tag.cop_heat_add      = 2;
        return world.instantiate(request, def, state, interact, tag);
    }
    return world.instantiate(request, def, state, interact);
}

void request_mission_accept(World& world, Entity mission) {
    MissionGiverInteractComponent* ix = world.get<MissionGiverInteractComponent>(mission);
    ENGINE_ASSERT(ix != nullptr, "mission interact missing");
    ix->requested = 1;
}

void UpdateMissionSystem(World& world, float delta_time, CommandBuffer& cmd) {
    ENGINE_ASSERT(delta_time >= 0.f, "mission dt");

    MissionSimClockComponent* clock = nullptr;
    for (Entity e : world.query<MissionSimClockComponent>()) {
        clock = world.get<MissionSimClockComponent>(e);
        break;
    }
    float world_time = 0.f;
    if (clock) {
        clock->world_time_s += delta_time;
        world_time = clock->world_time_s;
    }

    float3 player_pos{};
    float player_health = 100.f;
    bool have_player = false;
    for (Entity e : world.query<PlayerStateComponent>()) {
        const PlayerStateComponent* p = world.get<PlayerStateComponent>(e);
        if (p) {
            player_pos    = p->camera_position;
            player_health = p->health;
            have_player   = true;
            break;
        }
    }

    Entity missions[kSnapMax];
    u32 n = 0;
    for (Entity e : world.query<MissionDefinitionComponent, MissionStateComponent,
                                MissionGiverInteractComponent>()) {
        if (n < kSnapMax) {
            missions[n++] = e;
        }
    }

    RewardEvent rewards[8]{};
    u32 n_rewards = 0;

    for (u32 i = 0; i < n; ++i) {
        Entity e = missions[i];
        MissionDefinitionComponent* def = world.get<MissionDefinitionComponent>(e);
        MissionStateComponent* st       = world.get<MissionStateComponent>(e);
        MissionGiverInteractComponent* ix = world.get<MissionGiverInteractComponent>(e);
        ENGINE_ASSERT(def && st && ix, "mission stale");

        if (st->current_state == kMissionNotStarted) {
            if (ix->requested && have_player) {
                ix->requested = 0;
                const float dist = float3_length(float3_sub(player_pos, def->start_location));
                if (dist <= 6.0f && prereq_complete(world, def->prerequisite_mission_id)) {
                    st->current_state            = kMissionActive;
                    st->current_objective_index  = 0;
                    st->time_started             = world_time;
                    st->is_timer_running         = st->time_limit_s > 0.f;
                    activate_dialog(world, def->mission_id);
                    bump_transition(world);
                }
            }
            continue;
        }

        if (st->current_state != kMissionActive) {
            continue;
        }

        if (have_player && player_health <= 0.f) {
            st->current_state    = kMissionFailed;
            st->is_timer_running = false;
            bump_transition(world);
            continue;
        }
        if (st->is_timer_running && st->time_limit_s > 0.f) {
            if (world_time - st->time_started >= st->time_limit_s) {
                st->current_state    = kMissionFailed;
                st->is_timer_running = false;
                bump_transition(world);
                continue;
            }
        }

        const u32 total = objective_total(world, st->mission_id);
        while (st->current_objective_index < total
               && objective_done(world, st->mission_id, st->current_objective_index)) {
            st->current_objective_index += 1;
        }
        if (total > 0 && st->current_objective_index >= total) {
            st->current_state    = kMissionCompleted;
            st->is_timer_running = false;
            bump_transition(world);
            if (n_rewards < 8) {
                RewardEvent ev{};
                ev.mission_id             = def->mission_id;
                ev.reward_money           = def->reward_money;
                ev.reward_reputation      = def->reward_reputation;
                ev.reward_weapon_type_id  = def->reward_weapon_type_id;
                ev.reward_item_type_id    = 0;
                ev.reward_item_quantity   = 0;
                rewards[n_rewards++]      = ev;
            }
        }
    }

    if (n_rewards > 0) {
        ProcessRewardSystem(world, rewards, n_rewards, cmd);
    }
}

} // namespace engine
