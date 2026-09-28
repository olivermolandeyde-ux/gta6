#include "world/SkySystem.h"

#include "core/Assert.h"
#include "ecs/World.h"

#include <cmath>

namespace engine {

namespace {

constexpr float kPi = 3.14159265f;

float3 mix3(float3 a, float3 b, float t) {
    return float3_add(float3_scale(a, 1.f - t), float3_scale(b, t));
}

} // namespace

float3 AtmosphericScattering::calculateSkyColor(float3 view_dir, float3 sun_dir,
                                                float turbidity) const {
    const float3 V = float3_normalize_or(view_dir, float3{0.f, 1.f, 0.f});
    const float3 S = float3_normalize_or(sun_dir, float3{0.f, 1.f, 0.f});
    const float cosTheta = float3_dot(V, S);
    const float cosTheta2 = cosTheta * cosTheta;

    const float3 betaR{5.5e-6f, 13.0e-6f, 22.4e-6f};
    const float3 betaM{21e-6f * turbidity, 21e-6f * turbidity, 21e-6f * turbidity};

    const float rayleighPhase = 0.75f * (1.f + cosTheta2);
    const float g = 0.76f;
    const float mieDenom = std::pow(1.f + g * g - 2.f * g * cosTheta, 1.5f);
    const float miePhase = (1.f - g * g) / max_of(mieDenom, 1.0e-6f);

    float3 sky{
        (betaR.x * rayleighPhase + betaM.x * miePhase) * 1000.f,
        (betaR.y * rayleighPhase + betaM.y * miePhase) * 1000.f,
        (betaR.z * rayleighPhase + betaM.z * miePhase) * 1000.f,
    };

    const float sunDisk = clampf((cosTheta - 0.9995f) / 0.0004f, 0.f, 1.f);
    sky = float3_add(sky, float3_scale(float3{1.f, 0.9f, 0.7f}, sunDisk * 10.f));

    const float horizonFactor = std::pow(max_of(V.y, 0.f), 0.4f);
    const float hf = 0.3f + 0.7f * horizonFactor;
    sky = float3_scale(sky, hf);
    if (V.y < 0.f) {
        sky = float3_scale(sky, 0.15f);
    }
    return sky;
}

SkyComponent* find_sky(World& world) {
    for (Entity e : world.query<SkyComponent>()) {
        return world.get<SkyComponent>(e);
    }
    return nullptr;
}

void UpdateSkySystem(World& world, float delta_time, CommandBuffer& cmd) {
    (void)cmd;
    SkyComponent* sky = find_sky(world);
    if (!sky) {
        return;
    }

    // 24-minute real-time day: 1 real second = 1 in-game minute.
    sky->time_of_day = sky->time_of_day + delta_time / 60.f;
    while (sky->time_of_day >= 24.f) {
        sky->time_of_day -= 24.f;
    }
    while (sky->time_of_day < 0.f) {
        sky->time_of_day += 24.f;
    }

    const float sunAngle = (sky->time_of_day / 24.f) * 2.f * kPi - kPi * 0.5f;
    sky->sun_direction =
        float3_normalize_or(float3{std::cos(sunAngle), std::sin(sunAngle), 0.3f},
                            float3{0.f, 1.f, 0.f});
    sky->moon_direction = float3_scale(sky->sun_direction, -1.f);

    const float elev = clampf(sky->sun_direction.y, 0.f, 1.f);
    const float dusk = clampf(1.f - std::fabs(sky->sun_direction.y) * 3.f, 0.f, 1.f);
    sky->sun_color = mix3(float3{0.15f, 0.18f, 0.35f}, float3{1.f, 0.92f, 0.78f}, elev);
    sky->sun_color = mix3(sky->sun_color, float3{1.f, 0.45f, 0.18f}, dusk * 0.65f);

    AtmosphericScattering scatter{};
    sky->zenith_color = scatter.calculateSkyColor(float3{0.f, 1.f, 0.f}, sky->sun_direction,
                                                  sky->turbidity);
    sky->horizon_color = scatter.calculateSkyColor(float3{0.f, 0.05f, 1.f}, sky->sun_direction,
                                                   sky->turbidity);
}

} // namespace engine
