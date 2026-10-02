#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

inline constexpr float kGravityMs2 = 9.80665f;

// Rigid body of a specific automobile chassis. Not a generic Vehicle / RigidBody.
struct VehicleChassisComponent {
    float    mass_kg;
    float3   inertia_tensor;         // Principal moments of inertia (local)
    float3   center_of_mass_offset;  // Local space
    float3   velocity;               // World space linear velocity
    float3   angular_velocity;       // World space angular velocity
    float3x3 rotation_matrix;
};

struct VehicleChassisPose {
    float3 position_ws;
};

struct VehicleSteeringComponent {
    float wheelbase_m;
    float track_width_m;
    float max_steer_rad;
    float bicycle_steer_rad;     // Virtual bicycle input, radians
    float ackermann_inner_rad;
    float ackermann_outer_rad;
};

struct VehicleExternalLoadComponent {
    float3 force_ws;   // Extra world-space force (test rig, aero, ...)
    float3 torque_ws;
};

struct WheelNodeComponent {
    u32    chassis_entity_id;
    float3 local_attachment_point; // Suspension mount point relative to chassis COM
    float  suspension_rest_length_m;
    float  suspension_travel_m;
    float  spring_constant_n_m;    // k
    float  damping_ratio;          // zeta
    float  current_deflection_m;   // Dynamic state (compression, metres)
    float  steer_angle_rad;        // Ackermann steering input
    float  angular_velocity_rad_s; // Wheel rotation speed
    float  slip_ratio;             // Longitudinal slip (kappa)
    float  slip_angle_rad;         // Lateral slip (alpha)
};

struct VehicleTireContactComponent {
    float  radius_m;
    float  friction_mu;
    float  rolling_resistance;
    float3 contact_ws;
    float  normal_force_n;
    u8     in_contact;
    u8     is_front;
    u8     is_right;
    u8     _pad;
};

struct VehicleWheelAirborneTag {
    u32 frame;
};

struct VehicleDynamicsSpawnDesc {
    float  mass_kg          = 1460.f;
    float3 inertia_tensor   = {520.f, 1860.f, 1640.f};
    float3 com_offset       = {0.f, -0.12f, 0.06f};
    float3 position_ws      = {0.f, 0.62f, 0.f};
    float  wheelbase_m      = 2.70f;
    float  track_width_m    = 1.58f;
    float  spring_k         = 52000.f;
    float  damping_ratio    = 0.42f;
    float  rest_length_m    = 0.30f;
    float  travel_m         = 0.16f;
    float  tire_radius_m    = 0.32f;
    float  friction_mu      = 1.15f;
    float  extra_load_g     = 0.f; // added to gravity; 1.0 => 2G total vertical load
};

struct VehicleDynamicsSpawnResult {
    Entity chassis;
    Entity wheel_fl;
    Entity wheel_fr;
    Entity wheel_rl;
    Entity wheel_rr;
};

[[nodiscard]] Entity instantiate_vehicle_chassis(World& world,
                                                 const InstantiationRequest& request,
                                                 const VehicleDynamicsSpawnDesc& desc,
                                                 VehicleDynamicsSpawnResult* out_wheels);

void UpdateVehicleDynamicsSystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                                 CommandBuffer& cmd);

} // namespace engine
