#pragma once

#include "core/Types.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

// Lot / facade massing. Not a generic Prop / Actor.
struct BuildingComponent {
    u32    building_id;
    float3 position;
    float  width;
    float  depth;
    float  height;
    u32    num_floors;
    u32    window_rows;
    u32    window_cols;
    float3 albedo_color;
    float  roughness;
};

// Carriageway segment. Not a generic Path.
struct StreetComponent {
    u32    street_id;
    float3 start;
    float3 end;
    float  width;
    u8     has_sidewalk;
    u8     has_street_lights;
    u8     _pad[2];
};

struct CityGenerator {
    u32 buildings_spawned;
    u32 streets_spawned;
    u32 lights_spawned;
    u32 windows_spawned;

    // 10×10 block grid, one building per lot (100), no overlap.
    void generateStreets(World& world, float3 city_center, float city_radius);
    void generateCity(World& world, float3 city_center, float city_radius, u32 num_buildings);
};

[[nodiscard]] float city_gpu_terrain_height(float x, float z);

} // namespace engine
