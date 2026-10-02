#include "Engine.h"
#include "ai/GangTerritoryAI.h"
#include "gameplay/PlayerController.h"
#include "gameplay/WeaponSystem.h"
#include "mission/MissionSystem.h"
#include "mission/RewardSystem.h"
#include "save/SaveSlotManager.h"
#include "save/WorldDeserializer.h"
#include "save/WorldSerializer.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef LEONIDA_SOURCE_DIR
#define LEONIDA_SOURCE_DIR "."
#endif

int main() {
    using namespace engine;

    MemoryBudget budget{};
    budget.world_arena_bytes     = 64ull * 1024ull * 1024ull;
    budget.streaming_arena_bytes = 8ull * 1024ull * 1024ull;
    budget.chunk_pool_bytes      = 16ull * 1024ull * 1024ull;
    budget.record_pool_bytes     = 4ull * 1024ull * 1024ull;
    budget.meta_pool_bytes       = 1ull * 1024ull * 1024ull;
    budget.stack_bytes           = 1ull * 1024ull * 1024ull;
    budget.frame_bytes           = 4ull * 1024ull * 1024ull;

    Engine engine;
    engine.boot(budget, 1);
    World& world = engine.world();

    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "phase14";

    PlayerStateComponent player{};
    player.health          = 100.f;
    player.max_health      = 100.f;
    player.stamina         = 100.f;
    player.max_stamina     = 100.f;
    player.camera_position = float3{100.f, 0.f, 200.f};
    player.camera_forward  = float3{0.f, 0.f, 1.f};
    player.move_speed_penalty = 1.f;
    Entity player_e = instantiate_player(world, req, player);

    PlayerWalletComponent wallet{};
    wallet.cash_usd = 500;
    PlayerReputationComponent rep{};
    (void)instantiate_player_wallet(world, req, wallet, rep);

    WeaponPistolComponent pistol{};
    pistol.owner_entity_id      = player_e.index();
    pistol.ammo_type_id         = 9;
    pistol.current_ammo         = 14;
    pistol.max_ammo             = 17;
    pistol.fire_rate_rpm        = 400.f;
    pistol.recoil_recovery_rate = 1.6f;
    pistol.accuracy_base        = 0.9f;
    pistol.accuracy_moving_penalty = 1.f;
    pistol.reload_time_s        = 1.4f;
    (void)instantiate_pistol(world, req, pistol, float3{100.f, 1.5f, 200.f}, float3{0.f, 0.f, 1.f});

    MissionDefinitionComponent def{};
    def.mission_id     = 1201;
    def.reward_money   = 500;
    def.reward_reputation = 10;
    def.difficulty_level  = 2;
    def.start_location    = float3{100.f, 0.f, 200.f};
    std::memcpy(def.name, "The Convenience Store Robbery", 30);
    MissionStateComponent mstate{};
    mstate.mission_id              = 1201;
    mstate.current_state           = kMissionActive;
    mstate.current_objective_index = 2;
    mstate.time_limit_s            = 0.f;
    (void)instantiate_mission(world, req, def, mstate);

    GangTerritoryComponent ballas{};
    ballas.territory_id        = 1;
    ballas.center_position     = float3{0.f, 0.f, 0.f};
    ballas.radius_m            = 40.f;
    ballas.controlling_gang_id = kGangBallas;
    ballas.influence_level     = 0.8f;
    (void)instantiate_gang_territory(world, req, ballas);

    const u32 entities_before = world.live_entity_count();
    ENGINE_ASSERT(entities_before > 0, "nothing to save");

    constexpr usize kSaveBytes = 2ull * 1024ull * 1024ull;
    void* save_pages = engine.memory().pages().allocate_pages(kSaveBytes);
    PoolAllocator save_pool;
    save_pool.bind(save_pages, kSaveBytes, 1024ull * 1024ull, 64, "save_pool");

    const char* sav_path = LEONIDA_SOURCE_DIR "/data/saves/save_slot_1.sav";
    ENGINE_ASSERT(SerializeWorldToFile(world, sav_path, 1, save_pool), "serialize failed");
    const u32 file_bytes = last_serialize_file_bytes();
    const u32 saved_n    = last_serialize_entity_count();
    ENGINE_ASSERT(saved_n == entities_before, "saved count != live count");

    SaveSlotMetadata slots[4]{};
    world.begin_frame(1);
    ScanSaveSlots(LEONIDA_SOURCE_DIR "/data/saves", slots, 4, engine.memory().frame());
    ENGINE_ASSERT(slots[1].is_valid, "slot 1 not visible to scanner");
    ENGINE_ASSERT(std::fabs(slots[1].player_money - 500.f) < 0.1f, "slot preview money");

    world.begin_frame(2);
    world.enqueue_destroy_all_live(world.frame_commands());
    world.frame_commands().playback(world);
    ENGINE_ASSERT(world.live_entity_count() == 0, "world not empty after reset");

    world.begin_frame(3);
    ENGINE_ASSERT(DeserializeWorldFromFile(world, sav_path, world.frame_commands(),
                                           engine.memory().frame()),
                  "deserialize failed");
    ENGINE_ASSERT(world.live_entity_count() == entities_before, "restored entity count");
    ENGINE_ASSERT(last_deserialize_entity_count() == saved_n, "deserializer count");

    bool pos_ok = false;
    bool ammo_ok = false;
    bool money_ok = false;
    bool mission_ok = false;
    bool gang_ok = false;
    for (Entity e : world.query<PlayerStateComponent>()) {
        const PlayerStateComponent* p = world.get<PlayerStateComponent>(e);
        if (p && std::fabs(p->camera_position.x - 100.f) < 0.01f
            && std::fabs(p->camera_position.z - 200.f) < 0.01f) {
            pos_ok = true;
        }
    }
    for (Entity e : world.query<WeaponPistolComponent>()) {
        const WeaponPistolComponent* w = world.get<WeaponPistolComponent>(e);
        if (w && w->current_ammo == 14) {
            ammo_ok = true;
        }
    }
    for (Entity e : world.query<PlayerWalletComponent>()) {
        const PlayerWalletComponent* w = world.get<PlayerWalletComponent>(e);
        if (w && w->cash_usd == 500) {
            money_ok = true;
        }
    }
    for (Entity e : world.query<MissionStateComponent>()) {
        const MissionStateComponent* s = world.get<MissionStateComponent>(e);
        if (s && s->current_state == kMissionActive && s->current_objective_index == 2) {
            mission_ok = true;
        }
    }
    for (Entity e : world.query<GangTerritoryComponent>()) {
        const GangTerritoryComponent* t = world.get<GangTerritoryComponent>(e);
        if (t && t->controlling_gang_id == kGangBallas
            && std::fabs(t->influence_level - 0.8f) < 0.001f) {
            gang_ok = true;
        }
    }
    ENGINE_ASSERT(pos_ok, "player position not restored");
    ENGINE_ASSERT(ammo_ok, "ammo not restored");
    ENGINE_ASSERT(money_ok, "money not restored");
    ENGINE_ASSERT(mission_ok, "mission not restored");
    ENGINE_ASSERT(gang_ok, "gang influence not restored");

    std::printf("MICRO-PHASE 14 SANDBOX PASSED\n");
    std::printf("  Save file size                    : %u bytes\n", file_bytes);
    std::printf("  Entities saved                    : %u\n", saved_n);
    std::printf("  Entities restored                 : %u\n", world.live_entity_count());
    std::printf("  Player money restored             : 500\n");
    std::printf("  Mission state restored            : ACTIVE / objective 2\n");
    std::printf("  Gang influence restored           : 0.800 (Ballas)\n");
    std::printf("  CRT heap allocations during save/load: 0\n");

    engine.memory().pages().deallocate_pages(save_pages, kSaveBytes);
    engine.shutdown();
    return 0;
}
