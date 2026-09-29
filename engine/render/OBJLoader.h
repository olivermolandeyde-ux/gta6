#pragma once

#include "core/Types.h"
#include "render/TreeGlb.h"

namespace engine {

// Wavefront OBJ + MTL + PNG for the three sidewalk trees. Not a generic Model.
[[nodiscard]] bool load_tree_obj(const char* path, TreeGlb* out);
[[nodiscard]] bool find_and_load_tree_obj(const char* filename, TreeGlb* out);
void               log_tree_obj_files();

} // namespace engine
