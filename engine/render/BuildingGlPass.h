#pragma once

#include "core/Types.h"
#include "render/CarTraffic.h"
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
    unsigned lot_vao;   // static block-interior ground (one quad per block)
    unsigned lot_ibo;
    unsigned lot_count; // index count for the lot mesh
    unsigned ring_vao;  // static sidewalk ring (textured strips per block)
    unsigned ring_ibo;
    unsigned ring_count; // index count for the ring mesh
    unsigned tex_sidewalk; // assets/textures/sidewalk.png, or 0 = flat gray fallback
    unsigned tex_grass;    // assets/textures/grass.png, or 0 = flat green fallback
    unsigned tex_flat_n;   // 1x1 flat normal for textured non-facade draws
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
    TreeGlb  corolla_glb;              // low-poly_toyota_corolla_e80_sedan.glb, instanced per frame
    TreeGlb  sports_glb;               // low_poly_sports_car__game_ready_vehicle.glb
    TreeGlb  suv_glb;                  // low_poly_suv.glb — the third class in the mix
    // New GLB building types (3 variants each × 3 types = 9 models)
    TreeGlb  shop_glb[3];               // shop_small_variant_a/b/c.glb
    TreeGlb  apartment_glb[3];         // apartment_5story_variant_a/b/c.glb
    TreeGlb  warehouse_glb[3];         // warehouse_industrial_variant_a/b/c.glb
    // Instance matrices for new buildings
    float    shop_mats[kTreeInstanceCap * 16];
    float    apartment_mats[kTreeInstanceCap * 16];
    float    warehouse_mats[kTreeInstanceCap * 16];
    u32      shop_count;
    u32      apartment_count;
    u32      warehouse_count;
    // Street props (one GLB each, real-size, instanced on sidewalks)
    TreeGlb  hydrant_glb; // props/prop_hydrant_red.glb
    TreeGlb  bench_glb;   // props/prop_bench_wood.glb
    TreeGlb  bin_glb;     // props/prop_bin_metal.glb
    TreeGlb  mailbox_glb; // props/prop_mailbox_usps.glb
    TreeGlb  kiosk_glb;   // props/prop_newskiosk_metal.glb
    TreeGlb  atm_glb;     // props/prop_atm_wall.glb
    bool     verification_row; // sandbox --bldg-row: append 1 instance per variant in a lineup
    // Moving traffic: circuits, agent state, and the per-frame instance matrices.
    CarTraffic car_traffic;
    bool       car_traffic_live;
    float      car_clock;
    float      car_basis[kCarMeshCount][9]; // model → world basis, by mesh class
    float      car_scale[kCarMeshCount];
    float      car_y[kCarMeshCount];        // model origin height, so wheels sit on the road
    float      car_body_flip;   // extra yaw on every body; the sandbox F key toggles it
    float      car_suv_flip;    // extra yaw on the SUV alone; the sandbox G key toggles it
    float      car_yaw_off[kCarMeshCount]; // baked body yaw offset per model (nose direction)
    int        car_fwd_axis[kCarMeshCount];          // model-space length axis, from the AABB
    int        car_up_axis[kCarMeshCount];           // model-space height axis, from the AABB
    float      car_wheel_radius[kCarMeshCount];      // world metres, per model
    float      car_wheel_angles[kCarMeshCount][kCarAgentCap]; // radians, uploaded every frame
    float      car_mats[kCarMeshCount][kCarAgentCap * 16];
    unsigned glow_prog, glow_vao, glow_vbo, glow_ibo, glow_ivbo, glow_nidx;
    u32      glow_count;
    unsigned street_count;
    u32      num_buildings;
    bool     ok;

    bool init();
    void buildMesh(World& world);
    void update_car_instances(float clock_s);
    void draw(World& world, float3 camera_pos, float3 camera_target, int width, int height,
              float time_of_day, float3 sun_dir, float clock_s);
    void shutdown();
};

} // namespace engine
