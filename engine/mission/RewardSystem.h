#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

struct RewardEvent {
    u32 mission_id;
    u32 reward_money;
    u32 reward_reputation;
    u32 reward_weapon_type_id;
    u32 reward_item_type_id;
    u32 reward_item_quantity;
};

struct PlayerWalletComponent {
    u32 cash_usd;
};

struct PlayerReputationComponent {
    i32 street_rep;
};

struct RewardUnlockNoticeComponent {
    u32 mission_id;
    u32 money;
    i32 reputation;
    u8  shown;
};

[[nodiscard]] Entity instantiate_player_wallet(World& world, const InstantiationRequest& request,
                                               const PlayerWalletComponent& wallet,
                                               const PlayerReputationComponent& rep);

void ProcessRewardSystem(World& world, const RewardEvent* events, u32 event_count, CommandBuffer& cmd);

} // namespace engine
