#include "gameplay/InventorySystem.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"

namespace engine {

namespace {

constexpr u8 kGridW = 8;
constexpr u8 kGridH = 5;

[[nodiscard]] bool rects_overlap(u8 ax, u8 ay, u8 aw, u8 ah, u8 bx, u8 by, u8 bw, u8 bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

} // namespace

Entity instantiate_player_inventory(World& world, const InstantiationRequest& request,
                                    const PlayerInventoryComponent& inv) {
    validate_instantiation(request);
    return world.instantiate(request, inv);
}

Entity instantiate_item_definition(World& world, const InstantiationRequest& request,
                                   const ItemDefinition& def) {
    validate_instantiation(request);
    return world.instantiate(request, def);
}

bool inventory_try_place(World& world, Entity inventory, const ItemDefinition& def, u32 quantity,
                         FrameAllocator& frame_alloc) {
    PlayerInventoryComponent* inv = world.get<PlayerInventoryComponent>(inventory);
    if (!inv) {
        return false;
    }
    const float add_w = def.weight_kg * static_cast<float>(quantity);
    if (inv->current_weight_kg + add_w > inv->max_weight_kg + 1e-4f) {
        return false;
    }

    const u32 max_entities = world.entity_count();
    Entity* slots = frame_alloc.allocate_array<Entity>(max_entities);
    u32 n = 0;
    for (Entity s : world.query<InventorySlotComponent>()) {
        const InventorySlotComponent* slot = world.get<InventorySlotComponent>(s);
        if (!slot) {
            continue;
        }
        if (n < max_entities) {
            slots[n++] = s;
        }
    }

    for (u8 y = 0; y + def.grid_h <= kGridH; ++y) {
        for (u8 x = 0; x + def.grid_w <= kGridW; ++x) {
            bool blocked = false;
            for (u32 i = 0; i < n; ++i) {
                const InventorySlotComponent* slot = world.get<InventorySlotComponent>(slots[i]);
                if (!slot) {
                    continue;
                }
                if (rects_overlap(x, y, def.grid_w, def.grid_h, slot->grid_x, slot->grid_y,
                                  slot->grid_w, slot->grid_h)) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) {
                continue;
            }
            InventorySlotComponent placed{};
            placed.item_type_id = def.type_id;
            placed.quantity     = quantity;
            placed.max_stack    = def.max_stack;
            placed.grid_x       = x;
            placed.grid_y       = y;
            placed.grid_w       = def.grid_w;
            placed.grid_h       = def.grid_h;
            placed.is_equipped  = false;
            placed.durability   = 100;
            InstantiationRequest req{};
            req.domain      = InstantiationDomain::PersistentWorld;
            req.debug_label = "inventory_slot";
            (void)world.instantiate(req, placed);
            inv->slot_count += 1;
            inv->current_weight_kg += add_w;
            return true;
        }
    }
    return false;
}

void UpdateInventorySystem(World& world, float delta_time, CommandBuffer& cmd,
                           FrameAllocator& frame_alloc) {
    (void)delta_time;
    (void)cmd;
    const u32 max_entities = world.entity_count();
    Entity* slots = frame_alloc.allocate_array<Entity>(max_entities);
    u32 n = 0;
    for (Entity s : world.query<InventorySlotComponent>()) {
        if (n < max_entities) {
            slots[n++] = s;
        }
    }

    for (Entity e : world.query<PlayerInventoryComponent>()) {
        PlayerInventoryComponent* inv = world.get<PlayerInventoryComponent>(e);
        ENGINE_ASSERT(inv != nullptr, "inv stale");
        float weight = 0.f;
        u32 count = 0;
        for (u32 i = 0; i < n; ++i) {
            const InventorySlotComponent* slot = world.get<InventorySlotComponent>(slots[i]);
            if (!slot) {
                continue;
            }
            ++count;
            float item_w = 0.1f;
            for (Entity d : world.query<ItemDefinition>()) {
                const ItemDefinition* def = world.get<ItemDefinition>(d);
                if (def && def->type_id == slot->item_type_id) {
                    item_w = def->weight_kg;
                    break;
                }
            }
            weight += item_w * static_cast<float>(slot->quantity);
        }
        inv->slot_count = count;
        inv->current_weight_kg = weight;

        const float ratio = clampf(weight / max_of(inv->max_weight_kg, 0.001f), 0.f, 1.f);
        const float speed_penalty = 1.0f - 0.4f * ratio;
        for (Entity p : world.query<PlayerStateComponent>()) {
            if (p.index() == inv->owner_entity_id) {
                PlayerStateComponent* ps = world.get<PlayerStateComponent>(p);
                if (ps) {
                    ps->move_speed_penalty = speed_penalty;
                }
            }
        }
    }
}

} // namespace engine
