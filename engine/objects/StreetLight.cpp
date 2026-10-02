#include "objects/StreetLight.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "memory/FrameAllocator.h"
#include "memory/MemorySystem.h"

namespace engine {

namespace {

struct NodeSample {
    u32   node_id;
    float voltage_pu;
    u8    islanded;
};

[[nodiscard]] u32 mix32(u32 x) noexcept {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

[[nodiscard]] float u32_to_unit(u32 x) noexcept {
    return static_cast<float>(x >> 8) * (1.0f / static_cast<float>(0x00FFFFFFu));
}

[[nodiscard]] float lookup_node_voltage(const NodeSample* nodes, u32 node_count, u32 node_id,
                                        u8* islanded) {
    for (u32 i = 0; i < node_count; ++i) {
        if (nodes[i].node_id == node_id) {
            if (islanded) {
                *islanded = nodes[i].islanded;
            }
            return nodes[i].voltage_pu;
        }
    }
    if (islanded) {
        *islanded = 1;
    }
    return 0.f;
}

} // namespace

Entity instantiate_street_light(World& world, const InstantiationRequest& request,
                                const StreetLightSpawnDesc& desc) {
    validate_instantiation(request);
    ENGINE_ASSERT(desc.flicker_probability >= 0.f && desc.flicker_probability <= 1.f,
                  "flicker probability");
    ENGINE_ASSERT(desc.wear_factor >= 0.f && desc.wear_factor <= 1.f, "wear");

    StreetLightComponent lamp{};
    lamp.light_handle        = desc.light_handle;
    lamp.power_grid_node_id  = desc.power_grid_node_id;
    lamp.flicker_probability = desc.flicker_probability;
    lamp.current_voltage     = 0.f;
    lamp.wear_factor         = desc.wear_factor;
    return world.instantiate(request, lamp);
}

Entity instantiate_municipal_power_node(World& world, const InstantiationRequest& request,
                                        const MunicipalPowerNodeSpawnDesc& desc) {
    validate_instantiation(request);
    MunicipalPowerNodeComponent node{};
    node.node_id      = desc.node_id;
    node.feeder_id    = desc.feeder_id;
    node.voltage_pu   = desc.voltage_pu;
    node.frequency_hz = desc.frequency_hz;
    node.islanded     = desc.islanded ? 1 : 0;
    node._pad[0] = node._pad[1] = node._pad[2] = 0;
    return world.instantiate(request, node);
}

void UpdateStreetLightSystem(World& world, float delta_time, CommandBuffer& cmd) {
    ENGINE_ASSERT(delta_time >= 0.f && delta_time < 0.25f, "streetlight dt");

    FrameAllocator& frame = world.memory().frame();
    const u32 max_entities = world.entity_count();

    Entity* node_ents = frame.allocate_array<Entity>(max_entities);
    u32 node_ent_count = 0;
    for (Entity e : world.query<MunicipalPowerNodeComponent>()) {
        if (node_ent_count < max_entities) {
            node_ents[node_ent_count++] = e;
        }
    }

    NodeSample* nodes = frame.allocate_array<NodeSample>(node_ent_count);
    for (u32 i = 0; i < node_ent_count; ++i) {
        const MunicipalPowerNodeComponent* n = world.get<MunicipalPowerNodeComponent>(node_ents[i]);
        ENGINE_ASSERT(n != nullptr, "power node snapshot stale");
        nodes[i].node_id    = n->node_id;
        nodes[i].voltage_pu = n->islanded ? 0.f : n->voltage_pu;
        nodes[i].islanded   = n->islanded;
    }

    Entity* lamps = frame.allocate_array<Entity>(max_entities);
    u32 lamp_count = 0;
    for (Entity e : world.query<StreetLightComponent>()) {
        if (lamp_count < max_entities) {
            lamps[lamp_count++] = e;
        }
    }

    const u32 outage_id = world.registry().id_of<StreetLightOutageTag>();
    const u64 frame_i   = world.frame_index();

    for (u32 i = 0; i < lamp_count; ++i) {
        Entity e = lamps[i];
        StreetLightComponent* lamp = world.get<StreetLightComponent>(e);
        ENGINE_ASSERT(lamp != nullptr, "streetlight snapshot stale");

        u8 islanded = 0;
        const float grid_v =
            lookup_node_voltage(nodes, node_ent_count, lamp->power_grid_node_id, &islanded);

        const float wear = clampf(lamp->wear_factor, 0.f, 1.f);
        float voltage    = grid_v * (1.0f - 0.55f * wear * wear);

        // Sodium-age flicker: hashed per (entity, frame) so it is deterministic
        // across machines and never allocates a timeline (L3).
        const u32 h0 = mix32(e.index() ^ static_cast<u32>(frame_i * 747796405ull));
        const float roll = u32_to_unit(h0);
        const float p    = clampf(lamp->flicker_probability * (0.15f + 1.85f * wear), 0.f, 1.f);
        u16 reason       = 0;
        if (islanded || grid_v < 0.05f) {
            voltage = 0.f;
            reason  = 1;
        } else if (wear >= 0.98f) {
            voltage = 0.f;
            reason  = 2;
        } else if (roll < p * clampf(delta_time * 8.0f, 0.f, 1.f)) {
            const float depth = 0.05f + 0.25f * u32_to_unit(mix32(h0 ^ 0xA511E9B3u));
            voltage *= depth;
            reason = 3;
        }

        lamp->current_voltage = clampf(voltage, 0.f, 1.2f);

        const bool dark = lamp->current_voltage < 0.05f;
        const bool tagged = world.has<StreetLightOutageTag>(e);
        if (dark && !tagged) {
            StreetLightOutageTag tag{};
            tag.frame_darkened = static_cast<u32>(frame_i);
            tag.reason         = reason == 0 ? static_cast<u16>(1) : reason;
            tag._pad           = 0;
            cmd.add_component<StreetLightOutageTag>(e, outage_id, tag);
        } else if (!dark && tagged) {
            cmd.remove_component(e, outage_id);
        }

        lamp->wear_factor = clampf(wear + delta_time * (2.0e-6f + 8.0e-6f * lamp->current_voltage),
                                   0.f, 1.f);
    }
}

} // namespace engine
