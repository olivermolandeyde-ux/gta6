#pragma once

#include "core/Types.h"

namespace engine {

inline constexpr u32 kTreePrimCap     = 128;
inline constexpr u32 kTreeKindCount   = 3;
inline constexpr u32 kTreeInstanceCap = 300;
inline constexpr u32 kTreeSpawnCap    = 280;
inline constexpr u32 kLampKindCount   = 2;
inline constexpr u32 kLampSpawnCap    = 280;
inline constexpr u32 kSuvSpawnCap      = 30;
inline constexpr u32 kCorollaSpawnCap = 30;
inline constexpr u32 kSportsSpawnCap  = 10;

// One glTF primitive uploaded to GL. Not a generic Model / Mesh.
struct TreePrim {
    unsigned vao;
    unsigned vbo;
    unsigned ibo;
    unsigned tex;
    unsigned tex_emit;
    unsigned nidx;
    int      alpha_mask;
    float    cutoff;
    u32      tex_w;
    u32      tex_h;
    int      has_alpha;
    int      gl_mode; // glTF 0 points, 1 lines, 4 triangles, …
    int      has_color0;
    float    max_ext;  // longest AABB axis of this prim
    int      suv_part; // 0 none, 1 glass, 2 tire, 3 rim
    // Wheels: model-space disc geometry, so the wheel can spin with the car (see
    // CarWheelFit.h). count 0 means this primitive is not a wheel and never rotates.
    int      wheel_count;
    int      wheel_axis;        // model-space axle axis index
    float    wheel_center[4][3]; // model space
    float    wheel_radius[4];    // model units
    float    wheel_roll;         // +-1, forward-travel spin direction for this model
};

struct TreeGlb {
    TreePrim prims[kTreePrimCap];
    u32      nprims;
    u32      nverts;
    unsigned instance_vbo;
    unsigned instance_wheel_vbo; // one float per instance: wheel spin angle, radians
    u32      instance_count;
    u32      wheel_prim_count;   // primitives that carry rotating wheels
    u32      wheel_count;        // wheels across those primitives (4 for a whole car)
    float    wheel_radius;       // model units, the tyre radius used for the spin rate
    float    ymin;
    float    ymax;
    float    xmin;
    float    xmax;
    float    zmin;
    float    zmax;
    int      z_up;
    char     label[32];
};

[[nodiscard]] bool load_tree_glb(const char* path, TreeGlb* out);
// Searches cwd, LEONIDA_SOURCE_DIR, exe-relative, and build/assets for name.
[[nodiscard]] bool find_and_load_tree_glb(const char* filename, TreeGlb* out);
void               tree_glb_shutdown(TreeGlb* t);
void               tree_glb_set_instances(TreeGlb* t, const float* mats16, u32 count);
// Per-instance wheel spin angles in radians, already scaled to this model's wheel radius.
void               tree_glb_set_wheel_angles(TreeGlb* t, const float* angles, u32 count);

} // namespace engine
