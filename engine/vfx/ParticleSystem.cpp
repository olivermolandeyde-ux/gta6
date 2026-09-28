#include "vfx/ParticleSystem.h"

#include "core/Assert.h"
#include "ecs/Chunk.h"
#include "ecs/World.h"
#include "eco/WeatherSystem.h"
#include "memory/MemorySystem.h"

#include <cstring>

namespace engine {

namespace {

[[nodiscard]] u32 mix32(u32 x) noexcept {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

} // namespace

Entity instantiate_particle_emitter(World& world, const InstantiationRequest& request,
                                    const ParticleEmitterComponent& emitter, u32 max_count) {
    validate_instantiation(request);
    ENGINE_ASSERT(max_count > 0, "particle cap");

    void* slab = world.memory().chunk_pool().allocate();
    std::memset(slab, 0, kChunkBytes);
    u8* bytes = static_cast<u8*>(slab);

    const u32 cap = max_count;
    auto* positions  = reinterpret_cast<float3*>(bytes);
    auto* velocities = reinterpret_cast<float3*>(bytes + sizeof(float3) * cap);
    auto* lifetimes  = reinterpret_cast<float*>(bytes + sizeof(float3) * cap * 2);
    ENGINE_ASSERT(sizeof(float3) * cap * 2 + sizeof(float) * cap <= kChunkBytes,
                  "particle slab exceeds 16 KiB chunk");

    ParticleBatchComponent batch{};
    batch.positions    = positions;
    batch.velocities   = velocities;
    batch.lifetimes    = lifetimes;
    batch.max_count    = cap;
    batch.active_count = 0;

    return world.instantiate(request, emitter, batch);
}

void UpdateParticleSystem(World& world, float delta_time, FrameAllocator& frame_alloc) {
    ENGINE_ASSERT(delta_time >= 0.f && delta_time < 0.25f, "particle dt");

    float wind_x = 0.f;
    float wind_z = 0.f;
    for (Entity e : world.query<WeatherZoneComponent>()) {
        const WeatherZoneComponent* z = world.get<WeatherZoneComponent>(e);
        if (z) {
            wind_x = z->wind_vector_x;
            wind_z = z->wind_vector_z;
            break;
        }
    }

    const u32 max_entities = world.entity_count();
    Entity* snap = frame_alloc.allocate_array<Entity>(max_entities);
    u32 count = 0;
    for (Entity e : world.query<ParticleEmitterComponent, ParticleBatchComponent>()) {
        if (count < max_entities) {
            snap[count++] = e;
        }
    }

    for (u32 i = 0; i < count; ++i) {
        ParticleEmitterComponent* em = world.get<ParticleEmitterComponent>(snap[i]);
        ParticleBatchComponent*   b  = world.get<ParticleBatchComponent>(snap[i]);
        ENGINE_ASSERT(em && b && b->positions && b->velocities && b->lifetimes, "particle slab");

        float spawn_accum = em->spawn_rate_per_sec * delta_time;
        u32 spawn_n = static_cast<u32>(spawn_accum);
        if (spawn_accum - static_cast<float>(spawn_n) > 0.5f) {
            ++spawn_n;
        }

        u32 seed = snap[i].index() * 747796405u + 1u;
        for (u32 s = 0; s < spawn_n && b->active_count < b->max_count; ++s) {
            seed = mix32(seed);
            const float rx = (static_cast<float>(seed & 255u) / 255.f - 0.5f) * 2.f;
            seed = mix32(seed);
            const float ry = (static_cast<float>(seed & 255u) / 255.f - 0.5f) * 2.f;
            seed = mix32(seed);
            const float rz = (static_cast<float>(seed & 255u) / 255.f - 0.5f) * 2.f;

            const u32 idx = b->active_count++;
            b->positions[idx]  = em->position;
            b->velocities[idx] = float3_add(em->emission_direction,
                                            float3{em->velocity_variance.x * rx,
                                                   em->velocity_variance.y * ry,
                                                   em->velocity_variance.z * rz});
            b->lifetimes[idx]  = em->particle_lifetime;
            if (em->emitter_type == kParticleRain) {
                b->velocities[idx].y -= 8.0f;
            }
        }

        u32 w = 0;
        for (u32 p = 0; p < b->active_count; ++p) {
            b->velocities[p].x += wind_x * delta_time;
            b->velocities[p].z += wind_z * delta_time;
            if (em->emitter_type != kParticleRain) {
                b->velocities[p].y -= 1.2f * delta_time;
            }
            b->positions[p] = float3_add(b->positions[p], float3_scale(b->velocities[p], delta_time));
            b->lifetimes[p] -= delta_time;
            if (b->lifetimes[p] > 0.f) {
                if (w != p) {
                    b->positions[w]  = b->positions[p];
                    b->velocities[w] = b->velocities[p];
                    b->lifetimes[w]  = b->lifetimes[p];
                }
                ++w;
            }
        }
        b->active_count = w;
    }
}

} // namespace engine
