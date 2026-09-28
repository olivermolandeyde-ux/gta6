#pragma once

#include "core/Types.h"

namespace engine {

// Pure mathematical solver for limb placement on uneven terrain.
// No heap allocations. Operates entirely on stack or frame arena.
struct BoneChain {
    float3 root_pos;      // e.g., Hip/Pelvis
    float3 joint_pos;     // e.g., Knee
    float3 end_effector;  // e.g., Ankle
    float  bone1_length;  // Thigh length
    float  bone2_length;  // Calf length
};

struct IKSolution {
    float3 new_joint_pos;
    float3 new_end_effector_pos;
    float  bend_direction_weight; // For knee/elbow polarity
};

// Solves Two-Bone IK using Law of Cosines.
// Returns true if target is reachable, false if out of reach (clamps to max extension).
bool SolveTwoBoneIK(const BoneChain& chain, const float3& target_pos, float3 bend_axis,
                    IKSolution& out_solution);

[[nodiscard]] float two_bone_joint_angle_rad(const IKSolution& solution, const float3& root_pos);

} // namespace engine
