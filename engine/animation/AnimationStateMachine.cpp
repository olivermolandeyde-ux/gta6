#include "animation/AnimationStateMachine.h"

#include "ai/PedestrianBehavior.h"
#include "animation/TwoBoneIK.h"
#include "core/Assert.h"
#include "ecs/World.h"

namespace engine {

namespace {

GroundHeightFn g_ground = nullptr;

[[nodiscard]] PedestrianGroundSample default_ground(float3 world_xz) {
    PedestrianGroundSample s{};
    s.height_y  = 0.f;
    s.normal_ws = float3{0.f, 1.f, 0.f};
    // 0.5 m kerb / stair along +Z starting at z >= 0.4, used by the MICRO-PHASE 5 sandbox.
    if (world_xz.z >= 0.40f) {
        s.height_y = 0.50f;
    }
    return s;
}

[[nodiscard]] float3 blend3(float3 a, float3 b, float t) {
    return float3_add(float3_scale(a, 1.0f - t), float3_scale(b, t));
}

} // namespace

void set_pedestrian_ground_sampler(GroundHeightFn fn) {
    g_ground = fn;
}

Entity attach_pedestrian_ik(World& world, Entity pedestrian, u32 skeleton_handle,
                            const PedestrianLimbRestComponent& rest) {
    ENGINE_ASSERT(world.is_alive(pedestrian), "attach_pedestrian_ik stale entity");
    AnimationIKComponent ik{};
    ik.skeleton_handle         = skeleton_handle;
    ik.left_foot_target_world  = float3{0.f, 0.f, 0.f};
    ik.right_foot_target_world = float3{0.f, 0.f, 0.f};
    ik.left_foot_ik_weight     = 0.f;
    ik.right_foot_ik_weight    = 0.f;

    PedestrianIkResultComponent result{};
    world.add_component<PedestrianLimbRestComponent>(pedestrian, rest);
    world.add_component<AnimationIKComponent>(pedestrian, ik);
    world.add_component<PedestrianIkResultComponent>(pedestrian, result);
    return pedestrian;
}

void UpdateAnimationIKSystem(World& world, float delta_time, FrameAllocator& frame_alloc) {
    ENGINE_ASSERT(delta_time >= 0.f, "anim dt");
    GroundHeightFn ground = g_ground ? g_ground : &default_ground;

    const u32 max_entities = world.entity_count();
    Entity* snap = frame_alloc.allocate_array<Entity>(max_entities);
    u32 count = 0;
    for (Entity e : world.query<PedestrianBehaviorComponent, PedestrianWorldPose, AnimationIKComponent,
                                PedestrianLimbRestComponent>()) {
        if (count < max_entities) {
            snap[count++] = e;
        }
    }

    for (u32 i = 0; i < count; ++i) {
        Entity e = snap[i];
        const PedestrianBehaviorComponent* brain = world.get<PedestrianBehaviorComponent>(e);
        const PedestrianWorldPose*         pose  = world.get<PedestrianWorldPose>(e);
        AnimationIKComponent*              ik    = world.get<AnimationIKComponent>(e);
        const PedestrianLimbRestComponent* rest  = world.get<PedestrianLimbRestComponent>(e);
        PedestrianIkResultComponent*       result = world.get<PedestrianIkResultComponent>(e);
        ENGINE_ASSERT(brain && pose && ik && rest, "anim snapshot stale");

        const float flee_w = (brain->state_id == kPedestrianStateFleeing) ? 1.0f
                             : (brain->state_id == kPedestrianStateAlerted) ? 0.55f
                                                                            : 0.25f;
        ik->left_foot_ik_weight  = clampf(0.35f + 0.65f * flee_w, 0.f, 1.f);
        ik->right_foot_ik_weight = ik->left_foot_ik_weight;

        const float3 hip_l = float3_add(pose->position_ws,
                                        float3{-rest->hip_width_m * 0.5f, rest->pelvis_height_m, 0.f});
        const float3 hip_r = float3_add(pose->position_ws,
                                        float3{ rest->hip_width_m * 0.5f, rest->pelvis_height_m, 0.f});

        const float3 anim_l = float3_add(pose->position_ws, rest->animated_left_foot_os);
        const float3 anim_r = float3_add(pose->position_ws, rest->animated_right_foot_os);

        const PedestrianGroundSample g_l = ground(anim_l);
        const PedestrianGroundSample g_r = ground(anim_r);

        float3 ik_l = anim_l;
        ik_l.y      = g_l.height_y;
        float3 ik_r = anim_r;
        ik_r.y      = g_r.height_y;

        ik->left_foot_target_world  = blend3(anim_l, ik_l, ik->left_foot_ik_weight);
        ik->right_foot_target_world = blend3(anim_r, ik_r, ik->right_foot_ik_weight);

        BoneChain left{};
        left.root_pos     = hip_l;
        left.joint_pos    = float3_add(hip_l, float3{0.f, -rest->thigh_length_m, 0.05f});
        left.end_effector = anim_l;
        left.bone1_length = rest->thigh_length_m;
        left.bone2_length = rest->calf_length_m;

        BoneChain right = left;
        right.root_pos     = hip_r;
        right.joint_pos    = float3_add(hip_r, float3{0.f, -rest->thigh_length_m, 0.05f});
        right.end_effector = anim_r;

        const float3 knee_pole{0.f, 0.f, 1.f};
        IKSolution sl{};
        IKSolution sr{};
        const bool rl = SolveTwoBoneIK(left, ik->left_foot_target_world, knee_pole, sl);
        const bool rr = SolveTwoBoneIK(right, ik->right_foot_target_world, knee_pole, sr);

        if (result) {
            result->left_knee_ws          = sl.new_joint_pos;
            result->right_knee_ws         = sr.new_joint_pos;
            result->left_joint_angle_rad  = two_bone_joint_angle_rad(sl, hip_l);
            result->right_joint_angle_rad = two_bone_joint_angle_rad(sr, hip_r);
            result->left_reachable        = rl ? 1.f : 0.f;
            result->right_reachable       = rr ? 1.f : 0.f;
        }
    }
}

} // namespace engine
