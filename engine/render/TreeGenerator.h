#pragma once

#include "render/TreeGlb.h"

namespace engine {

// Procedural oak / pine / palm for sidewalks. Not a generic Mesh / Model factory.
struct TreeGenerator {
    static bool oak(TreeGlb* out);
    static bool pine(TreeGlb* out);
    static bool palm(TreeGlb* out);
};

} // namespace engine
