#include "audio/AudioEngine.h"

#include "core/Assert.h"
#include "ecs/World.h"

#include <cmath>

namespace engine {

namespace {

[[nodiscard]] bool ray_aabb(float3 origin, float3 dir, float3 bmin, float3 bmax, float max_t) {
    float tmin = 0.f;
    float tmax = max_t;
    const float orig[3] = {origin.x, origin.y, origin.z};
    const float d[3]    = {dir.x, dir.y, dir.z};
    const float mn[3]   = {bmin.x, bmin.y, bmin.z};
    const float mx[3]   = {bmax.x, bmax.y, bmax.z};
    for (u32 a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1.0e-8f) {
            if (orig[a] < mn[a] || orig[a] > mx[a]) {
                return false;
            }
            continue;
        }
        const float inv = 1.0f / d[a];
        float t0 = (mn[a] - orig[a]) * inv;
        float t1 = (mx[a] - orig[a]) * inv;
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
    return tmax >= 0.f && tmin <= max_t;
}

} // namespace

Entity instantiate_audio_listener(World& world, const InstantiationRequest& request,
                                  const AudioListenerComponent& listener) {
    validate_instantiation(request);
    AudioMixResultComponent mix{};
    mix.observed_hz      = 0.f;
    mix.occluded_volume  = 1.f;
    mix.lowpass_scalar   = 1.f;
    mix.azimuth_rad      = 0.f;
    return world.instantiate(request, listener, mix);
}

Entity instantiate_audio_emitter(World& world, const InstantiationRequest& request,
                                 const AudioEmitterComponent& emitter, float source_hz) {
    validate_instantiation(request);
    AudioSourcePitchComponent pitch{};
    pitch.source_hz = source_hz;
    AudioMixResultComponent mix{};
    return world.instantiate(request, emitter, pitch, mix);
}

Entity instantiate_audio_occluder(World& world, const InstantiationRequest& request,
                                  const AudioOccluderSlab& slab) {
    validate_instantiation(request);
    return world.instantiate(request, slab);
}

void UpdateAudioSpatializationSystem(World& world, float delta_time, FrameAllocator& frame_alloc) {
    (void)delta_time;
    const u32 max_entities = world.entity_count();

    Entity* listeners = frame_alloc.allocate_array<Entity>(max_entities);
    u32 n_listeners = 0;
    for (Entity e : world.query<AudioListenerComponent>()) {
        if (n_listeners < max_entities) {
            listeners[n_listeners++] = e;
        }
    }

    Entity* emitters = frame_alloc.allocate_array<Entity>(max_entities);
    u32 n_emitters = 0;
    for (Entity e : world.query<AudioEmitterComponent>()) {
        if (n_emitters < max_entities) {
            emitters[n_emitters++] = e;
        }
    }

    Entity* walls = frame_alloc.allocate_array<Entity>(max_entities);
    u32 n_walls = 0;
    for (Entity e : world.query<AudioOccluderSlab>()) {
        if (n_walls < max_entities) {
            walls[n_walls++] = e;
        }
    }

    if (n_listeners == 0) {
        return;
    }

    const AudioListenerComponent* listener = world.get<AudioListenerComponent>(listeners[0]);
    ENGINE_ASSERT(listener != nullptr, "listener snapshot stale");

    const float3 fwd = float3_normalize_or(listener->forward_vector, float3{0.f, 0.f, 1.f});
    const float3 up  = float3_normalize_or(listener->up_vector, float3{0.f, 1.f, 0.f});
    const float3 right = float3_normalize_or(float3_cross(up, fwd), float3{1.f, 0.f, 0.f});

    for (u32 i = 0; i < n_emitters; ++i) {
        Entity e = emitters[i];
        const AudioEmitterComponent* emit = world.get<AudioEmitterComponent>(e);
        const AudioSourcePitchComponent* pitch = world.get<AudioSourcePitchComponent>(e);
        AudioMixResultComponent* mix = world.get<AudioMixResultComponent>(e);
        ENGINE_ASSERT(emit != nullptr, "emitter snapshot stale");

        const float3 to_src = float3_sub(emit->position, listener->position);
        const float dist    = max_of(float3_length(to_src), 0.05f);
        const float3 dir    = float3_scale(to_src, 1.0f / dist);

        // Doppler: f_observed = f_source * ((v_sound + v_listener_proj) / (v_sound - v_source_proj))
        const float v_listener_proj = float3_dot(listener->velocity, dir);
        const float v_source_proj   = float3_dot(emit->velocity, dir);
        float denom = kSpeedOfSoundMs - v_source_proj;
        if (std::fabs(denom) < 1.0f) {
            denom = denom >= 0.f ? 1.0f : -1.0f;
        }
        const float f_source = pitch ? pitch->source_hz : 440.f;
        float f_observed = f_source * ((kSpeedOfSoundMs + v_listener_proj) / denom);
        f_observed = clampf(f_observed, 20.f, 16000.f);

        bool occluded = false;
        for (u32 w = 0; w < n_walls; ++w) {
            const AudioOccluderSlab* slab = world.get<AudioOccluderSlab>(walls[w]);
            ENGINE_ASSERT(slab != nullptr, "occluder snapshot stale");
            if ((slab->material_mask & emit->material_occlusion_mask) == 0 && emit->material_occlusion_mask != 0) {
                continue;
            }
            if (ray_aabb(listener->position, dir, slab->min_ws, slab->max_ws, dist)) {
                occluded = true;
                break;
            }
        }

        float volume = emit->volume_scalar / (1.0f + dist * 0.08f);
        float lowpass = 1.0f;
        if (occluded) {
            volume *= 0.3f;
            lowpass *= 0.25f; // masonry low-pass
        }
        if (listener->is_inside_vehicle) {
            lowpass *= 0.55f;
        }

        const float azimuth = std::atan2(float3_dot(dir, right), float3_dot(dir, fwd));

        if (mix) {
            mix->observed_hz     = f_observed;
            mix->occluded_volume = volume;
            mix->lowpass_scalar  = lowpass;
            mix->azimuth_rad     = azimuth;
        }
    }
}

} // namespace engine
