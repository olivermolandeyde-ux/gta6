#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

struct InventorySlotComponent {
    u32  item_type_id;
    u32  quantity;
    u32  max_stack;
    u8   grid_x;
    u8   grid_y;
    u8   grid_w;
    u8   grid_h;
    bool is_equipped;
    u32  durability;
};

struct ItemDefinition {
    u32  type_id;
    char name[32];
    u8   grid_w;
    u8   grid_h;
    u32  max_stack;
    float weight_kg;
    u32  category; // 0=weapon, 1=ammo, 2=consumable, 3=key, 4=armor
    u32  icon_handle;
};

struct PlayerInventoryComponent {
    u32   owner_entity_id;
    u32   slot_count;
    float current_weight_kg;
    float max_weight_kg;
    u32   equipped_weapon_slot;
    u32   equipped_armor_slot;
};

[[nodiscard]] Entity instantiate_player_inventory(World& world, const InstantiationRequest& request,
                                                  const PlayerInventoryComponent& inv);

[[nodiscard]] Entity instantiate_item_definition(World& world, const InstantiationRequest& request,
                                                 const ItemDefinition& def);

[[nodiscard]] bool inventory_try_place(World& world, Entity inventory, const ItemDefinition& def,
                                       u32 quantity, FrameAllocator& frame_alloc);

void UpdateInventorySystem(World& world, float delta_time, CommandBuffer& cmd,
                           FrameAllocator& frame_alloc);

} // namespace engine
