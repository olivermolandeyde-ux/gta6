#include "Engine.h"
#include "gameplay/PlayerController.h"
#include "gameplay/WeaponSystem.h"
#include "mission/DialogSystem.h"
#include "mission/MissionDataLoader.h"
#include "mission/MissionMarker.h"
#include "mission/MissionSystem.h"
#include "mission/ObjectiveTracker.h"
#include "mission/RewardSystem.h"
#include "ui/HUDSystem.h"

#include <cstdio>
#include <cstring>

#ifndef LEONIDA_SOURCE_DIR
#define LEONIDA_SOURCE_DIR "."
#endif

namespace {

void tick_mission(engine::Engine& engine, float dt) {
    engine::World& world = engine.world();
    world.begin_frame(engine.frame() + 1);
    engine::UpdateObjectiveTrackerSystem(world, dt, world.frame_commands());
    engine::UpdateMissionSystem(world, dt, world.frame_commands());
    engine::UpdateDialogSystem(world, dt, world.frame_commands());
    engine::UpdateHUDSystem(world, dt, world.frame_commands());
    engine::UpdateMissionMarkerSystem(world, dt, engine.memory().frame());
    world.frame_commands().playback(world);
}

const engine::MissionStateComponent* mission_state(engine::World& world, engine::u32 id) {
    for (engine::Entity e : world.query<engine::MissionStateComponent>()) {
        const engine::MissionStateComponent* s = world.get<engine::MissionStateComponent>(e);
        if (s && s->mission_id == id) {
            return s;
        }
    }
    return nullptr;
}

engine::Entity mission_entity(engine::World& world, engine::u32 id) {
    for (engine::Entity e : world.query<engine::MissionDefinitionComponent>()) {
        const engine::MissionDefinitionComponent* d = world.get<engine::MissionDefinitionComponent>(e);
        if (d && d->mission_id == id) {
            return e;
        }
    }
    return engine::kNullEntity;
}

const engine::DialogStateComponent* dialog_state(engine::World& world, engine::u32 id) {
    for (engine::Entity e : world.query<engine::DialogStateComponent>()) {
        const engine::DialogStateComponent* s = world.get<engine::DialogStateComponent>(e);
        if (s && s->active_dialog_id == id) {
            return s;
        }
    }
    return nullptr;
}

engine::Entity dialog_entity(engine::World& world, engine::u32 id) {
    for (engine::Entity e : world.query<engine::DialogStateComponent>()) {
        const engine::DialogStateComponent* s = world.get<engine::DialogStateComponent>(e);
        if (s && s->active_dialog_id == id) {
            return e;
        }
    }
    return engine::kNullEntity;
}

} // namespace

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
    engine.boot(budget, /*world_id=*/1);
    World& world = engine.world();

    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "phase12";

    PlayerStateComponent player{};
    player.health          = 100.f;
    player.max_health      = 100.f;
    player.armor           = 0.f;
    player.max_armor       = 50.f;
    player.stamina         = 100.f;
    player.max_stamina     = 100.f;
    player.camera_position = float3{100.f, 1.6f, 200.f};
    player.camera_forward  = float3{0.f, 0.f, 1.f};
    player.move_speed_penalty = 1.f;
    Entity player_e = instantiate_player(world, req, player);

    PlayerWalletComponent wallet{};
    wallet.cash_usd = 0;
    PlayerReputationComponent reputation{};
    reputation.street_rep = 0;
    (void)instantiate_player_wallet(world, req, wallet, reputation);

    MissionGiverPoseComponent giver_pose{};
    giver_pose.position_ws = float3{100.f, 0.f, 200.f};
    Entity giver = world.instantiate(req, giver_pose);

    StoreClerkPoseComponent clerk_pose{};
    clerk_pose.position_ws = float3{150.f, 0.f, 250.f};
    FleshHealthComponent clerk_hp{};
    clerk_hp.health     = 100.f;
    clerk_hp.max_health = 100.f;
    Entity clerk = world.instantiate(req, clerk_pose, clerk_hp);

    WeaponPistolComponent pistol{};
    pistol.owner_entity_id      = player_e.index();
    pistol.ammo_type_id         = 9;
    pistol.current_ammo         = 17;
    pistol.max_ammo             = 17;
    pistol.fire_rate_rpm        = 400.f;
    pistol.recoil_vertical      = 1.0f;
    pistol.recoil_recovery_rate = 1.6f;
    pistol.accuracy_base        = 0.9f;
    pistol.accuracy_moving_penalty = 1.f;
    pistol.reload_time_s        = 1.4f;
    Entity pistol_e = instantiate_pistol(world, req, pistol, float3{100.f, 1.5f, 200.f},
                                         float3{0.f, 0.f, 1.f});

    HUDStateComponent hud{};
    hud.health.screen_pos = float2{32.f, 640.f};
    hud.health.size       = float2{220.f, 18.f};
    hud.ammo.screen_pos   = float2{1100.f, 640.f};
    hud.minimap.screen_pos = float2{32.f, 32.f};
    hud.minimap.size      = float2{180.f, 180.f};
    hud.minimap.zoom_level = 1.f;
    Entity hud_e = instantiate_hud(world, req, hud);

    const char* mission_path = LEONIDA_SOURCE_DIR "/data/missions/test_mission.mission";
    ENGINE_ASSERT(WriteTestConvenienceStoreMission(mission_path), "write test_mission.mission");
    ENGINE_ASSERT(LoadMissionsFromFile(world, mission_path, world.frame_commands()),
                  "load test_mission.mission");

    Entity mission_e = mission_entity(world, 1201);
    ENGINE_ASSERT(!mission_e.is_null(), "mission 1201 missing");
    MissionDefinitionComponent* def = world.get<MissionDefinitionComponent>(mission_e);
    ENGINE_ASSERT(def != nullptr, "definition");
    def->giver_entity_id = giver.index();

    for (Entity e : world.query<MissionObjectiveComponent>()) {
        MissionObjectiveComponent* o = world.get<MissionObjectiveComponent>(e);
        if (o && o->mission_id == 1201 && o->objective_index == 1) {
            o->target_entity_id = clerk.index();
        }
    }
    for (Entity e : world.query<ObjectiveTrackerComponent>()) {
        ObjectiveTrackerComponent* t = world.get<ObjectiveTrackerComponent>(e);
        if (t && t->mission_id == 1201 && t->objective_index == 1) {
            t->target_entity_id = clerk.index();
        }
    }
    for (Entity e : world.query<ThreatenAimObjectiveComponent>()) {
        ThreatenAimObjectiveComponent* t = world.get<ThreatenAimObjectiveComponent>(e);
        if (t && t->mission_id == 1201) {
            t->clerk_entity_id = clerk.index();
        }
    }
    for (Entity e : world.query<KillObjectiveComponent>()) {
        KillObjectiveComponent* t = world.get<KillObjectiveComponent>(e);
        if (t && t->mission_id == 1201) {
            t->target_entity_id = clerk.index();
        }
    }

    constexpr float kDt = 1.0f / 60.0f;
    constexpr u32 kMissionId = 1201;

    request_mission_accept(world, mission_e);
    bool became_active = false;
    for (u32 i = 0; i < 10; ++i) {
        tick_mission(engine, kDt);
        const MissionStateComponent* st = mission_state(world, kMissionId);
        if (st && st->current_state == kMissionActive) {
            became_active = true;
            break;
        }
    }
    ENGINE_ASSERT(became_active, "mission did not become ACTIVE");

    // Greeting (node 1) auto-advances after duration.
    bool at_choice = false;
    for (u32 i = 0; i < 80; ++i) {
        tick_mission(engine, kDt);
        const DialogStateComponent* ds = dialog_state(world, kMissionId);
        if (ds && ds->current_node_id == 2) {
            at_choice = true;
            break;
        }
    }
    ENGINE_ASSERT(at_choice, "dialog did not reach threat node");
    Entity ds_e = dialog_entity(world, kMissionId);
    request_dialog_choice(world, ds_e, 0);

    bool left_choice = false;
    for (u32 i = 0; i < 80; ++i) {
        tick_mission(engine, kDt);
        const DialogStateComponent* ds = dialog_state(world, kMissionId);
        if (ds && (ds->current_node_id == 3 || ds->current_node_id == 0 || !ds->is_active)) {
            left_choice = true;
        }
        if (ds && ds->current_node_id == 0 && !ds->is_active) {
            break;
        }
    }
    ENGINE_ASSERT(left_choice, "dialog choice did not advance");
    for (u32 i = 0; i < 80; ++i) {
        const DialogStateComponent* ds = dialog_state(world, kMissionId);
        if (ds && !ds->is_active) {
            break;
        }
        tick_mission(engine, kDt);
    }

    // Objective 1: goto store
    PlayerStateComponent* ps = world.get<PlayerStateComponent>(player_e);
    ENGINE_ASSERT(ps != nullptr, "player");
    ps->camera_position = float3{150.f, 1.6f, 250.f};
    bool obj0 = false;
    for (u32 i = 0; i < 10; ++i) {
        tick_mission(engine, kDt);
        const MissionStateComponent* st = mission_state(world, kMissionId);
        if (st && st->current_objective_index >= 1) {
            obj0 = true;
            break;
        }
    }
    ENGINE_ASSERT(obj0, "goto store did not complete");

    const HUDStateComponent* hud_live = world.get<HUDStateComponent>(hud_e);
    ENGINE_ASSERT(hud_live != nullptr && hud_live->minimap.blip_count > 0, "mission markers missing on minimap");

    // Objective 2: point pistol at clerk
    ps = world.get<PlayerStateComponent>(player_e);
    ps->current_weapon_entity_id = pistol_e.index();
    ps->camera_position          = float3{150.f, 1.6f, 250.f};
    ps->camera_forward           = float3{0.f, -1.f, 0.f};
    WeaponTriggerComponent* trig = world.get<WeaponTriggerComponent>(pistol_e);
    ENGINE_ASSERT(trig != nullptr, "pistol trigger");
    trig->aim_dir_ws = float3{0.f, -1.f, 0.f};
    trig->muzzle_ws  = float3{150.f, 1.5f, 250.f};
    bool obj1 = false;
    for (u32 i = 0; i < 10; ++i) {
        tick_mission(engine, kDt);
        const MissionStateComponent* st = mission_state(world, kMissionId);
        if (st && st->current_objective_index >= 2) {
            obj1 = true;
            break;
        }
    }
    ENGINE_ASSERT(obj1, "point weapon at clerk did not complete");

    // Objective 3: 200 m from store
    ps = world.get<PlayerStateComponent>(player_e);
    ps->camera_position = float3{150.f, 1.6f, 450.f};
    bool completed = false;
    for (u32 i = 0; i < 10; ++i) {
        tick_mission(engine, kDt);
        const MissionStateComponent* st = mission_state(world, kMissionId);
        if (st && st->current_state == kMissionCompleted) {
            completed = true;
            break;
        }
    }
    ENGINE_ASSERT(completed, "escape did not complete the mission");

    const PlayerWalletComponent* w = nullptr;
    const PlayerReputationComponent* r = nullptr;
    for (Entity e : world.query<PlayerWalletComponent, PlayerReputationComponent>()) {
        w = world.get<PlayerWalletComponent>(e);
        r = world.get<PlayerReputationComponent>(e);
        break;
    }
    ENGINE_ASSERT(w && r, "wallet");
    ENGINE_ASSERT(w->cash_usd == 500, "reward money not $500");
    ENGINE_ASSERT(r->street_rep == 10, "reward reputation not +10");

    const MissionTelemetryComponent* tel = nullptr;
    for (Entity e : world.query<MissionTelemetryComponent>()) {
        tel = world.get<MissionTelemetryComponent>(e);
        break;
    }
    ENGINE_ASSERT(tel != nullptr, "telemetry");
    ENGINE_ASSERT(tel->missions_loaded >= 1, "missions loaded");
    ENGINE_ASSERT(tel->state_transitions >= 2, "state transitions");
    ENGINE_ASSERT(tel->objectives_completed >= 3, "objectives completed");
    ENGINE_ASSERT(tel->dialog_nodes_traversed >= 3, "dialog nodes traversed");
    ENGINE_ASSERT(tel->rewards_money == 500, "telemetry money");
    ENGINE_ASSERT(tel->rewards_reputation == 10, "telemetry rep");

    hud_live = world.get<HUDStateComponent>(hud_e);
    ENGINE_ASSERT(hud_live != nullptr, "hud");

    std::printf("MICRO-PHASE 12 SANDBOX PASSED\n");
    std::printf("  Missions loaded          : %u\n", tel->missions_loaded);
    std::printf("  Mission state transitions: %u\n", tel->state_transitions);
    std::printf("  Objectives completed     : %u\n", tel->objectives_completed);
    std::printf("  Dialog nodes traversed   : %u\n", tel->dialog_nodes_traversed);
    std::printf("  Rewards granted          : money=%u  reputation=%d\n", tel->rewards_money,
                tel->rewards_reputation);
    std::printf("  HUD minimap blips        : %u\n", hud_live->minimap.blip_count);
    engine.shutdown();
    return 0;
}
