#include "ui/HUDSystem.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"
#include "gameplay/WeaponSystem.h"

#include <cmath>

namespace engine {

Entity instantiate_hud(World& world, const InstantiationRequest& request,
                       const HUDStateComponent& hud) {
    validate_instantiation(request);
    return world.instantiate(request, hud);
}

void UpdateHUDSystem(World& world, float delta_time, CommandBuffer& cmd) {
    (void)delta_time;
    (void)cmd;
    PlayerStateComponent player{};
    bool have_player = false;
    for (Entity e : world.query<PlayerStateComponent>()) {
        const PlayerStateComponent* p = world.get<PlayerStateComponent>(e);
        if (p) {
            player = *p;
            have_player = true;
            break;
        }
    }
    WeaponPistolComponent pistol{};
    bool have_pistol = false;
    for (Entity e : world.query<WeaponPistolComponent>()) {
        const WeaponPistolComponent* w = world.get<WeaponPistolComponent>(e);
        if (w) {
            pistol = *w;
            have_pistol = true;
            break;
        }
    }

    for (Entity e : world.query<HUDStateComponent>()) {
        HUDStateComponent* hud = world.get<HUDStateComponent>(e);
        ENGINE_ASSERT(hud != nullptr, "hud stale");
        if (have_player) {
            hud->health.current_health = player.health;
            hud->health.max_health     = player.max_health;
            hud->health.current_armor  = player.armor;
            hud->health.max_armor      = player.max_armor;
            hud->minimap.player_pos_world = float2{player.camera_position.x, player.camera_position.z};
            hud->minimap.player_heading_rad =
                std::atan2(player.camera_forward.x, player.camera_forward.z);
        }
        if (have_pistol) {
            hud->ammo.current_clip = pistol.current_ammo;
            hud->ammo.reserve_ammo = pistol.max_ammo;
        }
        if (hud->mission.is_visible) {
            const float3 d = float3_sub(hud->mission.waypoint_world,
                                        float3{hud->minimap.player_pos_world.x, 0.f,
                                               hud->minimap.player_pos_world.y});
            hud->mission.distance_m = float3_length(d);
        }
    }
}

} // namespace engine
