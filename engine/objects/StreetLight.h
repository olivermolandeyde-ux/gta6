#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

// Municipal luminaire. Not a generic Light / Prop. Flicker and voltage are
// owned here; the renderer only consumes `light_handle`.
struct StreetLightComponent {
    u32   light_handle;         // Handle to the rendering light source
    u32   power_grid_node_id;   // MunicipalPowerNodeComponent.node_id
    float flicker_probability;  // Per-second chance of a dropout, scaled by wear
    float current_voltage;      // 0 = dark, 1 = nominal 240 V per-unit
    float wear_factor;          // 0 = new lamp, 1 = dead ballast
};

// Feeder / transformer node that street lights subscribe to by id.
// This is the lighting grid, not a generic "Powerable" interface.
struct MunicipalPowerNodeComponent {
    u32   node_id;
    u32   feeder_id;
    float voltage_pu;     // per-unit, 1.0 = 240 V RMS
    float frequency_hz;
    u8    islanded;       // 1 = disconnected from plant
    u8    _pad[3];
};

// Deferred when voltage collapses. Presence means the luminaire is dark.
struct StreetLightOutageTag {
    u32  frame_darkened;
    u16  reason;          // 1 = grid, 2 = wear, 3 = flicker
    u16  _pad;
};

struct StreetLightSpawnDesc {
    u32   light_handle        = 0;
    u32   power_grid_node_id  = 0;
    float flicker_probability = 0.02f;
    float wear_factor         = 0.f;
};

struct MunicipalPowerNodeSpawnDesc {
    u32   node_id      = 0;
    u32   feeder_id    = 0;
    float voltage_pu   = 1.f;
    float frequency_hz = 60.f;
    bool  islanded     = false;
};

[[nodiscard]] Entity instantiate_street_light(World& world,
                                              const InstantiationRequest& request,
                                              const StreetLightSpawnDesc& desc);

[[nodiscard]] Entity instantiate_municipal_power_node(World& world,
                                                      const InstantiationRequest& request,
                                                      const MunicipalPowerNodeSpawnDesc& desc);

void UpdateStreetLightSystem(World& world, float delta_time, CommandBuffer& cmd);

} // namespace engine
