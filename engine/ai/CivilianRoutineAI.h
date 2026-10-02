#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

struct CivilianScheduleComponent {
    u32    civilian_entity_id;
    u32    schedule_id;
    u32    current_activity_index;
    float  activity_start_time;
    float3 current_destination;
    bool   is_at_destination;
};

struct ScheduleDefinition {
    u32  schedule_id;
    char name[32];
    struct Activity {
        float  start_hour;
        float  end_hour;
        float3 location;
        u8     activity_type; // 0=work, 1=eat, 2=shop, 3=sleep, 4=socialize
    } activities[8];
    u32 activity_count;
};

struct CivilianMemoryComponent {
    u32 civilian_entity_id;
    struct Memory {
        float  timestamp;
        u32    event_type; // 0=saw_crime, 1=witnessed_shooting, 2=was_threatened
        float3 event_location;
        u32    perpetrator_entity_id;
        float  severity;
    } memories[16];
    u32   memory_count;
    float trust_in_authority;
};

struct CivilianRelationshipComponent {
    u32   civilian_entity_id;
    float reputation_with_player;
    bool  knows_player_identity;
    bool  is_hostile;
};

struct CivilianPoseComponent {
    float3 position_ws;
};

struct CityClockComponent {
    float world_time_s;
    float hour_of_day;
    float time_scale;
};

struct StreetCrimeStimulusComponent {
    float3 position_ws;
    u32    perpetrator_entity_id;
    u32    event_type;
    float  severity;
};

struct CivilianCalledPoliceTag {
    u32 civilian_entity_id;
    u32 dispatch_frame;
};

[[nodiscard]] Entity instantiate_schedule_definition(World& world, const InstantiationRequest& request,
                                                     const ScheduleDefinition& def);

[[nodiscard]] Entity instantiate_civilian_routine(World& world, const InstantiationRequest& request,
                                                  const CivilianScheduleComponent& schedule,
                                                  const CivilianMemoryComponent& memory,
                                                  const CivilianRelationshipComponent& rel,
                                                  float3 position_ws);

void UpdateCivilianRoutineAISystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                                   CommandBuffer& cmd);

} // namespace engine
