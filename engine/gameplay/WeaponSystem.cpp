#include "gameplay/WeaponSystem.h"

#include "core/Assert.h"
#include "ecs/World.h"

#include <cmath>

namespace engine {

namespace {

u32 g_shot_salt = 1;

[[nodiscard]] float hash01(u32 x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return static_cast<float>(x & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

void spawn_one(World& world, u32 weapon_index, u32 ammo_id, float3 muzzle, float3 dir,
               float extra_spread, const AmmoTypeDefinition* ammo) {
    const float mv = ammo ? ammo->muzzle_velocity_ms : 370.f;
    const float mass = ammo ? ammo->projectile_mass_kg : 0.009f;
    const float cd = ammo ? ammo->drag_coefficient : 0.00035f;
    const float pen = ammo ? ammo->penetration_mm_rha : 4.f;
    const float3 nd = float3_normalize_or(dir, float3{0.f, 0.f, 1.f});
    ProjectileComponent p{};
    p.weapon_entity_id   = weapon_index;
    p.ammo_type_id       = ammo_id;
    p.position           = muzzle;
    p.velocity           = float3_scale(nd, mv);
    p.mass_kg            = mass;
    p.drag_coefficient   = cd;
    p.penetration_power  = pen;
    p.remaining_energy_j = 0.5f * mass * mv * mv;
    p.time_alive_s       = 0.f;
    p.material_hit       = 0;
    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "projectile";
    (void)spawn_projectile(world, req, p);
    (void)extra_spread;
}

const AmmoTypeDefinition* find_ammo(World& world, u32 type_id) {
    for (Entity e : world.query<AmmoTypeDefinition>()) {
        const AmmoTypeDefinition* a = world.get<AmmoTypeDefinition>(e);
        if (a && a->type_id == type_id) {
            return a;
        }
    }
    return nullptr;
}

} // namespace

Entity instantiate_pistol(World& world, const InstantiationRequest& request,
                          const WeaponPistolComponent& pistol, float3 muzzle, float3 aim) {
    validate_instantiation(request);
    WeaponTriggerComponent trig{};
    trig.pull = 0;
    trig.owner_moving = 0;
    trig.muzzle_ws = muzzle;
    trig.aim_dir_ws = aim;
    trig.cooldown_s = 0.f;
    return world.instantiate(request, pistol, trig);
}

void UpdateWeaponSystem(World& world, float delta_time, CommandBuffer& cmd,
                        FrameAllocator& frame_alloc) {
    (void)cmd;
    ENGINE_ASSERT(delta_time >= 0.f, "weapon dt");
    const u32 max_entities = world.entity_count();
    Entity* pistols = frame_alloc.allocate_array<Entity>(max_entities);
    u32 n = 0;
    for (Entity e : world.query<WeaponPistolComponent, WeaponTriggerComponent>()) {
        if (n < max_entities) {
            pistols[n++] = e;
        }
    }

    for (u32 i = 0; i < n; ++i) {
        Entity e = pistols[i];
        WeaponPistolComponent* w = world.get<WeaponPistolComponent>(e);
        WeaponTriggerComponent* t = world.get<WeaponTriggerComponent>(e);
        ENGINE_ASSERT(w && t, "pistol snapshot stale");

        t->cooldown_s = max_of(0.f, t->cooldown_s - delta_time);
        if (w->is_reloading) {
            w->reload_timer_s -= delta_time;
            if (w->reload_timer_s <= 0.f) {
                w->is_reloading = false;
                w->current_ammo = w->max_ammo;
            }
        }
        w->current_recoil = max_of(0.f, w->current_recoil - w->recoil_recovery_rate * delta_time);

        const float min_interval = 60.0f / max_of(w->fire_rate_rpm, 1.f);
        if (t->pull && !w->is_reloading && w->current_ammo > 0 && t->cooldown_s <= 0.f) {
            const float moving = t->owner_moving ? w->accuracy_moving_penalty : 1.f;
            const float acc = clampf(w->accuracy_base * moving, 0.05f, 1.f);
            const float spread = w->spread_angle_rad * (2.0f - acc) + w->current_recoil * 0.01f;
            g_shot_salt += 17u + e.index();
            const float jx = (hash01(g_shot_salt) - 0.5f) * 2.f * spread;
            const float jy = (hash01(g_shot_salt * 3u) - 0.4f) * spread;
            float3 dir = t->aim_dir_ws;
            dir.x += jx;
            dir.y += jy;
            const AmmoTypeDefinition* ammo = find_ammo(world, w->ammo_type_id);
            spawn_one(world, e.index(), w->ammo_type_id, t->muzzle_ws, dir, spread, ammo);
            --w->current_ammo;
            w->current_recoil += w->recoil_vertical;
            w->current_recoil += (hash01(g_shot_salt + 9) - 0.5f) * w->recoil_horizontal;
            t->cooldown_s = min_interval;
            t->pull = 0;
        } else if (t->pull && w->current_ammo == 0 && !w->is_reloading) {
            w->is_reloading = true;
            w->reload_timer_s = w->reload_time_s;
            t->pull = 0;
        } else {
            t->pull = 0;
        }
    }

    Entity* rifles = frame_alloc.allocate_array<Entity>(max_entities);
    u32 nr = 0;
    for (Entity e : world.query<WeaponRifleComponent, WeaponTriggerComponent>()) {
        if (nr < max_entities) {
            rifles[nr++] = e;
        }
    }
    for (u32 i = 0; i < nr; ++i) {
        WeaponRifleComponent* w = world.get<WeaponRifleComponent>(rifles[i]);
        WeaponTriggerComponent* t = world.get<WeaponTriggerComponent>(rifles[i]);
        if (!w || !t) {
            continue;
        }
        t->cooldown_s = max_of(0.f, t->cooldown_s - delta_time);
        w->current_recoil = max_of(0.f, w->current_recoil - w->recoil_recovery_rate * delta_time);
        if (w->is_reloading) {
            w->reload_timer_s -= delta_time;
            if (w->reload_timer_s <= 0.f) {
                w->is_reloading = false;
                w->current_ammo = w->max_ammo;
            }
        }
        const float min_interval = 60.0f / max_of(w->fire_rate_rpm, 1.f);
        if (t->pull && !w->is_reloading && w->current_ammo > 0 && t->cooldown_s <= 0.f) {
            const AmmoTypeDefinition* ammo = find_ammo(world, w->ammo_type_id);
            spawn_one(world, rifles[i].index(), w->ammo_type_id, t->muzzle_ws, t->aim_dir_ws, 0.f, ammo);
            --w->current_ammo;
            w->current_recoil += w->recoil_vertical;
            t->cooldown_s = min_interval;
            t->pull = 0;
        } else {
            t->pull = 0;
        }
    }

    Entity* shotguns = frame_alloc.allocate_array<Entity>(max_entities);
    u32 ns = 0;
    for (Entity e : world.query<WeaponShotgunComponent, WeaponTriggerComponent>()) {
        if (ns < max_entities) {
            shotguns[ns++] = e;
        }
    }
    for (u32 i = 0; i < ns; ++i) {
        WeaponShotgunComponent* w = world.get<WeaponShotgunComponent>(shotguns[i]);
        WeaponTriggerComponent* t = world.get<WeaponTriggerComponent>(shotguns[i]);
        if (!w || !t) {
            continue;
        }
        t->cooldown_s = max_of(0.f, t->cooldown_s - delta_time);
        if (w->is_pumping) {
            w->pump_timer_s -= delta_time;
            if (w->pump_timer_s <= 0.f) {
                w->is_pumping = false;
            }
        }
        const float min_interval = 60.0f / max_of(w->fire_rate_rpm, 1.f);
        if (t->pull && !w->is_pumping && w->current_ammo > 0 && t->cooldown_s <= 0.f) {
            const AmmoTypeDefinition* ammo = find_ammo(world, w->ammo_type_id);
            const u32 pellets = static_cast<u32>(clampf(w->pellets_per_shot, 1.f, 12.f));
            for (u32 k = 0; k < pellets; ++k) {
                g_shot_salt += 31u;
                float3 dir = t->aim_dir_ws;
                dir.x += (hash01(g_shot_salt) - 0.5f) * w->pellet_spread_rad;
                dir.y += (hash01(g_shot_salt * 7u) - 0.5f) * w->pellet_spread_rad;
                spawn_one(world, shotguns[i].index(), w->ammo_type_id, t->muzzle_ws, dir, 0.f, ammo);
            }
            --w->current_ammo;
            w->is_pumping = true;
            w->pump_timer_s = min_interval;
            t->cooldown_s = min_interval;
            t->pull = 0;
        } else {
            t->pull = 0;
        }
    }
}

} // namespace engine
