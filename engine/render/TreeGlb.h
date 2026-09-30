#pragma once

#include "core/Types.h"

namespace engine {

inline constexpr u32 kTreePrimCap     = 128;
inline constexpr u32 kTreeKindCount   = 3;
inline constexpr u32 kTreeInstanceCap = 300;
inline constexpr u32 kTreeSpawnCap    = 280;
inline constexpr u32 kLampKindCount   = 2;
inline constexpr u32 kLampSpawnCap    = 280;
inline constexpr u32 kSuvSpawnCap     = 30;

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
};

struct TreeGlb {
    TreePrim prims[kTreePrimCap];
    u32      nprims;
    u32      nverts;
    unsigned instance_vbo;
    u32      instance_count;
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

} // namespace engine
