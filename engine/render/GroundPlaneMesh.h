#pragma once

#include "core/Types.h"
#include "render/CubeMesh.h"

namespace engine {

// 100 m x 100 m concrete slab on Y=0, +Y up. UVs tile so grout lines have scale.
inline constexpr u32 kGroundVertexCount = 4;
inline constexpr u32 kGroundIndexCount  = 6;

inline constexpr CubeVertex kGroundVertices[kGroundVertexCount] = {
    {{-50.f, 0.f, -50.f}, {0.f, 1.f, 0.f}, {0.f,  0.f}},
    {{ 50.f, 0.f, -50.f}, {0.f, 1.f, 0.f}, {50.f, 0.f}},
    {{ 50.f, 0.f,  50.f}, {0.f, 1.f, 0.f}, {50.f, 50.f}},
    {{-50.f, 0.f,  50.f}, {0.f, 1.f, 0.f}, {0.f,  50.f}},
};

inline constexpr u16 kGroundIndices[kGroundIndexCount] = {0, 1, 2, 0, 2, 3};

} // namespace engine
