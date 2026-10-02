#include "ai/AIDataLoader.h"

#include "ai/AdvancedNavigation.h"
#include "ai/PoliceTacticalAI.h"
#include "core/Assert.h"
#include "ecs/World.h"

#include <cstdio>
#include <cstring>

namespace engine {

namespace {

void copy_text(char* dst, u32 cap, const char* src) {
    if (cap == 0) {
        return;
    }
    u32 i = 0;
    if (src) {
        for (; src[i] != '\0' && i + 1 < cap; ++i) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
    for (u32 z = i + 1; z < cap; ++z) {
        dst[z] = '\0';
    }
}

void fill_activity(ScheduleDefinition::Activity* a, float start, float end, float3 loc, u8 type) {
    a->start_hour    = start;
    a->end_hour      = end;
    a->location      = loc;
    a->activity_type = type;
}

void make_route(GangPatrolRouteComponent* r, u32 id, float3 c, float rad) {
    r->route_id        = id;
    r->waypoint_count  = 4;
    r->waypoints[0]    = float3{c.x + rad, c.y, c.z};
    r->waypoints[1]    = float3{c.x, c.y, c.z + rad};
    r->waypoints[2]    = float3{c.x - rad, c.y, c.z};
    r->waypoints[3]    = float3{c.x, c.y, c.z - rad};
}

} // namespace

bool WriteTestCityAI(const char* filepath) {
    ENGINE_ASSERT(filepath != nullptr, "ai path");
    FILE* f = std::fopen(filepath, "wb");
    if (!f) {
        return false;
    }

    AIDataFileHeader header{};
    std::memcpy(header.magic, "LEONAIDA", 8);
    header.version            = 1;
    header.territory_count    = 3;
    header.schedule_count     = 5;
    header.patrol_route_count = 10;

    GangTerritoryComponent territories[3]{};
    territories[0].territory_id        = 1;
    territories[0].center_position     = float3{0.f, 0.f, 0.f};
    territories[0].radius_m            = 40.f;
    territories[0].controlling_gang_id = kGangBallas;
    territories[0].influence_level     = 0.8f;
    territories[0].last_contested_time = 0;

    territories[1].territory_id        = 2;
    territories[1].center_position     = float3{80.f, 0.f, 0.f};
    territories[1].radius_m            = 40.f;
    territories[1].controlling_gang_id = kGangGrove;
    territories[1].influence_level     = 0.75f;

    territories[2].territory_id        = 3;
    territories[2].center_position     = float3{40.f, 0.f, 80.f};
    territories[2].radius_m            = 40.f;
    territories[2].controlling_gang_id = kGangVagos;
    territories[2].influence_level     = 0.7f;

    ScheduleDefinition schedules[5]{};
    schedules[0].schedule_id = 1;
    copy_text(schedules[0].name, 32, "Office Worker");
    schedules[0].activity_count = 4;
    fill_activity(&schedules[0].activities[0], 8.f, 17.f, float3{20.f, 0.f, 10.f}, 0);
    fill_activity(&schedules[0].activities[1], 17.f, 19.f, float3{24.f, 0.f, 14.f}, 1);
    fill_activity(&schedules[0].activities[2], 19.f, 23.f, float3{18.f, 0.f, 16.f}, 4);
    fill_activity(&schedules[0].activities[3], 23.f, 8.f, float3{12.f, 0.f, 8.f}, 3);

    schedules[1].schedule_id = 2;
    copy_text(schedules[1].name, 32, "Student");
    schedules[1].activity_count = 4;
    fill_activity(&schedules[1].activities[0], 9.f, 15.f, float3{30.f, 0.f, 20.f}, 0);
    fill_activity(&schedules[1].activities[1], 15.f, 18.f, float3{32.f, 0.f, 22.f}, 4);
    fill_activity(&schedules[1].activities[2], 18.f, 20.f, float3{28.f, 0.f, 18.f}, 1);
    fill_activity(&schedules[1].activities[3], 20.f, 9.f, float3{26.f, 0.f, 12.f}, 3);

    schedules[2].schedule_id = 3;
    copy_text(schedules[2].name, 32, "Tourist");
    schedules[2].activity_count = 3;
    fill_activity(&schedules[2].activities[0], 10.f, 18.f, float3{8.f, 0.f, 28.f}, 2);
    fill_activity(&schedules[2].activities[1], 18.f, 22.f, float3{10.f, 0.f, 26.f}, 1);
    fill_activity(&schedules[2].activities[2], 22.f, 10.f, float3{6.f, 0.f, 24.f}, 3);

    schedules[3].schedule_id = 4;
    copy_text(schedules[3].name, 32, "Homeless");
    schedules[3].activity_count = 2;
    fill_activity(&schedules[3].activities[0], 6.f, 22.f, float3{-8.f, 0.f, 6.f}, 4);
    fill_activity(&schedules[3].activities[1], 22.f, 6.f, float3{-10.f, 0.f, 4.f}, 3);

    schedules[4].schedule_id = 5;
    copy_text(schedules[4].name, 32, "Jogger");
    schedules[4].activity_count = 4;
    fill_activity(&schedules[4].activities[0], 6.f, 8.f, float3{-4.f, 0.f, 30.f}, 4);
    fill_activity(&schedules[4].activities[1], 8.f, 18.f, float3{16.f, 0.f, 6.f}, 0);
    fill_activity(&schedules[4].activities[2], 18.f, 20.f, float3{14.f, 0.f, 8.f}, 1);
    fill_activity(&schedules[4].activities[3], 20.f, 6.f, float3{12.f, 0.f, 4.f}, 3);

    GangPatrolRouteComponent routes[10]{};
    make_route(&routes[0], 1, float3{0.f, 0.f, 0.f}, 12.f);
    make_route(&routes[1], 2, float3{0.f, 0.f, 0.f}, 18.f);
    make_route(&routes[2], 3, float3{0.f, 0.f, 0.f}, 24.f);
    make_route(&routes[3], 4, float3{80.f, 0.f, 0.f}, 12.f);
    make_route(&routes[4], 5, float3{80.f, 0.f, 0.f}, 18.f);
    make_route(&routes[5], 6, float3{80.f, 0.f, 0.f}, 24.f);
    make_route(&routes[6], 7, float3{40.f, 0.f, 80.f}, 12.f);
    make_route(&routes[7], 8, float3{40.f, 0.f, 80.f}, 18.f);
    make_route(&routes[8], 9, float3{40.f, 0.f, 80.f}, 24.f);
    make_route(&routes[9], 10, float3{40.f, 0.f, 40.f}, 16.f);

    const bool ok = std::fwrite(&header, sizeof(header), 1, f) == 1
                 && std::fwrite(territories, sizeof(territories[0]), 3, f) == 3
                 && std::fwrite(schedules, sizeof(schedules[0]), 5, f) == 5
                 && std::fwrite(routes, sizeof(routes[0]), 10, f) == 10;
    std::fclose(f);
    return ok;
}

bool LoadAIDataFromFile(World& world, const char* filepath, CommandBuffer& cmd) {
    (void)cmd;
    ENGINE_ASSERT(filepath != nullptr, "ai path");
    FILE* f = std::fopen(filepath, "rb");
    if (!f) {
        return false;
    }
    AIDataFileHeader header{};
    if (std::fread(&header, sizeof(header), 1, f) != 1) {
        std::fclose(f);
        return false;
    }
    if (std::memcmp(header.magic, "LEONAIDA", 8) != 0 || header.version != 1) {
        std::fclose(f);
        return false;
    }

    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "ai_data";

    for (u32 i = 0; i < header.territory_count; ++i) {
        GangTerritoryComponent t{};
        if (std::fread(&t, sizeof(t), 1, f) != 1) {
            std::fclose(f);
            return false;
        }
        (void)instantiate_gang_territory(world, req, t);
    }
    for (u32 i = 0; i < header.schedule_count; ++i) {
        ScheduleDefinition s{};
        if (std::fread(&s, sizeof(s), 1, f) != 1) {
            std::fclose(f);
            return false;
        }
        (void)instantiate_schedule_definition(world, req, s);
    }
    u32 node_id = 1;
    for (u32 i = 0; i < header.patrol_route_count; ++i) {
        GangPatrolRouteComponent r{};
        if (std::fread(&r, sizeof(r), 1, f) != 1) {
            std::fclose(f);
            return false;
        }
        (void)world.instantiate(req, r);
        for (u32 w = 0; w < r.waypoint_count && w < 8; ++w) {
            StreetPathNodeComponent n{};
            n.node_id         = node_id++;
            n.position_ws     = r.waypoints[w];
            n.neighbor_count  = 1;
            n.neighbor_ids[0] = node_id; // next (best-effort)
            (void)world.instantiate(req, n);
        }
    }
    std::fclose(f);

    CityClockComponent clock{};
    clock.world_time_s = 0.f;
    clock.hour_of_day  = 9.0f;
    clock.time_scale   = 60.0f;
    (void)world.instantiate(req, clock);

    PlayerScentTrailComponent trail{};
    (void)world.instantiate(req, trail);

    AdvancedAITelemetryComponent tel{};
    (void)world.instantiate(req, tel);
    return true;
}

} // namespace engine
