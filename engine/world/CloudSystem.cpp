#include "world/CloudSystem.h"

#include "core/Assert.h"
#include "ecs/World.h"

#include <cmath>

namespace engine {

namespace {

float hash3(float x, float y, float z) {
    float fx = x * 0.3183099f + 0.1f;
    float fy = y * 0.3183099f + 0.1f;
    float fz = z * 0.3183099f + 0.1f;
    fx = fx - std::floor(fx);
    fy = fy - std::floor(fy);
    fz = fz - std::floor(fz);
    fx *= 17.f;
    fy *= 17.f;
    fz *= 17.f;
    const float n = fx * fy * fz * (fx + fy + fz);
    return n - std::floor(n);
}

float noise3D(float3 x) {
    const float3 i{std::floor(x.x), std::floor(x.y), std::floor(x.z)};
    float3 f{x.x - i.x, x.y - i.y, x.z - i.z};
    f.x = f.x * f.x * (3.f - 2.f * f.x);
    f.y = f.y * f.y * (3.f - 2.f * f.y);
    f.z = f.z * f.z * (3.f - 2.f * f.z);
    const auto h = [&](float ox, float oy, float oz) {
        return hash3(i.x + ox, i.y + oy, i.z + oz);
    };
    const float x00 = h(0, 0, 0) * (1.f - f.x) + h(1, 0, 0) * f.x;
    const float x10 = h(0, 1, 0) * (1.f - f.x) + h(1, 1, 0) * f.x;
    const float x01 = h(0, 0, 1) * (1.f - f.x) + h(1, 0, 1) * f.x;
    const float x11 = h(0, 1, 1) * (1.f - f.x) + h(1, 1, 1) * f.x;
    const float y0 = x00 * (1.f - f.y) + x10 * f.y;
    const float y1 = x01 * (1.f - f.y) + x11 * f.y;
    return y0 * (1.f - f.z) + y1 * f.z;
}

float fbm3(float3 p) {
    float value = 0.f;
    float amplitude = 0.5f;
    for (u32 i = 0; i < 4; ++i) {
        value += amplitude * noise3D(p);
        p = float3_scale(p, 2.f);
        amplitude *= 0.5f;
    }
    return value;
}

} // namespace

CloudRayRgba CloudRayMarch::rayMarchClouds(float3 ray_origin, float3 ray_dir, float max_distance,
                                           const CloudLayerComponent& clouds) const {
    CloudRayRgba out{0.f, 0.f, 0.f, 0.f};
    const float3 dir = float3_normalize_or(ray_dir, float3{0.f, 1.f, 0.f});
    if (std::fabs(dir.y) < 0.001f) {
        return out;
    }
    const float t = (clouds.layer_position.y - ray_origin.y) / dir.y;
    if (t < 0.f || t > max_distance) {
        return out;
    }
    float3 hit = float3_add(ray_origin, float3_scale(dir, t));
    float totalT = 1.f;
    float3 scatter{0.f, 0.f, 0.f};
    const float stepSize = clouds.layer_thickness / 16.f;
    for (u32 i = 0; i < 16; ++i) {
        float3 samplePos = float3_add(hit, float3_scale(dir, stepSize * static_cast<float>(i)));
        samplePos.x += clouds.layer_position.x;
        samplePos.z += clouds.layer_position.z;
        float density = fbm3(float3_scale(samplePos, 0.001f));
        const float edge = 1.f - clouds.cloud_coverage;
        density = clampf((density - edge) / max_of(1.f - edge, 1.0e-4f), 0.f, 1.f);
        if (density > 0.01f) {
            const float lightDist = stepSize * 4.f;
            const float3 lightP =
                float3_add(samplePos, float3{0.f, lightDist, 0.f});
            const float lightDensity = fbm3(float3_scale(lightP, 0.001f));
            const float transmittance = std::exp(-lightDensity * lightDist * 0.1f);
            const float w = density * transmittance * stepSize * 0.01f;
            scatter.x += w * totalT;
            scatter.y += w * totalT;
            scatter.z += w * totalT;
            totalT *= std::exp(-density * stepSize * 0.1f);
            if (totalT < 0.01f) {
                break;
            }
        }
    }
    out.x = scatter.x;
    out.y = scatter.y;
    out.z = scatter.z;
    out.w = 1.f - totalT;
    return out;
}

CloudLayerComponent* find_cloud_layer(World& world) {
    for (Entity e : world.query<CloudLayerComponent>()) {
        return world.get<CloudLayerComponent>(e);
    }
    return nullptr;
}

void UpdateCloudSystem(World& world, float delta_time, CommandBuffer& cmd) {
    (void)cmd;
    CloudLayerComponent* clouds = find_cloud_layer(world);
    if (!clouds) {
        return;
    }
    clouds->layer_position = float3_add(clouds->layer_position,
                                        float3_scale(clouds->wind_velocity, delta_time));
}

} // namespace engine
