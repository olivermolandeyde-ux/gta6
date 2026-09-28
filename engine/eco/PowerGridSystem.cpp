#include "eco/PowerGridSystem.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "memory/FrameAllocator.h"
#include "memory/MemorySystem.h"
#include "objects/StreetLight.h"

namespace engine {

Entity instantiate_power_grid_node(World& world, const InstantiationRequest& request,
                                   const PowerGridNodeComponent& node) {
    validate_instantiation(request);
    return world.instantiate(request, node);
}

void UpdatePowerGridSystem(World& world, const PowerGridEvent* events, u32 event_count,
                           CommandBuffer& cmd) {
    FrameAllocator& frame = world.memory().frame();
    const u32 max_entities = world.entity_count();

    Entity* node_ents = frame.allocate_array<Entity>(max_entities);
    u32 node_count = 0;
    for (Entity e : world.query<PowerGridNodeComponent>()) {
        if (node_count < max_entities) {
            node_ents[node_count++] = e;
        }
    }

    for (u32 i = 0; i < node_count; ++i) {
        PowerGridNodeComponent* node = world.get<PowerGridNodeComponent>(node_ents[i]);
        ENGINE_ASSERT(node != nullptr, "grid node snapshot stale");
        if (!events || event_count == 0) {
            continue;
        }
        for (u32 ev = 0; ev < event_count; ++ev) {
            if (events[ev].damaged_node_id == node->node_id) {
                node->is_operational = false;
                node->current_load   = 0.f;
            }
        }
    }

    Entity* lamps = frame.allocate_array<Entity>(max_entities);
    u32 lamp_count = 0;
    for (Entity e : world.query<StreetLightComponent>()) {
        if (lamp_count < max_entities) {
            lamps[lamp_count++] = e;
        }
    }

    const u32 outage_id = world.registry().id_of<StreetLightOutageTag>();
    const u32 frame_i   = static_cast<u32>(world.frame_index());

    for (u32 n = 0; n < node_count; ++n) {
        PowerGridNodeComponent* node = world.get<PowerGridNodeComponent>(node_ents[n]);
        if (!node || node->is_operational) {
            continue;
        }
        for (u32 L = 0; L < lamp_count; ++L) {
            Entity le = lamps[L];
            StreetLightComponent* lamp = world.get<StreetLightComponent>(le);
            ENGINE_ASSERT(lamp != nullptr, "lamp snapshot stale");

            bool wired = lamp->power_grid_node_id == node->node_id;
            if (!wired) {
                for (u32 c = 0; c < node->connected_light_count && c < 32; ++c) {
                    if (node->connected_lights[c] == le.index()) {
                        wired = true;
                        break;
                    }
                }
            }
            if (!wired) {
                continue;
            }

            lamp->current_voltage = 0.0f;
            if (!world.has<StreetLightOutageTag>(le)) {
                StreetLightOutageTag tag{};
                tag.frame_darkened = frame_i;
                tag.reason         = 1; // grid
                tag._pad           = 0;
                cmd.add_component<StreetLightOutageTag>(le, outage_id, tag);
            }
        }
    }
}

} // namespace engine
