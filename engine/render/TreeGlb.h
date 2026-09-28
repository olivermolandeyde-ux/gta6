#pragma once

#include "core/Types.h"

namespace engine {

inline constexpr u32 kTreePrimCap     = 8;
inline constexpr u32 kTreeKindCount   = 3;
inline constexpr u32 kTreeInstanceCap = 8192;

// One glTF primitive uploaded to GL. Not a generic Model / Mesh.
struct TreePrim {
    unsigned vao;
    unsigned vbo;
    unsigned ibo;
    unsigned tex;
    unsigned nidx;
    int      alpha_mask;
    float    cutoff;
};

struct TreeGlb {
    TreePrim prims[kTreePrimCap];
    u32      nprims;
    u32      nverts;
    unsigned instance_vbo;
    u32      instance_count;
};

[[nodiscard]] bool load_tree_glb(const char* path, TreeGlb* out);
void               tree_glb_shutdown(TreeGlb* t);
void               tree_glb_set_instances(TreeGlb* t, const float* mats16, u32 count);

} // namespace engine
