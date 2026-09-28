#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

// Internal-combustion lumped-thermal model. Not a "Vehicle" subclass.
// Powertrain chassis / suspension arrive in MICRO-PHASE 4 as their own types.
struct VehicleEngineComponent {
    u32   entity_id;                    // Entity.index() back-reference (handle, not pointer)
    float temperature_c;                // Block temperature, Celsius
    float wear_factor;                  // 0 = new, 1 = seized
    float current_rpm;
    float throttle_input;               // 0..1
    u32   exhaust_particle_emitter_id;  // Handle into the ICE exhaust table
};

// ICE-specific exhaust plume. Not a generic VFX component.
struct VehicleExhaustPlumeComponent {
    u32   emitter_id;
    float soot_rate;        // particles / second
    float gas_temperature_c;
    float opacity;          // 0..1
};

struct VehicleEngineSpawnDesc {
    float idle_rpm                    = 850.f;
    float throttle                    = 0.f;
    float wear                        = 0.f;
    float temperature_c               = 25.f;
    u32   exhaust_particle_emitter_id = 0;
};

[[nodiscard]] Entity instantiate_vehicle_engine(World& world,
                                                const InstantiationRequest& request,
                                                const VehicleEngineSpawnDesc& desc);

void UpdateVehicleEngineSystem(World& world, float delta_time, FrameAllocator& frame_alloc);

} // namespace engine
