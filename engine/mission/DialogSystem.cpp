#include "mission/DialogSystem.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "mission/MissionSystem.h"
#include "mission/RewardSystem.h"

namespace engine {

namespace {

constexpr u32 kSnapMax = 256;

[[nodiscard]] bool flag_unlocked(World& world, u32 flag_id) {
    if (flag_id == 0) {
        return true;
    }
    for (Entity e : world.query<DialogUnlockFlagComponent>()) {
        const DialogUnlockFlagComponent* f = world.get<DialogUnlockFlagComponent>(e);
        if (f && f->flag_id == flag_id && f->unlocked) {
            return true;
        }
    }
    return false;
}

void bump_dialog_telemetry(World& world) {
    for (Entity e : world.query<MissionTelemetryComponent>()) {
        MissionTelemetryComponent* t = world.get<MissionTelemetryComponent>(e);
        if (t) {
            t->dialog_nodes_traversed += 1;
        }
        break;
    }
}

} // namespace

Entity instantiate_dialog_node(World& world, const InstantiationRequest& request,
                               const DialogNodeComponent& node) {
    validate_instantiation(request);
    if (node.is_player_choice) {
        DialogPlayerChoiceTag tag{};
        tag.dialog_id = node.dialog_id;
        tag.node_id   = node.node_id;
        return world.instantiate(request, node, tag);
    }
    DialogLinearAdvanceTag tag{};
    tag.dialog_id = node.dialog_id;
    return world.instantiate(request, node, tag);
}

Entity instantiate_dialog_response(World& world, const InstantiationRequest& request,
                                   const DialogResponseComponent& response) {
    validate_instantiation(request);
    return world.instantiate(request, response);
}

Entity instantiate_dialog_state(World& world, const InstantiationRequest& request,
                                const DialogStateComponent& state) {
    validate_instantiation(request);
    DialogChoiceRequestComponent choice{};
    choice.chosen_index = 0;
    choice.pending      = 0;
    return world.instantiate(request, state, choice);
}

void request_dialog_choice(World& world, Entity dialog_state, u32 response_index) {
    DialogChoiceRequestComponent* c = world.get<DialogChoiceRequestComponent>(dialog_state);
    ENGINE_ASSERT(c != nullptr, "dialog choice missing");
    c->chosen_index = response_index;
    c->pending      = 1;
}

void UpdateDialogSystem(World& world, float delta_time, CommandBuffer& cmd) {
    (void)cmd;
    ENGINE_ASSERT(delta_time >= 0.f, "dialog dt");

    Entity states[kSnapMax];
    u32 n_states = 0;
    for (Entity e : world.query<DialogStateComponent, DialogChoiceRequestComponent>()) {
        if (n_states < kSnapMax) {
            states[n_states++] = e;
        }
    }

    for (u32 i = 0; i < n_states; ++i) {
        Entity se = states[i];
        DialogStateComponent* st = world.get<DialogStateComponent>(se);
        DialogChoiceRequestComponent* choice = world.get<DialogChoiceRequestComponent>(se);
        ENGINE_ASSERT(st && choice, "dialog state stale");
        if (!st->is_active || st->current_node_id == 0) {
            continue;
        }

        const DialogNodeComponent* node = nullptr;
        for (Entity ne : world.query<DialogNodeComponent>()) {
            const DialogNodeComponent* n = world.get<DialogNodeComponent>(ne);
            if (n && n->dialog_id == st->active_dialog_id && n->node_id == st->current_node_id) {
                node = n;
                break;
            }
        }
        if (!node) {
            st->is_active = false;
            continue;
        }

        st->elapsed_time_s += delta_time;

        u32 next = 0;
        bool advance = false;
        if (node->is_player_choice) {
            if (choice->pending) {
                for (Entity re : world.query<DialogResponseComponent>()) {
                    const DialogResponseComponent* r = world.get<DialogResponseComponent>(re);
                    if (!r || r->dialog_id != st->active_dialog_id
                        || r->parent_node_id != st->current_node_id) {
                        continue;
                    }
                    if (r->response_index != choice->chosen_index) {
                        continue;
                    }
                    if (!flag_unlocked(world, r->prerequisite_flag_id)) {
                        continue;
                    }
                    next = r->next_node_id;
                    advance = true;
                    if (r->increases_reputation) {
                        for (Entity we : world.query<PlayerReputationComponent>()) {
                            PlayerReputationComponent* rp = world.get<PlayerReputationComponent>(we);
                            if (rp) {
                                rp->street_rep += 1;
                            }
                            break;
                        }
                    }
                    break;
                }
                choice->pending = 0;
            }
        } else if (st->elapsed_time_s >= node->duration_s) {
            next = node->next_node_id;
            advance = true;
        }

        if (!advance) {
            continue;
        }
        bump_dialog_telemetry(world);
        st->elapsed_time_s = 0.f;
        st->current_node_id = next;
        if (next == 0) {
            st->is_active = false;
        }
    }
}

} // namespace engine
