#include "gameplay/VehicleInteraction.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"
#include "physics/VehicleDynamics.h"

namespace engine {

Entity instantiate_vehicle_interaction(World& world, const InstantiationRequest& request,
                                       const VehicleInteractionComponent& ix) {
    validate_instantiation(request);
    return world.instantiate(request, ix);
}

void request_vehicle_enter(World& world, Entity interaction) {
    VehicleInteractionComponent* ix = world.get<VehicleInteractionComponent>(interaction);
    ENGINE_ASSERT(ix != nullptr, "interaction missing");
    if (ix->is_entering || ix->is_exiting) {
        return;
    }
    ix->is_entering = true;
    ix->is_exiting = false;
    ix->enter_exit_timer_s = ix->enter_exit_duration_s;
}

void request_vehicle_exit(World& world, Entity interaction) {
    VehicleInteractionComponent* ix = world.get<VehicleInteractionComponent>(interaction);
    ENGINE_ASSERT(ix != nullptr, "interaction missing");
    if (ix->camera_handle == 0 && !ix->is_entering) {
        return;
    }
    ix->is_exiting = true;
    ix->is_entering = false;
    ix->enter_exit_timer_s = ix->enter_exit_duration_s;
}

void UpdateVehicleInteractionSystem(World& world, float delta_time, CommandBuffer& cmd) {
    (void)cmd;
    for (Entity e : world.query<VehicleInteractionComponent>()) {
        VehicleInteractionComponent* ix = world.get<VehicleInteractionComponent>(e);
        ENGINE_ASSERT(ix != nullptr, "ix stale");
        if (!ix->is_entering && !ix->is_exiting) {
            continue;
        }
        ix->enter_exit_timer_s -= delta_time;
        if (ix->enter_exit_timer_s > 0.f) {
            continue;
        }
        ix->enter_exit_timer_s = 0.f;

        Entity player = kNullEntity;
        Entity chassis = kNullEntity;
        for (Entity p : world.query<PlayerStateComponent>()) {
            if (p.index() == ix->player_entity_id) {
                player = p;
                break;
            }
        }
        for (Entity c : world.query<VehicleChassisComponent, VehicleChassisPose>()) {
            if (c.index() == ix->vehicle_chassis_entity_id) {
                chassis = c;
                break;
            }
        }

        if (ix->is_entering) {
            ix->is_entering = false;
            ix->camera_handle = 1; // interior camera
            if (world.is_alive(player)) {
                PlayerStateComponent* ps = world.get<PlayerStateComponent>(player);
                if (ps) {
                    ps->current_vehicle_entity_id = ix->vehicle_chassis_entity_id;
                    if (world.is_alive(chassis)) {
                        const VehicleChassisPose* pose = world.get<VehicleChassisPose>(chassis);
                        if (pose) {
                            ps->camera_position = float3_add(pose->position_ws, float3{0.f, 1.1f, 0.2f});
                        }
                    }
                }
            }
        } else if (ix->is_exiting) {
            ix->is_exiting = false;
            ix->camera_handle = 0; // exterior / on-foot
            if (world.is_alive(player)) {
                PlayerStateComponent* ps = world.get<PlayerStateComponent>(player);
                if (ps) {
                    ps->current_vehicle_entity_id = 0;
                    ps->camera_position = ix->entry_point_world;
                    ps->camera_position.y += 1.6f;
                }
            }
        }
    }
}

} // namespace engine
