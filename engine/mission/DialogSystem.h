#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

struct DialogNodeComponent {
    u32   dialog_id;
    u32   node_id;
    char  speaker_name[32];
    char  dialog_text[256];
    u32   audio_event_id;
    float duration_s;
    u32   next_node_id;
    bool  is_player_choice;
};

struct DialogResponseComponent {
    u32  dialog_id;
    u32  parent_node_id;
    u32  response_index;
    char response_text[128];
    u32  next_node_id;
    u32  prerequisite_flag_id;
    bool increases_reputation;
};

struct DialogStateComponent {
    u32   active_dialog_id;
    u32   current_node_id;
    bool  is_active;
    float elapsed_time_s;
};

struct DialogChoiceRequestComponent {
    u32 chosen_index;
    u8  pending;
};

struct DialogUnlockFlagComponent {
    u32 flag_id;
    u8  unlocked;
};

struct DialogLinearAdvanceTag {
    u32 dialog_id;
};

struct DialogPlayerChoiceTag {
    u32 dialog_id;
    u32 node_id;
};

[[nodiscard]] Entity instantiate_dialog_node(World& world, const InstantiationRequest& request,
                                             const DialogNodeComponent& node);

[[nodiscard]] Entity instantiate_dialog_response(World& world, const InstantiationRequest& request,
                                                 const DialogResponseComponent& response);

[[nodiscard]] Entity instantiate_dialog_state(World& world, const InstantiationRequest& request,
                                              const DialogStateComponent& state);

void request_dialog_choice(World& world, Entity dialog_state, u32 response_index);

void UpdateDialogSystem(World& world, float delta_time, CommandBuffer& cmd);

} // namespace engine
