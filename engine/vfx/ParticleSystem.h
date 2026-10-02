#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

inline constexpr u32 kParticleExhaust    = 0;
inline constexpr u32 kParticleRain       = 1;
inline constexpr u32 kParticleGlassShard = 2;

struct ParticleEmitterComponent {
    u32    emitter_type; // 0=Exhaust (Phase 3), 1=Rain, 2=GlassShards (Phase 3)
    float3 position;
    float3 emission_direction;
    float  spawn_rate_per_sec;
    float  particle_lifetime;
    float3 velocity_variance;
};

struct ParticleBatchComponent {
    float3* positions;  // Points to a slab in the Chunk Pool
    float3* velocities;
    float*  lifetimes;
    u32     max_count;
    u32     active_count;
};

[[nodiscard]] Entity instantiate_particle_emitter(World& world, const InstantiationRequest& request,
                                                  const ParticleEmitterComponent& emitter,
                                                  u32 max_count);

void UpdateParticleSystem(World& world, float delta_time, FrameAllocator& frame_alloc);

} // namespace engine
