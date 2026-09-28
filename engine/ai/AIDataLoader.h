#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ai/GangTerritoryAI.h"
#include "ai/CivilianRoutineAI.h"

namespace engine {

class World;

struct AIDataFileHeader {
    char magic[8];
    u32  version;
    u32  territory_count;
    u32  schedule_count;
    u32  patrol_route_count;
};

struct GangPatrolRouteComponent {
    u32    route_id;
    u32    waypoint_count;
    float3 waypoints[8];
};

struct AdvancedAITelemetryComponent {
    u32 police_squads_spawned;
    u32 officers_flanking;
    u32 k9_tracking;
    u32 helicopters_pursuing;
    u32 territories_controlled;
    u32 turf_wars_active;
    u32 civilians_on_schedule;
    u32 civilians_witnessed_crime;
    u32 civilians_called_police;
    u32 danger_zones_avoided;
};

bool WriteTestCityAI(const char* filepath);
bool LoadAIDataFromFile(World& world, const char* filepath, CommandBuffer& cmd);

} // namespace engine
