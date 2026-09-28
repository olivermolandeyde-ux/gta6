#include "gameplay/PlayerController.h"

#include "core/Assert.h"
#include "ecs/World.h"

namespace engine {

Entity instantiate_player(World& world, const InstantiationRequest& request,
                          const PlayerStateComponent& state) {
    validate_instantiation(request);
    return world.instantiate(request, state);
}

void enqueue_damage(World& world, const DamageEventComponent& evt) {
    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "damage_event";
    (void)world.instantiate(req, evt);
}

void UpdatePlayerController(World& world, float delta_time, CommandBuffer& cmd,
                            FrameAllocator& frame_alloc) {
    ENGINE_ASSERT(delta_time >= 0.f, "player dt");
    const u32 max_entities = world.entity_count();
    Entity* events = frame_alloc.allocate_array<Entity>(max_entities);
    u32 n = 0;
    for (Entity e : world.query<DamageEventComponent>()) {
        if (n < max_entities) {
            events[n++] = e;
        }
    }

    for (u32 i = 0; i < n; ++i) {
        Entity e = events[i];
        const DamageEventComponent* d = world.get<DamageEventComponent>(e);
        ENGINE_ASSERT(d != nullptr, "damage stale");
        for (Entity p : world.query<PlayerStateComponent>()) {
            if (p.index() != d->target_entity_id) {
                continue;
            }
            PlayerStateComponent* ps = world.get<PlayerStateComponent>(p);
            if (!ps) {
                continue;
            }
            float remaining = d->damage_amount;
            if (d->body_part == 1) {
                remaining *= 2.0f; // head
            } else if (d->body_part == 2) {
                remaining *= 0.65f;
            }
            if (d->damage_type == 0) {
                const float absorbed = min_of(ps->armor, remaining);
                ps->armor -= absorbed;
                remaining -= absorbed;
            }
            ps->health = max_of(0.f, ps->health - remaining);
        }
        cmd.destroy_entity(e);
    }

    for (Entity p : world.query<PlayerStateComponent>()) {
        PlayerStateComponent* ps = world.get<PlayerStateComponent>(p);
        ENGINE_ASSERT(ps != nullptr, "player stale");
        if (ps->stamina < ps->max_stamina) {
            ps->stamina = min_of(ps->max_stamina, ps->stamina + 8.0f * delta_time);
        }
        ps->wanted_level = max_of(0.f, ps->wanted_level - 0.01f * delta_time);
    }
}

} // namespace engine
