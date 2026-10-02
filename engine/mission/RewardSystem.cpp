#include "mission/RewardSystem.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/InventorySystem.h"
#include "gameplay/PlayerController.h"
#include "mission/MissionSystem.h"

namespace engine {

Entity instantiate_player_wallet(World& world, const InstantiationRequest& request,
                                 const PlayerWalletComponent& wallet,
                                 const PlayerReputationComponent& rep) {
    validate_instantiation(request);
    return world.instantiate(request, wallet, rep);
}

void ProcessRewardSystem(World& world, const RewardEvent* events, u32 event_count, CommandBuffer& cmd) {
    (void)cmd;
    if (events == nullptr || event_count == 0) {
        return;
    }

    PlayerWalletComponent* wallet = nullptr;
    PlayerReputationComponent* rep = nullptr;
    for (Entity e : world.query<PlayerWalletComponent, PlayerReputationComponent>()) {
        wallet = world.get<PlayerWalletComponent>(e);
        rep    = world.get<PlayerReputationComponent>(e);
        break;
    }
    ENGINE_ASSERT(wallet && rep, "player wallet/reputation missing");

    MissionTelemetryComponent* tel = nullptr;
    for (Entity e : world.query<MissionTelemetryComponent>()) {
        tel = world.get<MissionTelemetryComponent>(e);
        break;
    }

    for (u32 i = 0; i < event_count; ++i) {
        const RewardEvent& ev = events[i];
        wallet->cash_usd += ev.reward_money;
        rep->street_rep += static_cast<i32>(ev.reward_reputation);
        if (tel) {
            tel->rewards_money += ev.reward_money;
            tel->rewards_reputation += static_cast<i32>(ev.reward_reputation);
        }
        if (ev.reward_item_type_id != 0 && ev.reward_item_quantity > 0) {
            InventorySlotComponent slot{};
            slot.item_type_id = ev.reward_item_type_id;
            slot.quantity     = ev.reward_item_quantity;
            slot.max_stack    = 99;
            slot.grid_x       = 7;
            slot.grid_y       = 4;
            slot.grid_w       = 1;
            slot.grid_h       = 1;
            slot.is_equipped  = false;
            slot.durability   = 100;
            InstantiationRequest req{};
            req.domain      = InstantiationDomain::PersistentWorld;
            req.debug_label = "reward_item";
            (void)world.instantiate(req, slot);
        }
        RewardUnlockNoticeComponent notice{};
        notice.mission_id  = ev.mission_id;
        notice.money       = ev.reward_money;
        notice.reputation  = static_cast<i32>(ev.reward_reputation);
        notice.shown       = 1;
        InstantiationRequest nreq{};
        nreq.domain      = InstantiationDomain::PersistentWorld;
        nreq.debug_label = "reward_notice";
        (void)world.instantiate(nreq, notice);
        (void)ev.reward_weapon_type_id;
    }
}

} // namespace engine
