#include "Engine.h"
#include "ai/PedestrianBehavior.h"
#include "animation/AnimationStateMachine.h"
#include "animation/TwoBoneIK.h"

#include <cmath>
#include <cstdio>

int main() {
    using namespace engine;

    MemoryBudget budget{};
    budget.world_arena_bytes     = 32ull * 1024ull * 1024ull;
    budget.streaming_arena_bytes = 8ull * 1024ull * 1024ull;
    budget.chunk_pool_bytes      = 16ull * 1024ull * 1024ull;
    budget.record_pool_bytes     = 4ull * 1024ull * 1024ull;
    budget.meta_pool_bytes       = 1ull * 1024ull * 1024ull;
    budget.stack_bytes           = 1ull * 1024ull * 1024ull;
    budget.frame_bytes           = 4ull * 1024ull * 1024ull;

    Engine engine;
    engine.boot(budget, /*world_id=*/1);

    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "civilian";

    PedestrianSpawnDesc ped_desc{};
    ped_desc.position_ws         = float3{0.f, 0.f, 0.f};
    ped_desc.facing_ws           = float3{0.f, 0.f, 1.f};
    ped_desc.reaction_delay_s    = 0.20f;
    ped_desc.destination_node_id = 1;
    Entity ped = instantiate_pedestrian(engine.world(), req, ped_desc);

    req.debug_label = "gunshot";
    PedestrianThreatStimulus threat{};
    threat.position_ws = float3{0.f, 0.f, 3.0f};
    threat.loudness    = 1.0f;
    threat.radius_m    = 12.0f;
    (void)instantiate_pedestrian_threat(engine.world(), req, threat);

    req.debug_label = "nav";
    (void)instantiate_pedestrian_nav_node(engine.world(), req, 1, float3{0.f, 0.f, 8.f});
    (void)instantiate_pedestrian_nav_node(engine.world(), req, 2, float3{0.f, 0.f, -12.f});

    PedestrianLimbRestComponent rest{};
    rest.thigh_length_m         = 0.42f;
    rest.calf_length_m          = 0.41f;
    rest.pelvis_height_m        = 0.95f;
    rest.hip_width_m            = 0.28f;
    rest.animated_left_foot_os  = float3{-0.14f, 0.f, 0.55f};  // 0.5 m step-up sample
    rest.animated_right_foot_os = float3{ 0.14f, 0.f, 0.55f};
    (void)attach_pedestrian_ik(engine.world(), ped, /*skeleton_handle=*/77, rest);

    constexpr float dt = 1.0f / 60.0f;
    for (u32 f = 0; f < 90; ++f) {
        engine.world().begin_frame(f);
        UpdatePedestrianAISystem(engine.world(), dt, engine.world().frame_commands(),
                                 engine.memory().frame());
        engine.world().frame_commands().playback(engine.world());
        UpdateAnimationIKSystem(engine.world(), dt, engine.memory().frame());
    }

    const PedestrianBehaviorComponent* brain = engine.world().get<PedestrianBehaviorComponent>(ped);
    const PedestrianIkResultComponent* ik    = engine.world().get<PedestrianIkResultComponent>(ped);
    ENGINE_ASSERT(brain != nullptr && ik != nullptr, "pedestrian missing");
    ENGINE_ASSERT(brain->state_id == kPedestrianStateFleeing, "did not enter flee");
    ENGINE_ASSERT(engine.world().has<PedestrianFleeingTag>(ped), "FleeingTag not deferred-added");
    ENGINE_ASSERT(brain->fear_level > 0.6f, "fear did not rise under threat");
    ENGINE_ASSERT(ik->left_reachable > 0.5f, "0.5m step should be reachable");
    ENGINE_ASSERT(ik->left_joint_angle_rad > 0.05f, "knee did not flex on the step");

    // Direct 0.5 m step-up solve (hip -> ankle raised 0.5 m) for the printed joint angle.
    BoneChain chain{};
    chain.root_pos     = float3{0.f, 0.95f, 0.f};
    chain.joint_pos    = float3{0.f, 0.53f, 0.05f};
    chain.end_effector = float3{0.f, 0.00f, 0.10f};
    chain.bone1_length = 0.42f;
    chain.bone2_length = 0.41f;
    IKSolution step{};
    const bool ok = SolveTwoBoneIK(chain, float3{0.f, 0.50f, 0.10f}, float3{0.f, 0.f, 1.f}, step);
    const float step_angle = two_bone_joint_angle_rad(step, chain.root_pos);
    ENGINE_ASSERT(ok, "direct 0.5m step IK unreachable");

    std::printf("MICRO-PHASE 5 sandbox passed\n");
    std::printf("  state_id        : %u (2=Fleeing)\n", brain->state_id);
    std::printf("  fear_level      : %.3f\n", static_cast<double>(brain->fear_level));
    std::printf("  flee node       : %u\n", brain->destination_node_id);
    std::printf("  fleeing tag     : %s\n", engine.world().has<PedestrianFleeingTag>(ped) ? "yes" : "no");
    std::printf("  left knee angle : %.3f rad (%.1f deg)\n",
                static_cast<double>(ik->left_joint_angle_rad),
                static_cast<double>(ik->left_joint_angle_rad * 180.0f / 3.14159265f));
    std::printf("  0.5m step IK    : reachable=%d  joint=%.3f rad (%.1f deg)\n",
                static_cast<int>(ok), static_cast<double>(step_angle),
                static_cast<double>(step_angle * 180.0f / 3.14159265f));
    std::printf("  step knee pos   : %.3f %.3f %.3f\n",
                static_cast<double>(step.new_joint_pos.x),
                static_cast<double>(step.new_joint_pos.y),
                static_cast<double>(step.new_joint_pos.z));

    engine.shutdown();
    return 0;
}
