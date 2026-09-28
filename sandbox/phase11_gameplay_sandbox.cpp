#include "Engine.h"
#include "gameplay/BallisticsSystem.h"
#include "gameplay/InventorySystem.h"
#include "gameplay/PlayerController.h"
#include "gameplay/VehicleInteraction.h"
#include "gameplay/WeaponSystem.h"
#include "physics/VehicleDynamics.h"
#include "ui/HUDSystem.h"

#include <cmath>
#include <cstdio>
#include <cstring>

int main() {
    using namespace engine;

    MemoryBudget budget{};
    budget.world_arena_bytes     = 32ull * 1024ull * 1024ull;
    budget.streaming_arena_bytes = 8ull * 1024ull * 1024ull;
    budget.chunk_pool_bytes      = 16ull * 1024ull * 1024ull;
    budget.record_pool_bytes     = 4ull * 1024ull * 1024ull;
    budget.meta_pool_bytes       = 1ull * 1024ull * 1024ull;
    budget.stack_bytes           = 1ull * 1024ull * 1024ull;
    budget.frame_bytes           = 4ull * 1024ull * 1024ull;

    Engine engine;
    engine.boot(budget, /*world_id=*/1);
    World& world = engine.world();
    FrameAllocator& frame = engine.memory().frame();

    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "phase11";

    AmmoTypeDefinition nine{};
    nine.type_id                 = 9;
    std::memcpy(nine.name, "9mm", 4);
    nine.muzzle_velocity_ms      = 370.f;
    nine.projectile_mass_kg      = 0.008f;
    nine.drag_coefficient        = 0.00035f;
    nine.penetration_mm_rha      = 4.0f;
    nine.damage_base             = 25.f;
    nine.damage_falloff_per_meter = 0.04f;
    (void)instantiate_ammo_type(world, req, nine);

    PlayerStateComponent player{};
    player.health                    = 100.f;
    player.max_health                = 100.f;
    player.armor                     = 50.f;
    player.max_armor                 = 50.f;
    player.stamina                   = 100.f;
    player.max_stamina               = 100.f;
    player.wanted_level              = 0.f;
    player.current_weapon_entity_id  = 0;
    player.current_vehicle_entity_id = 0;
    player.camera_position           = float3{0.f, 1.6f, 0.f};
    player.camera_forward            = float3{1.f, 0.f, 0.f};
    player.move_speed_penalty        = 1.f;
    Entity player_e = instantiate_player(world, req, player);

    WeaponPistolComponent pistol{};
    pistol.owner_entity_id         = player_e.index();
    pistol.ammo_type_id            = 9;
    pistol.current_ammo            = 17;
    pistol.max_ammo                = 17;
    pistol.fire_rate_rpm           = 400.f;
    pistol.recoil_vertical         = 1.25f;
    pistol.recoil_horizontal       = 0.35f;
    pistol.recoil_recovery_rate    = 1.6f;
    pistol.current_recoil          = 0.f;
    pistol.accuracy_base           = 0.92f;
    pistol.accuracy_moving_penalty = 0.7f;
    pistol.spread_angle_rad        = 0.004f;
    pistol.is_reloading            = false;
    pistol.reload_timer_s          = 0.f;
    pistol.reload_time_s           = 1.4f;
    pistol.fire_sound_event_id     = 1109;
    pistol.reload_sound_event_id   = 1110;
    Entity pistol_e = instantiate_pistol(world, req, pistol, float3{0.f, 1.5f, 0.f},
                                         float3{1.f, 0.f, 0.f});

    // 9mm muzzle energy and three-shot recoil climb
    constexpr float kDt = 1.0f / 60.0f;
    u64 frame_i = 1;
    float recoil_after_three = 0.f;
    u32 shots_fired = 0;
    for (u32 s = 0; s < 3; ++s) {
        WeaponTriggerComponent* trig = world.get<WeaponTriggerComponent>(pistol_e);
        ENGINE_ASSERT(trig != nullptr, "pistol trigger missing");
        trig->pull = 1;
        trig->owner_moving = 0;
        trig->muzzle_ws = float3{0.f, 1.5f, 0.f};
        trig->aim_dir_ws = float3{1.f, 0.f, 0.f};
        for (u32 wait = 0; wait < 12; ++wait) {
            world.begin_frame(frame_i++);
            UpdateWeaponSystem(world, kDt, world.frame_commands(), frame);
            world.frame_commands().playback(world);
        }
        ++shots_fired;
    }
    const WeaponPistolComponent* pistol_live = world.get<WeaponPistolComponent>(pistol_e);
    ENGINE_ASSERT(pistol_live != nullptr, "pistol missing");
    ENGINE_ASSERT(pistol_live->current_ammo == 14, "17-round 9mm did not decrement 3 shots");
    ENGINE_ASSERT(pistol_live->current_recoil > 2.0f, "recoil did not accumulate across 3 shots");
    recoil_after_three = pistol_live->current_recoil;

    u32 projectile_count = 0;
    float muzzle_speed = 0.f;
    float muzzle_energy = 0.f;
    for (Entity e : world.query<ProjectileComponent>()) {
        const ProjectileComponent* p = world.get<ProjectileComponent>(e);
        ENGINE_ASSERT(p != nullptr, "projectile snapshot");
        ++projectile_count;
        const float spd = float3_length(p->velocity);
        if (spd > muzzle_speed) {
            muzzle_speed = spd;
            muzzle_energy = p->remaining_energy_j;
        }
    }
    ENGINE_ASSERT(projectile_count == 3, "expected 3 pistol projectiles");
    ENGINE_ASSERT(std::fabs(muzzle_speed - 370.f) < 25.f, "9mm muzzle velocity not ~370 m/s");
    const float e_expect = 0.5f * 0.008f * 370.f * 370.f;
    ENGINE_ASSERT(std::fabs(muzzle_energy - e_expect) / e_expect < 0.2f, "muzzle energy 0.5mv^2");

    // Dedicated 50 m gravity drop (horizontal 9mm)
    ProjectileComponent drop{};
    drop.weapon_entity_id   = pistol_e.index();
    drop.ammo_type_id       = 9;
    drop.position           = float3{0.f, 1.5f, 0.f};
    drop.velocity           = float3{370.f, 0.f, 0.f};
    drop.mass_kg            = 0.008f;
    drop.drag_coefficient   = 0.00035f;
    drop.penetration_power  = 4.f;
    drop.remaining_energy_j = e_expect;
    drop.time_alive_s       = 0.f;
    drop.material_hit       = 0;
    Entity drop_e = spawn_projectile(world, req, drop);
    float y_at_50 = 1.5f;
    bool reached_50 = false;
    for (u32 s = 0; s < 120; ++s) {
        world.begin_frame(frame_i++);
        UpdateBallisticsSystem(world, kDt, frame, world.frame_commands());
        world.frame_commands().playback(world);
        const ProjectileComponent* p = world.get<ProjectileComponent>(drop_e);
        ENGINE_ASSERT(p != nullptr, "drop projectile destroyed early");
        if (p->position.x >= 50.f) {
            y_at_50 = p->position.y;
            reached_50 = true;
            break;
        }
    }
    ENGINE_ASSERT(reached_50, "9mm did not travel 50 m");
    const float drop_m = 1.5f - y_at_50;
    ENGINE_ASSERT(drop_m > 0.04f && drop_m < 0.40f, "50 m gravity drop out of range");

    // Vehicle enter → interior camera
    VehicleDynamicsSpawnDesc chassis_desc{};
    chassis_desc.position_ws = float3{4.f, 0.7f, 2.f};
    VehicleDynamicsSpawnResult wheels{};
    Entity chassis = instantiate_vehicle_chassis(world, req, chassis_desc, &wheels);

    VehicleInteractionComponent ix{};
    ix.player_entity_id          = player_e.index();
    ix.vehicle_chassis_entity_id = chassis.index();
    ix.seat_index                = 0;
    ix.is_entering               = false;
    ix.is_exiting                = false;
    ix.enter_exit_timer_s        = 0.f;
    ix.enter_exit_duration_s     = 0.25f;
    ix.entry_point_world         = float3{3.2f, 0.f, 2.f};
    ix.seat_position_local       = float3{0.35f, 0.4f, 0.1f};
    ix.camera_handle             = 0;
    Entity ix_e = instantiate_vehicle_interaction(world, req, ix);
    request_vehicle_enter(world, ix_e);
    for (u32 s = 0; s < 30; ++s) {
        world.begin_frame(frame_i++);
        UpdateVehicleInteractionSystem(world, kDt, world.frame_commands());
        world.frame_commands().playback(world);
    }
    const VehicleInteractionComponent* ix_live = world.get<VehicleInteractionComponent>(ix_e);
    const PlayerStateComponent* player_live = world.get<PlayerStateComponent>(player_e);
    ENGINE_ASSERT(ix_live && player_live, "enter missing");
    ENGINE_ASSERT(ix_live->camera_handle == 1, "interior camera not engaged");
    ENGINE_ASSERT(player_live->current_vehicle_entity_id == chassis.index(),
                  "player not seated in chassis");

    // Grid inventory: pistol 2x1 + ammo 1x2, no overlap
    PlayerInventoryComponent inv{};
    inv.owner_entity_id     = player_e.index();
    inv.slot_count          = 0;
    inv.current_weight_kg   = 0.f;
    inv.max_weight_kg       = 20.f;
    inv.equipped_weapon_slot = 0;
    inv.equipped_armor_slot  = 0;
    Entity inv_e = instantiate_player_inventory(world, req, inv);

    ItemDefinition pistol_item{};
    pistol_item.type_id = 1001;
    std::memcpy(pistol_item.name, "pistol", 7);
    pistol_item.grid_w = 2;
    pistol_item.grid_h = 1;
    pistol_item.max_stack = 1;
    pistol_item.weight_kg = 0.95f;
    pistol_item.category = 0;
    pistol_item.icon_handle = 21;
    (void)instantiate_item_definition(world, req, pistol_item);

    ItemDefinition ammo_item{};
    ammo_item.type_id = 1009;
    std::memcpy(ammo_item.name, "9mm_box", 8);
    ammo_item.grid_w = 1;
    ammo_item.grid_h = 2;
    ammo_item.max_stack = 50;
    ammo_item.weight_kg = 0.012f;
    ammo_item.category = 1;
    ammo_item.icon_handle = 22;
    (void)instantiate_item_definition(world, req, ammo_item);

    world.begin_frame(frame_i++);
    ENGINE_ASSERT(inventory_try_place(world, inv_e, pistol_item, 1, frame), "pistol 2x1 place failed");
    ENGINE_ASSERT(inventory_try_place(world, inv_e, ammo_item, 17, frame), "ammo 1x2 place failed");

    u32 slot_n = 0;
    InventorySlotComponent slots[8]{};
    for (Entity s : world.query<InventorySlotComponent>()) {
        const InventorySlotComponent* sl = world.get<InventorySlotComponent>(s);
        ENGINE_ASSERT(sl != nullptr, "slot");
        ENGINE_ASSERT(slot_n < 8, "too many slots");
        slots[slot_n++] = *sl;
    }
    ENGINE_ASSERT(slot_n == 2, "expected pistol + ammo slots");
    const bool overlap = slots[0].grid_x < slots[1].grid_x + slots[1].grid_w
                      && slots[0].grid_x + slots[0].grid_w > slots[1].grid_x
                      && slots[0].grid_y < slots[1].grid_y + slots[1].grid_h
                      && slots[0].grid_y + slots[0].grid_h > slots[1].grid_y;
    ENGINE_ASSERT(!overlap, "inventory grid overlap");

    UpdateInventorySystem(world, kDt, world.frame_commands(), frame);
    world.frame_commands().playback(world);
    const PlayerInventoryComponent* inv_live = world.get<PlayerInventoryComponent>(inv_e);
    player_live = world.get<PlayerStateComponent>(player_e);
    ENGINE_ASSERT(inv_live && player_live, "inv/player");
    ENGINE_ASSERT(inv_live->current_weight_kg > 0.5f, "weight not accumulated");
    ENGINE_ASSERT(player_live->move_speed_penalty < 1.0f, "encumbrance speed penalty missing");

    // Armor-first damage: 25 vs 50 armor
    DamageEventComponent dmg{};
    dmg.source_entity_id = pistol_e.index();
    dmg.target_entity_id = player_e.index();
    dmg.damage_amount    = 25.f;
    dmg.damage_type      = 0;
    dmg.hit_position     = float3{0.f, 1.2f, 0.f};
    dmg.hit_normal       = float3{0.f, 0.f, -1.f};
    dmg.body_part        = 0;
    enqueue_damage(world, dmg);
    world.begin_frame(frame_i++);
    UpdatePlayerController(world, kDt, world.frame_commands(), frame);
    world.frame_commands().playback(world);
    player_live = world.get<PlayerStateComponent>(player_e);
    ENGINE_ASSERT(player_live != nullptr, "player after dmg");
    ENGINE_ASSERT(std::fabs(player_live->armor - 25.f) < 0.01f, "armor did not absorb 25");
    ENGINE_ASSERT(std::fabs(player_live->health - 100.f) < 0.01f, "health leaked through armor");

    HUDStateComponent hud{};
    hud.health.color_health = float3{0.82f, 0.12f, 0.12f};
    hud.health.color_armor  = float3{0.25f, 0.45f, 0.9f};
    hud.health.screen_pos   = float2{32.f, 640.f};
    hud.health.size         = float2{220.f, 18.f};
    hud.ammo.screen_pos     = float2{1100.f, 640.f};
    std::memcpy(hud.ammo.weapon_name, "9mm Pistol", 11);
    hud.minimap.zoom_level  = 1.0f;
    hud.minimap.texture_handle = 7;
    hud.minimap.screen_pos  = float2{32.f, 32.f};
    hud.minimap.size        = float2{180.f, 180.f};
    std::memcpy(hud.mission.objective_text, "Reach the Vice shoreline.", 26);
    hud.mission.waypoint_world = float3{120.f, 0.f, 40.f};
    hud.mission.waypoint_icon  = 3;
    hud.mission.is_visible     = true;
    Entity hud_e = instantiate_hud(world, req, hud);
    world.begin_frame(frame_i++);
    UpdateHUDSystem(world, kDt, world.frame_commands());
    const HUDStateComponent* hud_live = world.get<HUDStateComponent>(hud_e);
    ENGINE_ASSERT(hud_live != nullptr, "hud missing");
    ENGINE_ASSERT(std::fabs(hud_live->health.current_health - 100.f) < 0.01f, "HUD health");
    ENGINE_ASSERT(std::fabs(hud_live->health.current_armor - 25.f) < 0.01f, "HUD armor");
    ENGINE_ASSERT(hud_live->ammo.current_clip == 14, "HUD ammo clip");
    ENGINE_ASSERT(hud_live->minimap.size.x > 0.f && hud_live->minimap.size.y > 0.f, "HUD minimap");
    ENGINE_ASSERT(hud_live->mission.is_visible && hud_live->mission.distance_m > 1.f, "HUD mission");

    std::printf("MICRO-PHASE 11 sandbox passed\n");
    std::printf("  9mm muzzle           : %.2f m/s  (energy %.1f J)\n",
                static_cast<double>(muzzle_speed), static_cast<double>(muzzle_energy));
    std::printf("  50 m gravity drop    : %.3f m\n", static_cast<double>(drop_m));
    std::printf("  pistol 17-3 ammo     : %u  recoil %.3f\n", pistol_live->current_ammo,
                static_cast<double>(recoil_after_three));
    std::printf("  vehicle camera       : %u\n", ix_live->camera_handle);
    std::printf("  inventory slots      : %u  overlap=no  weight=%.3f kg  speed=%.3f\n",
                inv_live->slot_count, static_cast<double>(inv_live->current_weight_kg),
                static_cast<double>(player_live->move_speed_penalty));
    std::printf("  armor/health         : %.1f / %.1f\n",
                static_cast<double>(player_live->armor),
                static_cast<double>(player_live->health));
    std::printf("  HUD health/ammo/map  : %.0f  clip=%u  minimap=%.0fx%.0f  mission=%.1f m\n",
                static_cast<double>(hud_live->health.current_health), hud_live->ammo.current_clip,
                static_cast<double>(hud_live->minimap.size.x),
                static_cast<double>(hud_live->minimap.size.y),
                static_cast<double>(hud_live->mission.distance_m));
    (void)shots_fired;
    (void)wheels;
    engine.shutdown();
    return 0;
}
