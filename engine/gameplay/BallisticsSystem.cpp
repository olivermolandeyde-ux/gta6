#include "gameplay/BallisticsSystem.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "eco/WeatherSystem.h"
#include "objects/BreakableWindow.h"
#include "physics/VehicleDynamics.h"

#include <cmath>

namespace engine {

namespace {

constexpr float kCoriolisOmega = 7.292115e-5f; // Earth sidereal, rad/s
constexpr float kRicochetDeg   = 15.0f;

[[nodiscard]] bool ray_aabb(float3 o, float3 d, float3 mn, float3 mx, float max_t, float* t_hit) {
    float tmin = 0.f;
    float tmax = max_t;
    const float orig[3] = {o.x, o.y, o.z};
    const float dir[3]  = {d.x, d.y, d.z};
    const float b0[3]   = {mn.x, mn.y, mn.z};
    const float b1[3]   = {mx.x, mx.y, mx.z};
    for (u32 a = 0; a < 3; ++a) {
        if (std::fabs(dir[a]) < 1e-8f) {
            if (orig[a] < b0[a] || orig[a] > b1[a]) {
                return false;
            }
            continue;
        }
        const float inv = 1.0f / dir[a];
        float t0 = (b0[a] - orig[a]) * inv;
        float t1 = (b1[a] - orig[a]) * inv;
        if (t0 > t1) {
            const float tmp = t0;
            t0 = t1;
            t1 = tmp;
        }
        tmin = t0 > tmin ? t0 : tmin;
        tmax = t1 < tmax ? t1 : tmax;
        if (tmax < tmin) {
            return false;
        }
    }
    if (t_hit) {
        *t_hit = tmin;
    }
    return tmax >= 0.f && tmin <= max_t;
}

} // namespace

Entity instantiate_ammo_type(World& world, const InstantiationRequest& request,
                             const AmmoTypeDefinition& def) {
    validate_instantiation(request);
    return world.instantiate(request, def);
}

Entity spawn_projectile(World& world, const InstantiationRequest& request,
                        const ProjectileComponent& projectile) {
    validate_instantiation(request);
    return world.instantiate(request, projectile);
}

void UpdateBallisticsSystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                            CommandBuffer& cmd) {
    ENGINE_ASSERT(delta_time > 0.f && delta_time < 0.05f, "ballistics dt");

    float3 wind{0.f, 0.f, 0.f};
    for (Entity e : world.query<WeatherZoneComponent>()) {
        const WeatherZoneComponent* z = world.get<WeatherZoneComponent>(e);
        if (z) {
            wind = float3{z->wind_vector_x, 0.f, z->wind_vector_z};
            break;
        }
    }

    const u32 max_entities = world.entity_count();
    Entity* slabs = frame_alloc.allocate_array<Entity>(max_entities);
    u32 n_slabs = 0;
    for (Entity e : world.query<BallisticMaterialSlab>()) {
        if (n_slabs < max_entities) {
            slabs[n_slabs++] = e;
        }
    }

    Entity* shots = frame_alloc.allocate_array<Entity>(max_entities);
    u32 n_shots = 0;
    for (Entity e : world.query<ProjectileComponent>()) {
        if (n_shots < max_entities) {
            shots[n_shots++] = e;
        }
    }

    for (u32 i = 0; i < n_shots; ++i) {
        Entity e = shots[i];
        ProjectileComponent* p = world.get<ProjectileComponent>(e);
        ENGINE_ASSERT(p != nullptr, "projectile snapshot stale");

        const float3 pos0 = p->position;
        float speed = float3_length(p->velocity);
        if (speed < 1.0f || p->time_alive_s > 8.0f || p->remaining_energy_j < 1.0f) {
            cmd.destroy_entity(e);
            continue;
        }

        // Gravity
        p->velocity.y -= kGravityMs2 * delta_time;
        // Quadratic-style drag: v -= v * Cd * |v| * dt
        speed = float3_length(p->velocity);
        p->velocity = float3_sub(p->velocity, float3_scale(p->velocity, p->drag_coefficient * speed * delta_time));
        // Wind
        p->velocity = float3_add(p->velocity, float3_scale(wind, 0.1f * delta_time));
        // Coriolis (Northern hemisphere, omega along +Y)
        const float3 omega{0.f, kCoriolisOmega, 0.f};
        const float3 coriolis = float3_scale(float3_cross(omega, p->velocity), -2.0f * delta_time);
        p->velocity = float3_add(p->velocity, coriolis);

        const float3 disp = float3_scale(p->velocity, delta_time);
        const float step  = float3_length(disp);
        const float3 dir  = float3_normalize_or(disp, float3{0.f, 0.f, 1.f});
        p->position = float3_add(p->position, disp);
        p->time_alive_s += delta_time;
        speed = float3_length(p->velocity);
        p->remaining_energy_j = 0.5f * p->mass_kg * speed * speed;

        if (p->position.y < 0.f) {
            p->position.y = 0.f;
            p->material_hit = 4;
            p->velocity = float3{0.f, 0.f, 0.f};
            continue;
        }

        for (u32 s = 0; s < n_slabs; ++s) {
            const BallisticMaterialSlab* slab = world.get<BallisticMaterialSlab>(slabs[s]);
            if (!slab) {
                continue;
            }
            float t_hit = 0.f;
            if (!ray_aabb(pos0, dir, slab->min_ws, slab->max_ws, step + 0.05f, &t_hit)) {
                continue;
            }
            p->material_hit = slab->material;
            const float impact_speed = speed;
            const float3 n = float3_normalize_or(
                float3{p->position.x < slab->min_ws.x ? -1.f : (p->position.x > slab->max_ws.x ? 1.f : 0.f),
                       p->position.y < slab->min_ws.y ? -1.f : (p->position.y > slab->max_ws.y ? 1.f : 0.f),
                       p->position.z < slab->min_ws.z ? -1.f : (p->position.z > slab->max_ws.z ? 1.f : 0.f)},
                float3{0.f, 0.f, -1.f});
            const float cos_i = std::fabs(float3_dot(float3_normalize_or(p->velocity, dir), n));
            const float angle_from_surface = std::asin(clampf(cos_i, 0.f, 1.f)) * (180.f / 3.14159265f);
            const float grazing = 90.f - angle_from_surface;
            if (grazing < kRicochetDeg && slab->hardness > 0.6f) {
                const float vn = float3_dot(p->velocity, n);
                p->velocity = float3_sub(p->velocity, float3_scale(n, 2.0f * vn));
                p->velocity = float3_scale(p->velocity, 0.55f);
                break;
            }
            const float rha = p->penetration_power;
            const float loss = clampf(slab->thickness_mm / max_of(rha, 0.1f), 0.15f, 0.95f);
            p->velocity = float3_scale(p->velocity, 1.0f - loss);
            p->remaining_energy_j *= (1.0f - loss);
            (void)impact_speed;
            if (p->remaining_energy_j < 40.f) {
                p->velocity = float3{0.f, 0.f, 0.f};
            }
            break;
        }
    }
}

} // namespace engine
