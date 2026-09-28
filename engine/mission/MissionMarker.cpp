#include "mission/MissionMarker.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "mission/MissionSystem.h"
#include "ui/HUDSystem.h"

#include <cmath>
#include <cstring>

namespace engine {

Entity instantiate_mission_marker(World& world, const InstantiationRequest& request,
                                  const MissionMarkerComponent& marker) {
    validate_instantiation(request);
    return world.instantiate(request, marker);
}

void UpdateMissionMarkerSystem(World& world, float delta_time, FrameAllocator& frame_alloc) {
    ENGINE_ASSERT(delta_time >= 0.f, "marker dt");
    const u32 cap = world.entity_count();
    Entity* markers = frame_alloc.allocate_array<Entity>(cap == 0 ? 1 : cap);
    u32 n = 0;
    for (Entity e : world.query<MissionMarkerComponent>()) {
        if (n < cap) {
            markers[n++] = e;
        }
    }

    u32 active_mission = 0;
    u32 active_index   = 0;
    u32 active_state   = kMissionNotStarted;
    for (Entity e : world.query<MissionStateComponent>()) {
        const MissionStateComponent* s = world.get<MissionStateComponent>(e);
        if (s && s->current_state == kMissionActive) {
            active_mission = s->mission_id;
            active_index   = s->current_objective_index;
            active_state   = s->current_state;
            break;
        }
        if (s && s->current_state == kMissionCompleted && active_state != kMissionActive) {
            active_mission = s->mission_id;
            active_index   = s->current_objective_index;
            active_state   = s->current_state;
        }
    }

    u32 blips = 0;
    float3 waypoint{};
    char obj_text[128]{};
    u32 icon = 0;
    bool have_waypoint = false;

    for (u32 i = 0; i < n; ++i) {
        MissionMarkerComponent* m = world.get<MissionMarkerComponent>(markers[i]);
        ENGINE_ASSERT(m != nullptr, "marker stale");
        const bool this_mission = (m->mission_id == active_mission);
        const bool current      = this_mission && m->objective_index == active_index
                             && active_state == kMissionActive;
        const bool done         = this_mission && active_state == kMissionCompleted;

        m->is_visible = current || (this_mission && m->objective_index < active_index);
        if (done) {
            m->is_visible = true;
            m->color_rgb  = float3{0.2f, 0.9f, 0.2f};
        } else if (current) {
            m->color_rgb = float3{1.0f, 0.85f, 0.12f};
        } else if (m->is_visible) {
            m->color_rgb = float3{0.2f, 0.9f, 0.2f};
        }
        if (m->is_visible && m->pulse_frequency_hz > 0.f) {
            const float phase = std::sin(6.2831853f * m->pulse_frequency_hz * delta_time);
            (void)phase; // pulse drives shader; keep marker visible
        }
        if (m->is_visible && (m->marker_type == 0 || m->marker_type == 2)) {
            ++blips;
        }
        if (current) {
            waypoint = m->world_position;
            icon     = m->icon_id;
            have_waypoint = true;
        }
    }

    for (Entity e : world.query<MissionObjectiveComponent>()) {
        const MissionObjectiveComponent* o = world.get<MissionObjectiveComponent>(e);
        if (o && o->mission_id == active_mission && o->objective_index == active_index) {
            std::memcpy(obj_text, o->description, sizeof(obj_text));
            break;
        }
    }

    for (Entity e : world.query<HUDStateComponent>()) {
        HUDStateComponent* hud = world.get<HUDStateComponent>(e);
        if (!hud) {
            continue;
        }
        hud->minimap.blip_count = blips;
        if (have_waypoint && active_state == kMissionActive) {
            hud->mission.is_visible     = true;
            hud->mission.waypoint_world = waypoint;
            hud->mission.waypoint_icon  = icon;
            std::memcpy(hud->mission.objective_text, obj_text, sizeof(hud->mission.objective_text));
            const float3 d = float3_sub(waypoint, float3{hud->minimap.player_pos_world.x, 0.f,
                                                         hud->minimap.player_pos_world.y});
            hud->mission.distance_m = float3_length(d);
        } else if (active_state == kMissionCompleted) {
            hud->mission.is_visible = true;
        }
    }
}

} // namespace engine
