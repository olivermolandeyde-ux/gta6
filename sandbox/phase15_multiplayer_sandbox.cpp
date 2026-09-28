#include "Engine.h"
#include "gameplay/PlayerController.h"
#include "multiplayer/CoopMissionMode.h"
#include "multiplayer/DeathmatchMode.h"
#include "multiplayer/GameModeCore.h"
#include "multiplayer/RaceMode.h"
#include "network/InterestManagement.h"
#include "network/NetworkCore.h"
#include "network/StateReplication.h"

#include <cmath>
#include <cstdio>

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
    req.debug_label = "phase15";

    NetworkEndpoint ep_server{0x7F000001u, 7777};
    NetworkEndpoint ep_c[4] = {
        {0x7F000002u, 1001},
        {0x7F000003u, 1002},
        {0x7F000004u, 1003},
        {0x7F000005u, 1004},
    };
    (void)instantiate_network_peer(world, req, ep_server, true, 0, 0);
    Entity peers[4];
    for (u32 i = 0; i < 4; ++i) {
        peers[i] = instantiate_network_peer(world, req, ep_c[i], false, 0, 0);
    }

    MultiplayerTelemetryComponent tel{};
    (void)world.instantiate(req, tel);

    auto spawn_player = [&](float3 pos, float3 fwd, float health) {
        PlayerStateComponent ps{};
        ps.health          = health;
        ps.max_health      = 100.f;
        ps.stamina         = 100.f;
        ps.max_stamina     = 100.f;
        ps.camera_position = pos;
        ps.camera_forward  = fwd;
        ps.move_speed_penalty = 1.f;
        return instantiate_player(world, req, ps);
    };

    Entity p1 = spawn_player(float3{0.f, 0.f, 0.f}, float3{0.f, 0.f, 1.f}, 100.f);
    Entity p2 = spawn_player(float3{0.f, 0.f, 10.f}, float3{0.f, 0.f, -1.f}, 100.f);
    Entity p3 = spawn_player(float3{30.f, 0.f, 0.f}, float3{1.f, 0.f, 0.f}, 100.f);
    Entity p4 = spawn_player(float3{30.f, 0.f, 0.5f}, float3{0.f, 0.f, 1.f}, 0.f);

    StreamingCellId cell{};
    (void)attach_network_identity(world, p1, 11, cell, true);
    (void)attach_network_identity(world, p2, 12, cell, true);
    (void)attach_network_identity(world, p3, 13, cell, true);
    (void)attach_network_identity(world, p4, 14, cell, true);

    auto bind_stats = [&](Entity player, u32 session, u32 team) {
        PlayerSessionStatsComponent st{};
        st.player_entity_id = player.index();
        st.team_id          = team;
        st.is_ready         = true;
        PlayerSessionBindComponent b{};
        b.session_id = session;
        world.add_component(player, st);
        world.add_component(player, b);
    };
    bind_stats(p1, 10, 0);
    bind_stats(p2, 10, 0);
    bind_stats(p3, 20, 0);
    bind_stats(p4, 20, 0);

    PlayerRaceStateComponent race3{};
    race3.player_entity_id      = p3.index();
    race3.current_lap           = 0;
    race3.next_checkpoint_index = 2;
    race3.last_valid_position   = float3{20.f, 0.f, 0.f};
    world.add_component(p3, race3);
    PlayerRaceStateComponent race4{};
    race4.player_entity_id      = p4.index();
    race4.next_checkpoint_index = 0;
    race4.last_valid_position   = float3{10.f, 0.f, 0.f};
    world.add_component(p4, race4);

    CoopDownedComponent down4{};
    down4.player_entity_id = p4.index();
    down4.is_downed        = true;
    world.add_component(p4, down4);

    GameSessionComponent dm{};
    dm.session_id             = 10;
    dm.mode_type              = kModeDeathmatch;
    dm.session_state          = kSessionActive;
    dm.max_players            = 8;
    dm.current_player_count   = 2;
    dm.host_player_entity_id  = p1.index();
    (void)instantiate_game_session(world, req, dm);

    GameSessionComponent race_s{};
    race_s.session_id            = 20;
    race_s.mode_type             = kModeRace;
    race_s.session_state         = kSessionActive;
    race_s.max_players           = 8;
    race_s.current_player_count  = 2;
    race_s.host_player_entity_id = p3.index();
    (void)instantiate_game_session(world, req, race_s);

    GameSessionComponent coop_s{};
    coop_s.session_id            = 30;
    coop_s.mode_type             = kModeCoopMission;
    coop_s.session_state         = kSessionActive;
    coop_s.max_players           = 4;
    coop_s.host_player_entity_id = p3.index();
    (void)instantiate_game_session(world, req, coop_s);

    DeathmatchRuleComponent dmr{};
    dmr.session_id         = 10;
    dmr.score_limit        = 25;
    dmr.time_limit_s       = 600.f;
    dmr.respawn_delay_s    = 1;
    dmr.spawn_point_count  = 2;
    dmr.spawn_points[0]    = float3{0.f, 0.f, -8.f};
    dmr.spawn_points[1]    = float3{4.f, 0.f, -8.f};
    (void)instantiate_deathmatch_rules(world, req, dmr);

    RaceRuleComponent rr{};
    rr.session_id           = 20;
    rr.total_laps           = 3;
    rr.checkpoint_radius_m  = 5.0f;
    rr.checkpoint_count     = 4;
    rr.checkpoint_positions[0] = float3{10.f, 0.f, 0.f};
    rr.checkpoint_positions[1] = float3{20.f, 0.f, 0.f};
    rr.checkpoint_positions[2] = float3{30.f, 0.f, 0.f};
    rr.checkpoint_positions[3] = float3{40.f, 0.f, 0.f};
    (void)instantiate_race_rules(world, req, rr);

    CoopMissionRuleComponent cr{};
    cr.session_id               = 30;
    cr.mission_id               = 1201;
    cr.player_difficulty_scale  = 12;
    cr.required_objectives      = 3;
    (void)instantiate_coop_rules(world, req, cr);

    constexpr float kDt = 1.0f / 60.0f;
    auto net_tick = [&]() {
        ProcessNetworkPackets(world, kDt, engine.memory().frame());
        UpdateStateReplicationSystem(world, kDt, engine.memory().frame(), world.frame_commands());
        world.frame_commands().playback(world);
    };

    world.begin_frame(1);
    KillEvent cheat_kill{};
    cheat_kill.killer_entity_id = p1.index();
    cheat_kill.victim_entity_id = p2.index();
    cheat_kill.kill_location    = float3{900.f, 0.f, 900.f};
    ProcessDeathmatchEventsSystem(world, &cheat_kill, 1, world.frame_commands());

    CheckpointTriggerEvent cheat_cp{};
    cheat_cp.player_entity_id  = p3.index();
    cheat_cp.checkpoint_index  = 2;
    cheat_cp.trigger_position  = float3{900.f, 0.f, 0.f};
    ProcessRaceEventsSystem(world, &cheat_cp, 1, world.frame_commands());

    PlayerReviveRequest cheat_rv{};
    cheat_rv.downed_player_entity_id   = p4.index();
    cheat_rv.reviving_player_entity_id = p3.index();
    world.get<PlayerStateComponent>(p3)->camera_position = float3{100.f, 0.f, 100.f};
    ProcessCoopMissionSystem(world, kDt, &cheat_rv, 1, world.frame_commands());

    world.begin_frame(2);
    KillEvent kill{};
    kill.killer_entity_id = p1.index();
    kill.victim_entity_id = p2.index();
    kill.weapon_used_id   = 9;
    kill.kill_location    = float3{0.f, 0.f, 10.f};
    ProcessDeathmatchEventsSystem(world, &kill, 1, world.frame_commands());

    CheckpointTriggerEvent cp{};
    cp.player_entity_id = p3.index();
    cp.checkpoint_index = 2;
    cp.trigger_position = float3{30.f, 0.f, 0.2f};
    ProcessRaceEventsSystem(world, &cp, 1, world.frame_commands());
    net_tick();

    world.get<PlayerStateComponent>(p3)->camera_position = float3{30.f, 0.f, 0.4f};
    PlayerReviveRequest rv{};
    rv.downed_player_entity_id   = p4.index();
    rv.reviving_player_entity_id = p3.index();
    bool revived = false;
    for (u32 f = 0; f < 200; ++f) {
        world.begin_frame(3 + f);
        ProcessCoopMissionSystem(world, kDt, &rv, 1, world.frame_commands());
        UpdateGameSessionSystem(world, kDt, world.frame_commands());
        net_tick();
        const PlayerStateComponent* p4s = world.get<PlayerStateComponent>(p4);
        const CoopDownedComponent* d = world.get<CoopDownedComponent>(p4);
        if (p4s && d && !d->is_downed && p4s->health >= 49.f) {
            revived = true;
            break;
        }
    }
    ENGINE_ASSERT(revived, "coop revive did not complete");

    const PlayerSessionStatsComponent* s1 = world.get<PlayerSessionStatsComponent>(p1);
    const PlayerSessionStatsComponent* s2 = world.get<PlayerSessionStatsComponent>(p2);
    ENGINE_ASSERT(s1 && s1->kills == 1 && s1->score == 1, "killer score");
    ENGINE_ASSERT(s2 && s2->deaths == 1, "victim deaths");

    const PlayerRaceStateComponent* rs3 = world.get<PlayerRaceStateComponent>(p3);
    ENGINE_ASSERT(rs3 && rs3->next_checkpoint_index == 3, "race checkpoint not advanced");

    u32 aoi_packets = 0;
    for (u32 i = 0; i < 4; ++i) {
        const ClientNetInboxComponent* inbox = world.get<ClientNetInboxComponent>(peers[i]);
        ENGINE_ASSERT(inbox != nullptr, "inbox");
        aoi_packets += inbox->received_count;
    }
    ENGINE_ASSERT(aoi_packets > 0, "no AOI replication");

    const MultiplayerTelemetryComponent* t = nullptr;
    for (Entity e : world.query<MultiplayerTelemetryComponent>()) {
        t = world.get<MultiplayerTelemetryComponent>(e);
        break;
    }
    ENGINE_ASSERT(t != nullptr, "telemetry");
    ENGINE_ASSERT(t->sessions_active >= 2, "sessions");
    ENGINE_ASSERT(t->kills_validated >= 1, "kills");
    ENGINE_ASSERT(t->checkpoints_validated >= 1, "checkpoints");
    ENGINE_ASSERT(t->revives_completed >= 1, "revives");
    ENGINE_ASSERT(t->cheats_rejected >= 3, "cheats");

    world.begin_frame(300);
    AOIQueryResult aoi = CalculateAreaOfInterest(world, ep_c[0], 1, engine.memory().frame());
    ENGINE_ASSERT(aoi.count > 0, "AOI empty");

    std::printf("MICRO-PHASE 15 SANDBOX PASSED\n");
    std::printf("  Game sessions active            : %u\n", t->sessions_active);
    std::printf("  Deathmatch kills validated      : %u\n", t->kills_validated);
    std::printf("  Race checkpoints validated      : %u\n", t->checkpoints_validated);
    std::printf("  Co-op revives completed         : %u\n", t->revives_completed);
    std::printf("  Invalid/Cheat events rejected   : %u\n", t->cheats_rejected);
    std::printf("  Network packets replicated via AOI: %u\n", aoi_packets);
    engine.shutdown();
    return 0;
}
