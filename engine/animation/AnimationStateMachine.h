#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

// Component linking AI state to animation blends and IK targets.
struct AnimationIKComponent {
    u32    skeleton_handle;
    float3 left_foot_target_world;
    float3 right_foot_target_world;
    float  left_foot_ik_weight;  // 0.0 to 1.0 (blends between animated foot and IK foot)
    float  right_foot_ik_weight;
};

// Rest pose of one civilian skeleton. Not a generic Animator.
struct PedestrianLimbRestComponent {
    float  thigh_length_m;
    float  calf_length_m;
    float  pelvis_height_m;
    float  hip_width_m;
    float3 animated_left_foot_os;  // Object-space animated foot (before IK)
    float3 animated_right_foot_os;
};

struct PedestrianIkResultComponent {
    float3 left_knee_ws;
    float3 right_knee_ws;
    float  left_joint_angle_rad;
    float  right_joint_angle_rad;
    float  left_reachable;
    float  right_reachable;
};

struct PedestrianGroundSample {
    float  height_y;
    float3 normal_ws;
};

// Heightfield callback stored as a function pointer (no vtable Character class).
using GroundHeightFn = PedestrianGroundSample (*)(float3 world_xz);

void set_pedestrian_ground_sampler(GroundHeightFn fn);

void UpdateAnimationIKSystem(World& world, float delta_time, FrameAllocator& frame_alloc);

[[nodiscard]] Entity attach_pedestrian_ik(World& world, Entity pedestrian, u32 skeleton_handle,
                                          const PedestrianLimbRestComponent& rest);

} // namespace engine
