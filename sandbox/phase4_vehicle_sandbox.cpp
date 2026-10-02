#include "Engine.h"
#include "physics/VehicleDynamics.h"

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
    req.debug_label = "test_chassis";

    VehicleDynamicsSpawnDesc desc{};
    desc.extra_load_g = 1.0f; // gravity 1G + extra 1G = 2G vertical load
    desc.position_ws  = float3{0.f, desc.tire_radius_m + desc.rest_length_m - desc.com_offset.y, 0.f};

    VehicleDynamicsSpawnResult wheels{};
    Entity chassis = instantiate_vehicle_chassis(engine.world(), req, desc, &wheels);

    const float analytic_2g =
        (2.0f * desc.mass_kg * kGravityMs2) / (4.0f * desc.spring_k);

    constexpr float dt = 1.0f / 120.0f;
    for (u32 f = 0; f < 240; ++f) {
        engine.world().begin_frame(f);
        UpdateVehicleDynamicsSystem(engine.world(), dt, engine.memory().frame(),
                                    engine.world().frame_commands());
        engine.world().frame_commands().playback(engine.world());
    }

    const WheelNodeComponent* fl = engine.world().get<WheelNodeComponent>(wheels.wheel_fl);
    const WheelNodeComponent* fr = engine.world().get<WheelNodeComponent>(wheels.wheel_fr);
    const WheelNodeComponent* rl = engine.world().get<WheelNodeComponent>(wheels.wheel_rl);
    const WheelNodeComponent* rr = engine.world().get<WheelNodeComponent>(wheels.wheel_rr);
    ENGINE_ASSERT(fl && fr && rl && rr, "wheels missing");

    const float mean_defl =
        0.25f * (fl->current_deflection_m + fr->current_deflection_m + rl->current_deflection_m
                 + rr->current_deflection_m);
    ENGINE_ASSERT(mean_defl > 0.05f, "2G load produced no deflection");
    ENGINE_ASSERT(std::fabs(mean_defl - analytic_2g) / analytic_2g < 0.25f,
                  "deflection drifted more than 25% from 2G analytic sag");

    VehicleSteeringComponent* steer = engine.world().get<VehicleSteeringComponent>(chassis);
    VehicleChassisComponent*  body  = engine.world().get<VehicleChassisComponent>(chassis);
    ENGINE_ASSERT(steer && body, "chassis missing");
    steer->bicycle_steer_rad = 8.0f * 3.14159265f / 180.0f;
    body->velocity           = float3{0.f, 0.f, 20.0f};

    for (u32 f = 240; f < 300; ++f) {
        engine.world().begin_frame(f);
        UpdateVehicleDynamicsSystem(engine.world(), dt, engine.memory().frame(),
                                    engine.world().frame_commands());
        engine.world().frame_commands().playback(engine.world());
    }

    const float mean_alpha =
        0.5f * (std::fabs(fl->slip_angle_rad) + std::fabs(fr->slip_angle_rad));
    ENGINE_ASSERT(mean_alpha > 1.0e-4f, "front slip angle stayed zero under steer");

    std::printf("MICRO-PHASE 4 sandbox passed\n");
    std::printf("  2G analytic sag : %.4f m\n", static_cast<double>(analytic_2g));
    std::printf("  mean deflection : %.4f m\n", static_cast<double>(mean_defl));
    std::printf("  FL defl / steer : %.4f m / %.4f rad\n",
                static_cast<double>(fl->current_deflection_m),
                static_cast<double>(fl->steer_angle_rad));
    std::printf("  FR defl / steer : %.4f m / %.4f rad\n",
                static_cast<double>(fr->current_deflection_m),
                static_cast<double>(fr->steer_angle_rad));
    std::printf("  FL slip angle   : %.4f rad\n", static_cast<double>(fl->slip_angle_rad));
    std::printf("  FR slip angle   : %.4f rad\n", static_cast<double>(fr->slip_angle_rad));
    std::printf("  ackermann inner : %.4f rad\n", static_cast<double>(steer->ackermann_inner_rad));
    std::printf("  ackermann outer : %.4f rad\n", static_cast<double>(steer->ackermann_outer_rad));

    engine.shutdown();
    return 0;
}
