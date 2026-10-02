#include "animation/TwoBoneIK.h"

#include "core/Assert.h"

#include <cmath>

namespace engine {

bool SolveTwoBoneIK(const BoneChain& chain, const float3& target_pos, float3 bend_axis,
                    IKSolution& out_solution) {
    ENGINE_ASSERT(chain.bone1_length > 1.0e-4f && chain.bone2_length > 1.0e-4f, "degenerate limb");

    const float L1 = chain.bone1_length;
    const float L2 = chain.bone2_length;
    const float3 to_target = float3_sub(target_pos, chain.root_pos);
    float D = float3_length(to_target);

    const float max_reach = L1 + L2;
    const float min_reach = std::fabs(L1 - L2);
    const bool reachable  = (D <= max_reach + 1.0e-4f) && (D >= min_reach - 1.0e-4f);

    // Clamp D so acos stays in-domain when the target is slightly out of reach.
    const float D_clamped = clampf(D, min_reach + 1.0e-4f, max_reach - 1.0e-4f);

    float3 dir = float3_normalize_or(to_target, float3{0.f, -1.f, 0.f});
    if (D < 1.0e-6f) {
        dir = float3{0.f, -1.f, 0.f};
    }

    // Law of Cosines at the joint:
    //   cos(theta2) = (D^2 - L1^2 - L2^2) / (2 * L1 * L2)
    // theta2 is the exterior (hinge) angle; interior knee = pi - theta2.
    float cos_theta2 = (D_clamped * D_clamped - L1 * L1 - L2 * L2) / (2.0f * L1 * L2);
    cos_theta2 = clampf(cos_theta2, -1.0f, 1.0f);
    const float theta2 = std::acos(cos_theta2);

    float cos_theta1 = (L1 * L1 + D_clamped * D_clamped - L2 * L2) / (2.0f * L1 * D_clamped);
    cos_theta1 = clampf(cos_theta1, -1.0f, 1.0f);
    const float theta1 = std::acos(cos_theta1);

    float3 pole = float3_normalize_or(bend_axis, float3{0.f, 0.f, 1.f});
    // Project pole onto the plane perpendicular to dir so the knee cannot fold along the shin.
    pole = float3_sub(pole, float3_scale(dir, float3_dot(pole, dir)));
    pole = float3_normalize_or(pole, float3_normalize_or(float3_cross(dir, float3{0.f, 1.f, 0.f}),
                                                         float3{1.f, 0.f, 0.f}));

    const float3 bone1_dir =
        float3_normalize_or(float3_add(float3_scale(dir, std::cos(theta1)),
                                       float3_scale(pole, std::sin(theta1))),
                            dir);

    out_solution.new_joint_pos         = float3_add(chain.root_pos, float3_scale(bone1_dir, L1));
    out_solution.new_end_effector_pos  = reachable ? target_pos
         : float3_add(chain.root_pos, float3_scale(dir, max_reach));
    // If clamped, place the ankle at max extension along dir and the joint colinear.
    if (!reachable && D >= max_reach) {
        out_solution.new_joint_pos        = float3_add(chain.root_pos, float3_scale(dir, L1));
        out_solution.new_end_effector_pos = float3_add(chain.root_pos, float3_scale(dir, max_reach));
    }
    out_solution.bend_direction_weight = float3_dot(pole, bend_axis) >= 0.f ? 1.f : -1.f;

    (void)theta2;
    return reachable;
}

float two_bone_joint_angle_rad(const IKSolution& solution, const float3& root_pos) {
    const float3 a = float3_sub(solution.new_joint_pos, root_pos);
    const float3 b = float3_sub(solution.new_end_effector_pos, solution.new_joint_pos);
    const float  d = float3_dot(float3_normalize_or(a, float3{0.f, -1.f, 0.f}),
                               float3_normalize_or(b, float3{0.f, -1.f, 0.f}));
    return std::acos(clampf(d, -1.0f, 1.0f));
}

} // namespace engine
