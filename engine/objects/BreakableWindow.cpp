#include "objects/BreakableWindow.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "memory/FrameAllocator.h"
#include "memory/MemorySystem.h"

#include <cmath>

namespace engine {

namespace {

constexpr float kTemperedFractureJoules = 220.0f;
constexpr float kPaneThickness          = 0.012f; // 12 mm architectural

[[nodiscard]] float3 normalize_or(float3 v, float3 fallback) {
    const float ls = float3_length_sq(v);
    if (ls < 1.0e-12f) {
        return fallback;
    }
    const float inv = 1.0f / std::sqrt(ls);
    return float3_scale(v, inv);
}

[[nodiscard]] bool point_on_pane(const BreakableWindowPose& pose, float3 point, float* out_closing) {
    const float3 rel    = float3_sub(point, pose.center_ws);
    const float3 normal = normalize_or(pose.normal_ws, float3{0.f, 0.f, 1.f});
    const float  depth  = float3_dot(rel, normal);
    if (std::fabs(depth) > kPaneThickness * 4.0f) {
        return false;
    }

    // Build an in-plane basis from world-up, falling back to world-x.
    float3 up = {0.f, 1.f, 0.f};
    if (std::fabs(float3_dot(up, normal)) > 0.94f) {
        up = float3{1.f, 0.f, 0.f};
    }
    float3 tangent = float3{
        up.y * normal.z - up.z * normal.y,
        up.z * normal.x - up.x * normal.z,
        up.x * normal.y - up.y * normal.x};
    tangent = normalize_or(tangent, float3{1.f, 0.f, 0.f});
    const float3 bitangent = float3{
        normal.y * tangent.z - normal.z * tangent.y,
        normal.z * tangent.x - normal.x * tangent.z,
        normal.x * tangent.y - normal.y * tangent.x};

    const float u = float3_dot(rel, tangent);
    const float v = float3_dot(rel, bitangent);
    if (std::fabs(u) > pose.half_width || std::fabs(v) > pose.half_height) {
        return false;
    }
    if (out_closing) {
        *out_closing = depth;
    }
    return true;
}

[[nodiscard]] u16 shard_count_from_seed(u16 seed, float energy_j) {
    const u32 mixed = static_cast<u32>(seed) * 747796405u + 2891336453u;
    const u16 extra = static_cast<u16>((mixed >> 16) & 31u);
    const u16 from_e = static_cast<u16>(clampf(energy_j / 40.f, 4.f, 48.f));
    return static_cast<u16>(from_e + extra);
}

} // namespace

Entity instantiate_breakable_window(World& world, const InstantiationRequest& request,
                                    const BreakableWindowSpawnDesc& desc) {
    validate_instantiation(request);
    ENGINE_ASSERT(desc.half_width > 0.f && desc.half_height > 0.f, "window extents");

    BreakableWindowComponent glass{};
    glass.mesh_id_intact       = desc.mesh_id_intact;
    glass.mesh_id_shattered    = desc.mesh_id_shattered;
    glass.structural_integrity = 1.f;
    glass.fracture_seed        = desc.fracture_seed == 0 ? static_cast<u16>(1) : desc.fracture_seed;
    glass._pad                 = 0;

    BreakableWindowPose pose{};
    pose.center_ws   = desc.center_ws;
    pose.normal_ws   = normalize_or(desc.normal_ws, float3{0.f, 0.f, 1.f});
    pose.half_width  = desc.half_width;
    pose.half_height = desc.half_height;

    return world.instantiate(request, glass, pose);
}

void ProcessWindowImpactsSystem(World& world, const ImpactEvent* impacts, u32 impact_count,
                                CommandBuffer& cmd) {
    if (impact_count == 0 || impacts == nullptr) {
        return;
    }

    FrameAllocator& frame = world.memory().frame();
    const u32 max_entities = world.entity_count();
    Entity* snapshot = frame.allocate_array<Entity>(max_entities);
    u32 count = 0;
    for (Entity e : world.query<BreakableWindowComponent, BreakableWindowPose>()) {
        if (count < max_entities) {
            snapshot[count++] = e;
        }
    }

    const u32 window_id = world.registry().id_of<WindowShatteredTag>();

    for (u32 i = 0; i < count; ++i) {
        Entity e = snapshot[i];
        BreakableWindowComponent* glass = world.get<BreakableWindowComponent>(e);
        BreakableWindowPose*      pose  = world.get<BreakableWindowPose>(e);
        ENGINE_ASSERT(glass != nullptr && pose != nullptr, "window snapshot stale");

        if (glass->structural_integrity <= 0.f || world.has<WindowShatteredTag>(e)) {
            continue;
        }

        float accumulated_j = 0.f;
        for (u32 n = 0; n < impact_count; ++n) {
            const ImpactEvent& hit = impacts[n];
            const bool targeted = !hit.target_entity.is_null() && hit.target_entity == e;
            float dummy = 0.f;
            const bool on_pane  = point_on_pane(*pose, hit.impact_point, &dummy);
            if (!targeted && !on_pane) {
                continue;
            }
            if (!on_pane && targeted) {
                // Explicit target still requires the strike to be near the pane.
                const float3 delta = float3_sub(hit.impact_point, pose->center_ws);
                if (float3_length_sq(delta) > 4.0f) {
                    continue;
                }
            }

            const float3 normal = normalize_or(pose->normal_ws, float3{0.f, 0.f, 1.f});
            const float closing = -float3_dot(hit.impact_velocity, normal);
            if (closing <= 0.f) {
                continue; // receding, no fracture work
            }
            const float mass = hit.mass > 0.f ? hit.mass : 1.f;
            accumulated_j += 0.5f * mass * closing * closing;
        }

        if (accumulated_j <= 1.0e-4f) {
            continue;
        }

        const float toughness = kTemperedFractureJoules * (0.35f + 0.65f * glass->structural_integrity);
        glass->structural_integrity =
            clampf(glass->structural_integrity - accumulated_j / toughness, 0.f, 1.f);

        if (glass->structural_integrity <= 0.f) {
            WindowShatteredTag tag{};
            tag.fracture_seed = glass->fracture_seed;
            tag.shard_count   = shard_count_from_seed(glass->fracture_seed, accumulated_j);
            cmd.add_component<WindowShatteredTag>(e, window_id, tag);
        }
    }
}

} // namespace engine
