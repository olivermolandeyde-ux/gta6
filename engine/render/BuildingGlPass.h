#pragma once

#include "core/Types.h"

namespace engine {

class World;

// Dedicated OpenGL building / street / lamp pass. Not a generic Renderer.
struct BuildingGlPass {
    unsigned building_prog;
    unsigned street_prog;
    unsigned cube_vao;
    unsigned cube_vbo;
    unsigned cube_ibo;
    unsigned street_vao;
    unsigned street_vbo;
    unsigned cloud_prog;
    unsigned street_count;
    u32      num_buildings;
    bool     ok;

    bool init();
    void buildMesh(World& world);
    void draw(World& world, float3 camera_pos, float3 camera_target, int width, int height,
              float time_of_day, float3 sun_dir, float clock_s);
    void shutdown();
};

} // namespace engine
