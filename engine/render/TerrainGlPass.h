#pragma once

#include "core/Types.h"

namespace engine {

struct SkyComponent;

// OpenGL 3.3 terrain + sky pass. Dedicated, not a generic Renderer.
struct TerrainGlPass {
    unsigned sky_prog;
    unsigned terrain_prog;
    unsigned grid_vao;
    unsigned grid_vbo;
    unsigned grid_ibo;
    unsigned grid_index_count;
    int      width;
    int      height;
    float3   cameraPos;
    float3   cameraTarget;
    bool     ok;

    bool init(int width, int height);
    void beginFrame();
    void drawSky(const SkyComponent& sky);
    void drawTerrain();
    void shutdown();
};

} // namespace engine
