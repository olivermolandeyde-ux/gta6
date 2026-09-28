#include "Engine.h"
#include "ai/AIDataLoader.h"
#include "ai/AdvancedNavigation.h"
#include "ai/CivilianRoutineAI.h"
#include "ai/GangTerritoryAI.h"
#include "ai/PoliceTacticalAI.h"
#include "gameplay/PlayerController.h"

#include <cstdio>

#ifndef LEONIDA_SOURCE_DIR
#define LEONIDA_SOURCE_DIR "."
#endif

namespace {

void tick_ai(engine::Engine& engine, float dt, const engine::TurfWarEvent* wars, engine::u32 n_wars) {
    engine::World& world = engine.world();
    world.begin_frame(world.frame_index() + 1);
    engine::UpdatePoliceTacticalAISystem(world, dt, engine.memory().frame(), world.frame_commands());
    engine::UpdateGangTerritoryAISystem(world, dt, wars, n_wars, world.frame_commands());
    engine::UpdateCivilianRoutineAISystem(world, dt, engine.memory().frame(), world.frame_commands());
    engine::UpdateAdvancedNavigationSystem(world, dt, engine.memory().frame());
    world.frame_commands().playback(world);
}

void copy_route(engine::GangMemberComponent* m, const engine::GangPatrolRouteComponent& r) {
    m->patrol_route_count = r.waypoint_count;
    for (engine::u32 i = 0; i < r.waypoint_count && i < 8; ++i) {
        m->patrol_route[i] = r.waypoints[i];
    }
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
    engine.boot(budget, 1);
    World& world = engine.world();

    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "phase13";

    PlayerStateComponent player{};
    player.health          = 100.f;
    player.max_health      = 100.f;
    player.stamina         = 100.f;
    player.max_stamina     = 100.f;
    player.camera_position = float3{0.f, 1.6f, 0.f};
    player.camera_forward  = float3{0.f, 0.f, -1.f};
    player.move_speed_penalty = 1.f;
    Entity player_e = instantiate_player(world, req, player);

    const char* ai_path = LEONIDA_SOURCE_DIR "/data/ai/test_city.ai";
    ENGINE_ASSERT(WriteTestCityAI(ai_path), "write test_city.ai");
    ENGINE_ASSERT(LoadAIDataFromFile(world, ai_path, world.frame_commands()), "load test_city.ai");

    TacticalCoverSlabComponent cover{};
    cover.min_ws = float3{-2.f, 0.f, -10.f};
    cover.max_ws = float3{2.f, 2.0f, -8.f};
    (void)world.instantiate(req, cover);

    PoliceSquadComponent squad{};
    squad.squad_id                = 7;
    squad.officer_count           = 4;
    squad.target_player_entity_id = player_e.index();
    squad.squad_state             = 0;
    squad.formation_center        = float3{0.f, 0.f, -20.f};
    squad.squad_cohesion          = 0.5f;
    Entity squad_e = instantiate_police_squad(world, req, squad);

    const u8 roles[4] = {0, 1, 2, 2};
    const float3 off_pos[4] = {
        float3{0.f, 0.f, -20.f},
        float3{-3.f, 0.f, -20.f},
        float3{3.f, 0.f, -20.f},
        float3{6.f, 0.f, -20.f},
    };
    PoliceSquadComponent* squad_live = world.get<PoliceSquadComponent>(squad_e);
    for (u32 i = 0; i < 4; ++i) {
        PoliceOfficerComponent off{};
        off.squad_id                 = 7;
        off.role                     = roles[i];
        off.aggression_level         = 0.3f;
        off.accuracy_modifier        = 0.8f;
        off.current_target_entity_id = player_e.index();
        Entity oe = instantiate_police_officer(world, req, off, off_pos[i]);
        PoliceOfficerComponent* live = world.get<PoliceOfficerComponent>(oe);
        ENGINE_ASSERT(live != nullptr, "officer");
        live->officer_entity_id = oe.index();
        squad_live = world.get<PoliceSquadComponent>(squad_e);
        squad_live->officer_ids[i] = oe.index();
    }
    squad_live->officer_count = 4;

    K9UnitComponent k9{};
    k9.handler_entity_id       = squad_live->officer_ids[0];
    k9.target_player_entity_id = player_e.index();
    k9.speed_multiplier        = 1.4f;
    k9.scent_strength          = 0.f;
    Entity k9e = instantiate_k9_unit(world, req, k9, float3{-2.f, 0.f, -18.f});
    world.get<K9UnitComponent>(k9e)->dog_entity_id = k9e.index();

    PoliceHelicopterComponent heli{};
    heli.target_player_entity_id = player_e.index();
    heli.altitude_m              = 75.f;
    heli.spotlight_intensity     = 0.f;
    Entity heli_e = instantiate_police_helicopter(world, req, heli, float3{0.f, 75.f, -40.f});
    world.get<PoliceHelicopterComponent>(heli_e)->helicopter_entity_id = heli_e.index();

    GangPatrolRouteComponent routes[10]{};
    u32 n_routes = 0;
    for (Entity e : world.query<GangPatrolRouteComponent>()) {
        const GangPatrolRouteComponent* r = world.get<GangPatrolRouteComponent>(e);
        if (r && n_routes < 10) {
            routes[n_routes++] = *r;
        }
    }
    ENGINE_ASSERT(n_routes == 10, "expected 10 patrol routes");

    auto spawn_member = [&](u32 gang, u32 territory, u32 route_i, float3 pos) {
        GangMemberComponent m{};
        m.gang_id                   = gang;
        m.home_territory_id         = territory;
        m.aggression_toward_player  = 0.1f;
        m.is_patrolling             = true;
        if (route_i < n_routes) {
            copy_route(&m, routes[route_i]);
        }
        Entity ge = instantiate_gang_member(world, req, m, pos);
        world.get<GangMemberComponent>(ge)->member_entity_id = ge.index();
        return ge;
    };
    spawn_member(kGangBallas, 1, 0, float3{2.f, 0.f, 2.f});
    spawn_member(kGangBallas, 1, 1, float3{-2.f, 0.f, 2.f});
    spawn_member(kGangGrove, 2, 3, float3{4.f, 0.f, -2.f});
    spawn_member(kGangGrove, 2, 4, float3{6.f, 0.f, 0.f});
    spawn_member(kGangGrove, 2, 5, float3{5.f, 0.f, 3.f});
    spawn_member(kGangVagos, 3, 6, float3{40.f, 0.f, 80.f});

    StreetCrimeStimulusComponent crime{};
    crime.position_ws           = float3{20.f, 0.f, 10.f};
    crime.perpetrator_entity_id = player_e.index();
    crime.event_type            = 1;
    crime.severity              = 0.92f;
    (void)world.instantiate(req, crime);

    DangerZoneComponent danger{};
    danger.zone_id         = 1;
    danger.center_position = float3{20.f, 0.f, 10.f};
    danger.radius_m        = 16.f;
    danger.danger_type     = 0;
    danger.severity        = 0.9f;
    danger.duration_s      = 0.f;
    (void)instantiate_danger_zone(world, req, danger);

    const u32 sched_ids[10] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5};
    const float3 civ_pos[10] = {
        float3{19.f, 0.f, 10.f},  float3{21.f, 0.f, 11.f}, float3{29.f, 0.f, 20.f},
        float3{31.f, 0.f, 19.f},  float3{8.f, 0.f, 27.f},  float3{9.f, 0.f, 28.f},
        float3{-8.f, 0.f, 6.f},   float3{-9.f, 0.f, 5.f},  float3{16.f, 0.f, 6.f},
        float3{15.f, 0.f, 7.f},
    };
    for (u32 i = 0; i < 10; ++i) {
        CivilianScheduleComponent sch{};
        sch.schedule_id             = sched_ids[i];
        sch.current_activity_index  = 0;
        sch.current_destination     = civ_pos[i];
        CivilianMemoryComponent mem{};
        mem.trust_in_authority = 0.8f;
        CivilianRelationshipComponent rel{};
        rel.reputation_with_player = 0.f;
        Entity civ = instantiate_civilian_routine(world, req, sch, mem, rel, civ_pos[i]);
        world.get<CivilianScheduleComponent>(civ)->civilian_entity_id = civ.index();
        world.get<CivilianMemoryComponent>(civ)->civilian_entity_id   = civ.index();
        world.get<CivilianRelationshipComponent>(civ)->civilian_entity_id = civ.index();
    }

    constexpr float kDt = 1.0f / 60.0f;
    TurfWarEvent war{};
    war.territory_id     = 1;
    war.attacker_gang_id = kGangGrove;
    war.defender_gang_id = kGangBallas;
    war.battle_center    = float3{0.f, 0.f, 0.f};
    war.battle_radius_m  = 50.f;

    tick_ai(engine, kDt, &war, 1);
    for (u32 f = 0; f < 180; ++f) {
        tick_ai(engine, kDt, nullptr, 0);
    }

    const AdvancedAITelemetryComponent* tel = nullptr;
    for (Entity e : world.query<AdvancedAITelemetryComponent>()) {
        tel = world.get<AdvancedAITelemetryComponent>(e);
        break;
    }
    ENGINE_ASSERT(tel != nullptr, "telemetry");
    ENGINE_ASSERT(tel->police_squads_spawned >= 1, "squads");
    ENGINE_ASSERT(tel->officers_flanking >= 1, "flanking");
    ENGINE_ASSERT(tel->k9_tracking >= 1, "k9");
    ENGINE_ASSERT(tel->helicopters_pursuing >= 1, "heli");
    ENGINE_ASSERT(tel->territories_controlled >= 3, "territories");
    ENGINE_ASSERT(tel->turf_wars_active >= 1, "turf war");
    ENGINE_ASSERT(tel->civilians_on_schedule >= 10, "schedules");
    ENGINE_ASSERT(tel->civilians_witnessed_crime >= 1, "witness");
    ENGINE_ASSERT(tel->civilians_called_police >= 1, "police call");
    ENGINE_ASSERT(tel->danger_zones_avoided >= 1, "danger avoid");

    const PoliceSquadComponent* sl = world.get<PoliceSquadComponent>(squad_e);
    ENGINE_ASSERT(sl && sl->squad_state == 2, "squad not flanking");
    const K9UnitComponent* k9l = world.get<K9UnitComponent>(k9e);
    ENGINE_ASSERT(k9l && k9l->is_tracking, "k9 not tracking");
    const PoliceHelicopterComponent* hl = world.get<PoliceHelicopterComponent>(heli_e);
    ENGINE_ASSERT(hl && hl->has_visual_on_player, "heli lost visual");

    std::printf("MICRO-PHASE 13 SANDBOX PASSED\n");
    std::printf("  Police squads spawned         : %u\n", tel->police_squads_spawned);
    std::printf("  Officers flanking             : %u\n", tel->officers_flanking);
    std::printf("  K9 units tracking             : %u\n", tel->k9_tracking);
    std::printf("  Helicopters pursuing          : %u\n", tel->helicopters_pursuing);
    std::printf("  Gang territories controlled   : %u\n", tel->territories_controlled);
    std::printf("  Turf wars active              : %u\n", tel->turf_wars_active);
    std::printf("  Civilians following schedules : %u\n", tel->civilians_on_schedule);
    std::printf("  Civilians who witnessed crime : %u\n", tel->civilians_witnessed_crime);
    std::printf("  Civilians who called police   : %u\n", tel->civilians_called_police);
    std::printf("  Danger zones avoided          : %u\n", tel->danger_zones_avoided);
    engine.shutdown();
    return 0;
}
