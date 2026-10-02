#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

inline constexpr float kSpeedOfSoundMs = 343.0f;

struct AudioEmitterComponent {
    u32    sound_event_id;         // Handle to baked audio asset
    float3 position;
    float3 velocity;               // For Doppler calculation
    float  volume_scalar;
    u32    material_occlusion_mask; // Used for raycast checks
};

struct AudioListenerComponent {
    u32    entity_id; // Usually the player or camera
    float3 position;
    float3 velocity;
    float3 forward_vector;
    float3 up_vector;
    bool   is_inside_vehicle; // Triggers interior low-pass filter
};

struct AudioSourcePitchComponent {
    float source_hz;
};

// Axis-aligned masonry / glass slab used only for gunshot occlusion rays.
struct AudioOccluderSlab {
    float3 min_ws;
    float3 max_ws;
    u32    material_mask;
};

struct AudioMixResultComponent {
    float observed_hz;
    float occluded_volume;
    float lowpass_scalar;
    float azimuth_rad;
};

[[nodiscard]] Entity instantiate_audio_listener(World& world, const InstantiationRequest& request,
                                                const AudioListenerComponent& listener);

[[nodiscard]] Entity instantiate_audio_emitter(World& world, const InstantiationRequest& request,
                                               const AudioEmitterComponent& emitter,
                                               float source_hz);

[[nodiscard]] Entity instantiate_audio_occluder(World& world, const InstantiationRequest& request,
                                                const AudioOccluderSlab& slab);

void UpdateAudioSpatializationSystem(World& world, float delta_time, FrameAllocator& frame_alloc);

} // namespace engine
