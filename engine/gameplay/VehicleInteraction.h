#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

struct VehicleInteractionComponent {
    u32    player_entity_id;
    u32    vehicle_chassis_entity_id;
    u32    seat_index;
    bool   is_entering;
    bool   is_exiting;
    float  enter_exit_timer_s;
    float  enter_exit_duration_s;
    float3 entry_point_world;
    float3 seat_position_local;
    u32    camera_handle;
};

[[nodiscard]] Entity instantiate_vehicle_interaction(World& world,
                                                     const InstantiationRequest& request,
                                                     const VehicleInteractionComponent& ix);

void request_vehicle_enter(World& world, Entity interaction);
void request_vehicle_exit(World& world, Entity interaction);

void UpdateVehicleInteractionSystem(World& world, float delta_time, CommandBuffer& cmd);

} // namespace engine
