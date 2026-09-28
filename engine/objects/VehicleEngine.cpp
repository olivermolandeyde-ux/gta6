#include "objects/VehicleEngine.h"

#include "core/Assert.h"
#include "ecs/World.h"

#include <cmath>

namespace engine {

namespace {

constexpr float kAmbientC          = 25.0f;
constexpr float kIdleRpm           = 850.0f;
constexpr float kRedlineRpm        = 6500.0f;
constexpr float kMaxFuelKw         = 180.0f;
constexpr float kThermalCapacityKj = 12.0f;   // lumped iron block
constexpr float kRpmTimeConstant   = 0.18f;
constexpr float kOverheatOnsetC    = 105.0f;
constexpr float kSeizureC          = 135.0f;

[[nodiscard]] float exp_approach(float current, float target, float dt, float tau) {
    if (tau <= 1.0e-5f) {
        return target;
    }
    const float alpha = 1.0f - std::exp(-dt / tau);
    return current + (target - current) * alpha;
}

} // namespace

Entity instantiate_vehicle_engine(World& world, const InstantiationRequest& request,
                                  const VehicleEngineSpawnDesc& desc) {
    validate_instantiation(request);
    ENGINE_ASSERT(desc.throttle >= 0.f && desc.throttle <= 1.f, "throttle out of range");
    ENGINE_ASSERT(desc.wear >= 0.f && desc.wear <= 1.f, "wear out of range");

    VehicleEngineComponent ice{};
    ice.entity_id                   = 0;
    ice.temperature_c               = desc.temperature_c;
    ice.wear_factor                 = desc.wear;
    ice.current_rpm                 = kIdleRpm;
    ice.throttle_input              = desc.throttle;
    ice.exhaust_particle_emitter_id = desc.exhaust_particle_emitter_id;

    VehicleExhaustPlumeComponent plume{};
    plume.emitter_id         = desc.exhaust_particle_emitter_id;
    plume.soot_rate          = 0.f;
    plume.gas_temperature_c  = desc.temperature_c;
    plume.opacity            = 0.f;

    Entity entity = world.instantiate(request, ice, plume);
    VehicleEngineComponent* live = world.get<VehicleEngineComponent>(entity);
    ENGINE_ASSERT(live != nullptr, "ICE column missing after instantiate");
    live->entity_id = entity.index();
    return entity;
}

void UpdateVehicleEngineSystem(World& world, float delta_time, FrameAllocator& frame_alloc) {
    ENGINE_ASSERT(delta_time >= 0.f && delta_time < 0.25f, "ICE dt out of range");

    const u32 max_entities = world.entity_count();
    Entity* snapshot = frame_alloc.allocate_array<Entity>(max_entities);
    u32 count = 0;
    for (Entity e : world.query<VehicleEngineComponent>()) {
        if (count < max_entities) {
            snapshot[count++] = e;
        }
    }

    for (u32 i = 0; i < count; ++i) {
        Entity e = snapshot[i];
        VehicleEngineComponent* ice = world.get<VehicleEngineComponent>(e);
        ENGINE_ASSERT(ice != nullptr, "ICE snapshot stale (L2 violation if cached across frames)");
        ice->entity_id = e.index();

        const float throttle = clampf(ice->throttle_input, 0.f, 1.f);
        const float wear     = clampf(ice->wear_factor, 0.f, 1.f);

        // Fuel → shaft vs waste heat. Cold block and overheat both cut eta.
        float eta = 0.32f - 0.08f * wear;
        if (ice->temperature_c < 70.f) {
            eta *= 0.70f + 0.30f * clampf(ice->temperature_c / 70.f, 0.f, 1.f);
        }
        if (ice->temperature_c > 110.f) {
            eta *= clampf(1.0f - 0.025f * (ice->temperature_c - 110.f), 0.15f, 1.f);
        }
        const float fuel_kw       = throttle * kMaxFuelKw * (1.0f - 0.45f * wear);
        const float waste_heat_kw = fuel_kw * (1.0f - clampf(eta, 0.05f, 0.40f));

        // Forced convection scales with RPM (fan + ram air stand-in).
        const float rpm_n      = clampf(ice->current_rpm / kRedlineRpm, 0.f, 1.2f);
        const float convection = (0.35f + 1.10f * rpm_n) * (ice->temperature_c - kAmbientC);
        const float dT         = (waste_heat_kw - convection) / kThermalCapacityKj;
        ice->temperature_c     = clampf(ice->temperature_c + dT * delta_time, kAmbientC, 160.f);

        float rpm_target = kIdleRpm + throttle * (kRedlineRpm - kIdleRpm) * (1.0f - 0.35f * wear);
        if (ice->temperature_c > kOverheatOnsetC) {
            rpm_target *= clampf(1.0f - 0.012f * (ice->temperature_c - kOverheatOnsetC), 0.45f, 1.f);
        }
        if (ice->temperature_c >= kSeizureC || wear >= 0.999f) {
            rpm_target = 0.f;
        }
        ice->current_rpm = exp_approach(ice->current_rpm, rpm_target, delta_time, kRpmTimeConstant);
        if (ice->current_rpm < 0.f) {
            ice->current_rpm = 0.f;
        }

        const float overheat = clampf((ice->temperature_c - kOverheatOnsetC) / 40.f, 0.f, 1.f);
        ice->wear_factor = clampf(
            wear + delta_time * (1.2e-6f * ice->current_rpm * throttle + 9.0e-5f * overheat),
            0.f, 1.f);

        if (VehicleExhaustPlumeComponent* plume = world.get<VehicleExhaustPlumeComponent>(e)) {
            const float rich = clampf(throttle - 0.65f, 0.f, 1.f);
            plume->emitter_id        = ice->exhaust_particle_emitter_id;
            plume->soot_rate         = (40.f + 420.f * throttle) * (0.35f + 0.65f * wear)
                                       + 260.f * rich + 180.f * overheat;
            plume->gas_temperature_c = ice->temperature_c * 4.8f + 80.f * throttle;
            plume->opacity           = clampf(0.05f + 0.55f * wear + 0.35f * rich, 0.f, 1.f);
        }
    }
}

} // namespace engine
