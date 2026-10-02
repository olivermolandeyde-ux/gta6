#pragma once

#include "core/Types.h"
#include "render/TreeGlb.h"

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
    unsigned cyl_vao, cyl_ibo, cyl_count;
    unsigned sph_vao, sph_ibo, sph_count;
    unsigned cone_vao, cone_ibo, cone_count;
    unsigned tex_brick, tex_brick_n, tex_conc, tex_conc_n, tex_asph, tex_bark, tex_leaf;
    unsigned shadow_fbo, shadow_tex, shadow_prog;
    unsigned tree_prog, tree_shadow_prog;
    TreeGlb  tree_glb[kTreeKindCount];
    TreeGlb  sky_glb; // custom downtown skyscraper-2.glb
    TreeGlb  lamp_glb[kLampKindCount]; // klassisk, moderne
    TreeGlb  corolla_glb;              // low-poly_toyota_corolla_e80_sedan.glb parked on asphalt
    unsigned glow_prog, glow_vao, glow_vbo, glow_ibo, glow_ivbo, glow_nidx;
    u32      glow_count;
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
