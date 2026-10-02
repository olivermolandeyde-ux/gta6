#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "gameplay/BallisticsSystem.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

struct WeaponPistolComponent {
    u32   owner_entity_id;
    u32   ammo_type_id;
    u32   current_ammo;
    u32   max_ammo;
    float fire_rate_rpm;
    float recoil_vertical;
    float recoil_horizontal;
    float recoil_recovery_rate;
    float current_recoil;
    float accuracy_base;
    float accuracy_moving_penalty;
    float spread_angle_rad;
    bool  is_reloading;
    float reload_timer_s;
    float reload_time_s;
    u32   fire_sound_event_id;
    u32   reload_sound_event_id;
};

struct WeaponRifleComponent {
    u32   owner_entity_id;
    u32   ammo_type_id;
    u32   current_ammo;
    u32   max_ammo;
    float fire_rate_rpm;
    float recoil_vertical;
    float recoil_horizontal;
    float recoil_recovery_rate;
    float current_recoil;
    float accuracy_base;
    float accuracy_moving_penalty;
    float spread_angle_rad;
    bool  is_reloading;
    float reload_timer_s;
    float reload_time_s;
    bool  is_burst_mode;
    u32   burst_count;
    u32   fire_sound_event_id;
};

struct WeaponShotgunComponent {
    u32   owner_entity_id;
    u32   ammo_type_id;
    u32   current_ammo;
    u32   max_ammo;
    float fire_rate_rpm;
    float recoil_vertical;
    float pellets_per_shot;
    float pellet_spread_rad;
    float effective_range_m;
    bool  is_pumping;
    float pump_timer_s;
    u32   fire_sound_event_id;
    u32   pump_sound_event_id;
};

struct WeaponTriggerComponent {
    u8    pull;
    u8    owner_moving;
    u16   _pad;
    float3 muzzle_ws;
    float3 aim_dir_ws;
    float  cooldown_s;
};

[[nodiscard]] Entity instantiate_pistol(World& world, const InstantiationRequest& request,
                                        const WeaponPistolComponent& pistol, float3 muzzle,
                                        float3 aim);

void UpdateWeaponSystem(World& world, float delta_time, CommandBuffer& cmd,
                        FrameAllocator& frame_alloc);

} // namespace engine
