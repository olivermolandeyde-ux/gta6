#include "mission/ObjectiveTracker.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/InventorySystem.h"
#include "gameplay/PlayerController.h"
#include "gameplay/VehicleInteraction.h"
#include "gameplay/WeaponSystem.h"
#include "mission/MissionSystem.h"

namespace engine {

namespace {

constexpr u32 kSnapMax = 256;

[[nodiscard]] bool player_pose(World& world, float3* pos, float3* fwd, u32* weapon_id, u32* vehicle_id,
                               float* health) {
    for (Entity e : world.query<PlayerStateComponent>()) {
        const PlayerStateComponent* p = world.get<PlayerStateComponent>(e);
        if (!p) {
            continue;
        }
        if (pos) {
            *pos = p->camera_position;
        }
        if (fwd) {
            *fwd = p->camera_forward;
        }
        if (weapon_id) {
            *weapon_id = p->current_weapon_entity_id;
        }
        if (vehicle_id) {
            *vehicle_id = p->current_vehicle_entity_id;
        }
        if (health) {
            *health = p->health;
        }
        return true;
    }
    return false;
}

[[nodiscard]] bool find_entity_pose(World& world, u32 index, float3* out) {
    for (Entity e : world.query<StoreClerkPoseComponent>()) {
        if (e.index() != index) {
            continue;
        }
        const StoreClerkPoseComponent* c = world.get<StoreClerkPoseComponent>(e);
        if (c && out) {
            *out = c->position_ws;
        }
        return c != nullptr;
    }
    for (Entity e : world.query<MissionGiverPoseComponent>()) {
        if (e.index() != index) {
            continue;
        }
        const MissionGiverPoseComponent* c = world.get<MissionGiverPoseComponent>(e);
        if (c && out) {
            *out = c->position_ws;
        }
        return c != nullptr;
    }
    return false;
}

[[nodiscard]] float flesh_health(World& world, u32 index) {
    for (Entity e : world.query<FleshHealthComponent>()) {
        if (e.index() != index) {
            continue;
        }
        const FleshHealthComponent* h = world.get<FleshHealthComponent>(e);
        return h ? h->health : 0.f;
    }
    return 0.f;
}

[[nodiscard]] bool inventory_has(World& world, u32 item_type, u32 qty) {
    u32 have = 0;
    for (Entity e : world.query<InventorySlotComponent>()) {
        const InventorySlotComponent* s = world.get<InventorySlotComponent>(e);
        if (s && s->item_type_id == item_type) {
            have += s->quantity;
        }
    }
    return have >= qty;
}

[[nodiscard]] bool vehicle_entered(World& world, u32 player_vehicle_id) {
    if (player_vehicle_id != 0) {
        return true;
    }
    for (Entity e : world.query<VehicleInteractionComponent>()) {
        const VehicleInteractionComponent* ix = world.get<VehicleInteractionComponent>(e);
        if (ix && ix->camera_handle == 1) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool pistol_aimed_at(World& world, float3 player_pos, u32 weapon_id, float3 target,
                                   float aim_dot_min, float max_range) {
    if (weapon_id == 0) {
        return false;
    }
    bool have_pistol = false;
    float3 aim{0.f, 0.f, 1.f};
    for (Entity e : world.query<WeaponPistolComponent, WeaponTriggerComponent>()) {
        if (e.index() != weapon_id) {
            continue;
        }
        const WeaponTriggerComponent* t = world.get<WeaponTriggerComponent>(e);
        if (t) {
            aim = t->aim_dir_ws;
        }
        have_pistol = true;
        break;
    }
    if (!have_pistol) {
        return false;
    }
    const float3 delta = float3_sub(target, player_pos);
    const float dist = float3_length(delta);
    if (dist > max_range) {
        return false;
    }
    const float3 dir = float3_normalize_or(delta, float3{0.f, 0.f, 1.f});
    const float3 nd  = float3_normalize_or(aim, float3{0.f, 0.f, 1.f});
    return float3_dot(dir, nd) >= aim_dot_min;
}

void mark_objective(World& world, u32 mission_id, u32 index) {
    for (Entity e : world.query<MissionObjectiveComponent>()) {
        MissionObjectiveComponent* o = world.get<MissionObjectiveComponent>(e);
        if (!o || o->mission_id != mission_id || o->objective_index != index) {
            continue;
        }
        if (!o->is_completed) {
            o->is_completed = true;
            o->current_count = o->required_count == 0 ? 1 : o->required_count;
            for (Entity t : world.query<MissionTelemetryComponent>()) {
                MissionTelemetryComponent* tel = world.get<MissionTelemetryComponent>(t);
                if (tel) {
                    tel->objectives_completed += 1;
                }
                break;
            }
        }
    }
    for (Entity e : world.query<GotoObjectiveComponent>()) {
        GotoObjectiveComponent* g = world.get<GotoObjectiveComponent>(e);
        if (g && g->mission_id == mission_id && g->objective_index == index) {
            g->is_completed = true;
        }
    }
    for (Entity e : world.query<KillObjectiveComponent>()) {
        KillObjectiveComponent* g = world.get<KillObjectiveComponent>(e);
        if (g && g->mission_id == mission_id && g->objective_index == index) {
            g->is_completed = true;
        }
    }
    for (Entity e : world.query<CollectObjectiveComponent>()) {
        CollectObjectiveComponent* g = world.get<CollectObjectiveComponent>(e);
        if (g && g->mission_id == mission_id && g->objective_index == index) {
            g->is_completed = true;
        }
    }
    for (Entity e : world.query<DeliverObjectiveComponent>()) {
        DeliverObjectiveComponent* g = world.get<DeliverObjectiveComponent>(e);
        if (g && g->mission_id == mission_id && g->objective_index == index) {
            g->is_completed = true;
        }
    }
    for (Entity e : world.query<EscortObjectiveComponent>()) {
        EscortObjectiveComponent* g = world.get<EscortObjectiveComponent>(e);
        if (g && g->mission_id == mission_id && g->objective_index == index) {
            g->is_completed = true;
        }
    }
    for (Entity e : world.query<StealVehicleObjectiveComponent>()) {
        StealVehicleObjectiveComponent* g = world.get<StealVehicleObjectiveComponent>(e);
        if (g && g->mission_id == mission_id && g->objective_index == index) {
            g->is_completed = true;
        }
    }
    for (Entity e : world.query<DestroyObjectiveComponent>()) {
        DestroyObjectiveComponent* g = world.get<DestroyObjectiveComponent>(e);
        if (g && g->mission_id == mission_id && g->objective_index == index) {
            g->is_completed = true;
        }
    }
    for (Entity e : world.query<ThreatenAimObjectiveComponent>()) {
        ThreatenAimObjectiveComponent* g = world.get<ThreatenAimObjectiveComponent>(e);
        if (g && g->mission_id == mission_id && g->objective_index == index) {
            g->is_completed = true;
        }
    }
}

[[nodiscard]] bool objective_is_current(World& world, u32 mission_id, u32 index) {
    for (Entity e : world.query<MissionStateComponent>()) {
        const MissionStateComponent* s = world.get<MissionStateComponent>(e);
        if (s && s->mission_id == mission_id && s->current_state == kMissionActive
            && s->current_objective_index == index) {
            return true;
        }
    }
    return false;
}

} // namespace

Entity instantiate_objective_tracker(World& world, const InstantiationRequest& request,
                                     const ObjectiveTrackerComponent& tracker) {
    validate_instantiation(request);
    return world.instantiate(request, tracker);
}

void UpdateObjectiveTrackerSystem(World& world, float delta_time, CommandBuffer& cmd) {
    (void)delta_time;
    (void)cmd;
    float3 player_pos{};
    float3 player_fwd{};
    u32 weapon_id = 0;
    u32 vehicle_id = 0;
    float health = 100.f;
    if (!player_pose(world, &player_pos, &player_fwd, &weapon_id, &vehicle_id, &health)) {
        return;
    }

    Entity trackers[kSnapMax];
    u32 n = 0;
    for (Entity e : world.query<ObjectiveTrackerComponent>()) {
        if (n < kSnapMax) {
            trackers[n++] = e;
        }
    }

    for (u32 i = 0; i < n; ++i) {
        ObjectiveTrackerComponent* tr = world.get<ObjectiveTrackerComponent>(trackers[i]);
        ENGINE_ASSERT(tr != nullptr, "tracker stale");
        if (tr->is_triggered) {
            continue;
        }
        if (!objective_is_current(world, tr->mission_id, tr->objective_index)) {
            continue;
        }

        bool hit = false;
        switch (tr->tracker_type) {
        case 0: {
            float3 target = tr->target_location;
            if (tr->target_entity_id != 0) {
                (void)find_entity_pose(world, tr->target_entity_id, &target);
            }
            const float dist = float3_length(float3_sub(player_pos, target));
            hit = dist <= tr->trigger_distance_m;
            break;
        }
        case 1: {
            hit = flesh_health(world, tr->target_entity_id) <= 0.f;
            break;
        }
        case 2: {
            hit = inventory_has(world, tr->target_entity_id, 1);
            break;
        }
        case 3: {
            hit = vehicle_entered(world, vehicle_id);
            break;
        }
        case 4: {
            const float dist = float3_length(float3_sub(player_pos, tr->target_location));
            hit = dist <= tr->trigger_distance_m;
            break;
        }
        default:
            break;
        }

        // Convenience-store threaten: current kill/threaten objective also needs aim.
        for (Entity te : world.query<ThreatenAimObjectiveComponent>()) {
            const ThreatenAimObjectiveComponent* th = world.get<ThreatenAimObjectiveComponent>(te);
            if (!th || th->mission_id != tr->mission_id || th->objective_index != tr->objective_index) {
                continue;
            }
            float3 clerk = tr->target_location;
            (void)find_entity_pose(world, th->clerk_entity_id, &clerk);
            hit = pistol_aimed_at(world, player_pos, weapon_id, clerk, th->aim_dot_min, th->max_range_m);
        }

        // Escape: location tracker with large minimum distance encoded as negative trigger.
        if (tr->trigger_distance_m < 0.f && tr->tracker_type == 4) {
            const float dist = float3_length(float3_sub(player_pos, tr->target_location));
            hit = dist >= -tr->trigger_distance_m;
        }

        if (hit) {
            tr->is_triggered = true;
            mark_objective(world, tr->mission_id, tr->objective_index);
        }
    }

    // Dedicated objective types not covered solely by tracker_type.
    for (Entity e : world.query<GotoObjectiveComponent>()) {
        GotoObjectiveComponent* g = world.get<GotoObjectiveComponent>(e);
        if (!g || g->is_completed || !objective_is_current(world, g->mission_id, g->objective_index)) {
            continue;
        }
        if (float3_length(float3_sub(player_pos, g->target_location)) <= g->radius_m) {
            g->is_completed = true;
            mark_objective(world, g->mission_id, g->objective_index);
        }
    }
    for (Entity e : world.query<KillObjectiveComponent>()) {
        KillObjectiveComponent* g = world.get<KillObjectiveComponent>(e);
        if (!g || g->is_completed || !objective_is_current(world, g->mission_id, g->objective_index)) {
            continue;
        }
        if (flesh_health(world, g->target_entity_id) <= 0.f) {
            g->current_count = g->required_count;
            g->is_completed  = true;
            mark_objective(world, g->mission_id, g->objective_index);
        }
    }
    for (Entity e : world.query<CollectObjectiveComponent>()) {
        CollectObjectiveComponent* g = world.get<CollectObjectiveComponent>(e);
        if (!g || g->is_completed || !objective_is_current(world, g->mission_id, g->objective_index)) {
            continue;
        }
        if (inventory_has(world, g->item_type_id, g->required_count)) {
            g->is_completed = true;
            mark_objective(world, g->mission_id, g->objective_index);
        }
    }
    for (Entity e : world.query<DeliverObjectiveComponent>()) {
        DeliverObjectiveComponent* g = world.get<DeliverObjectiveComponent>(e);
        if (!g || g->is_completed || !objective_is_current(world, g->mission_id, g->objective_index)) {
            continue;
        }
        if (inventory_has(world, g->item_type_id, 1)
            && float3_length(float3_sub(player_pos, g->dropoff_ws)) <= g->radius_m) {
            g->is_completed = true;
            mark_objective(world, g->mission_id, g->objective_index);
        }
    }
    for (Entity e : world.query<StealVehicleObjectiveComponent>()) {
        StealVehicleObjectiveComponent* g = world.get<StealVehicleObjectiveComponent>(e);
        if (!g || g->is_completed || !objective_is_current(world, g->mission_id, g->objective_index)) {
            continue;
        }
        if (vehicle_entered(world, vehicle_id)) {
            g->is_completed = true;
            mark_objective(world, g->mission_id, g->objective_index);
        }
    }
    for (Entity e : world.query<DestroyObjectiveComponent>()) {
        DestroyObjectiveComponent* g = world.get<DestroyObjectiveComponent>(e);
        if (!g || g->is_completed || !objective_is_current(world, g->mission_id, g->objective_index)) {
            continue;
        }
        if (flesh_health(world, g->target_entity_id) <= 0.f) {
            g->is_completed = true;
            mark_objective(world, g->mission_id, g->objective_index);
        }
    }
    for (Entity e : world.query<EscortObjectiveComponent>()) {
        EscortObjectiveComponent* g = world.get<EscortObjectiveComponent>(e);
        if (!g || g->is_completed || !objective_is_current(world, g->mission_id, g->objective_index)) {
            continue;
        }
        float3 escort_pos = player_pos;
        (void)find_entity_pose(world, g->escort_entity_id, &escort_pos);
        if (float3_length(float3_sub(escort_pos, g->destination_ws)) <= g->radius_m) {
            g->is_completed = true;
            mark_objective(world, g->mission_id, g->objective_index);
        }
    }
    for (Entity e : world.query<ThreatenAimObjectiveComponent>()) {
        ThreatenAimObjectiveComponent* g = world.get<ThreatenAimObjectiveComponent>(e);
        if (!g || g->is_completed || !objective_is_current(world, g->mission_id, g->objective_index)) {
            continue;
        }
        float3 clerk{};
        if (!find_entity_pose(world, g->clerk_entity_id, &clerk)) {
            continue;
        }
        if (pistol_aimed_at(world, player_pos, weapon_id, clerk, g->aim_dot_min, g->max_range_m)) {
            g->is_completed = true;
            mark_objective(world, g->mission_id, g->objective_index);
        }
    }
}

} // namespace engine
