#pragma once

#include "core/Types.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

inline constexpr u32   kCityBlocks      = 20;
inline constexpr float kCityStreetWidth = 24.f;
inline constexpr float kCityBlockPitch  = 96.f;
inline constexpr float kCityExtentM     = 1920.f;
inline constexpr float kCityPlateauY    = 16.f;
inline constexpr float kCityCenterM     = 960.f;

// 0 downtown glass, 1 residential masonry, 2 industrial shed.
inline constexpr u32 kDistrictDowntown    = 0;
inline constexpr u32 kDistrictResidential = 1;
inline constexpr u32 kDistrictIndustrial  = 2;

// Lot / facade massing. Not a generic Prop / Actor.
struct BuildingComponent {
    u32    building_id;
    float3 position; // world-space, Y = city plateau (grounded)
    float  width;
    float  depth;
    float  height;
    u32    num_floors;
    u32    window_rows;
    u32    window_cols;
    float3 albedo_color;
    float  roughness;
    u32    district;
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

    void generateStreets(World& world, float3 city_center, float city_radius);
    void generateCity(World& world, float3 city_center, float city_radius, u32 num_buildings);
};

// Matches shaders/terrain.vert: natural fbm, then flatten inside the city square.
[[nodiscard]] float city_natural_terrain_height(float x, float z);
[[nodiscard]] float city_gpu_terrain_height(float x, float z);
[[nodiscard]] float city_urban_mask(float x, float z);

} // namespace engine
