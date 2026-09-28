#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

struct HUDHealthBar {
    float current_health;
    float max_health;
    float current_armor;
    float max_armor;
    float3 color_health;
    float3 color_armor;
    float2 screen_pos;
    float2 size;
};

struct HUDAmmoCounter {
    u32   current_clip;
    u32   reserve_ammo;
    u32   weapon_icon_handle;
    char  weapon_name[24];
    float2 screen_pos;
};

struct HUDMinimap {
    float2 player_pos_world;
    float  player_heading_rad;
    float  zoom_level;
    u32    texture_handle;
    u32    blip_count;
    float2 screen_pos;
    float2 size;
};

struct HUDMissionTracker {
    char  objective_text[128];
    float distance_m;
    u32   waypoint_icon;
    float3 waypoint_world;
    bool  is_visible;
};

struct HUDStateComponent {
    HUDHealthBar      health;
    HUDAmmoCounter    ammo;
    HUDMinimap        minimap;
    HUDMissionTracker mission;
};

[[nodiscard]] Entity instantiate_hud(World& world, const InstantiationRequest& request,
                                     const HUDStateComponent& hud);

void UpdateHUDSystem(World& world, float delta_time, CommandBuffer& cmd);

} // namespace engine
