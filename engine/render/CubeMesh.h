#pragma once

#include "core/Types.h"

namespace engine {

struct CubeVertex {
    float3 position;
    float3 normal;
    float2 uv;
};

inline constexpr u32 kCubeVertexCount = 24;
inline constexpr u32 kCubeIndexCount  = 36;

// 24 unique verts (4 per face) so normals/UVs are correct per face.
inline constexpr CubeVertex kCubeVertices[kCubeVertexCount] = {
    // +Z
    {{-0.5f, -0.5f, 0.5f}, {0.f, 0.f, 1.f}, {0.f, 0.f}},
    {{0.5f, -0.5f, 0.5f}, {0.f, 0.f, 1.f}, {1.f, 0.f}},
    {{0.5f, 0.5f, 0.5f}, {0.f, 0.f, 1.f}, {1.f, 1.f}},
    {{-0.5f, 0.5f, 0.5f}, {0.f, 0.f, 1.f}, {0.f, 1.f}},
    // -Z
    {{0.5f, -0.5f, -0.5f}, {0.f, 0.f, -1.f}, {0.f, 0.f}},
    {{-0.5f, -0.5f, -0.5f}, {0.f, 0.f, -1.f}, {1.f, 0.f}},
    {{-0.5f, 0.5f, -0.5f}, {0.f, 0.f, -1.f}, {1.f, 1.f}},
    {{0.5f, 0.5f, -0.5f}, {0.f, 0.f, -1.f}, {0.f, 1.f}},
    // +X
    {{0.5f, -0.5f, 0.5f}, {1.f, 0.f, 0.f}, {0.f, 0.f}},
    {{0.5f, -0.5f, -0.5f}, {1.f, 0.f, 0.f}, {1.f, 0.f}},
    {{0.5f, 0.5f, -0.5f}, {1.f, 0.f, 0.f}, {1.f, 1.f}},
    {{0.5f, 0.5f, 0.5f}, {1.f, 0.f, 0.f}, {0.f, 1.f}},
    // -X
    {{-0.5f, -0.5f, -0.5f}, {-1.f, 0.f, 0.f}, {0.f, 0.f}},
    {{-0.5f, -0.5f, 0.5f}, {-1.f, 0.f, 0.f}, {1.f, 0.f}},
    {{-0.5f, 0.5f, 0.5f}, {-1.f, 0.f, 0.f}, {1.f, 1.f}},
    {{-0.5f, 0.5f, -0.5f}, {-1.f, 0.f, 0.f}, {0.f, 1.f}},
    // +Y
    {{-0.5f, 0.5f, 0.5f}, {0.f, 1.f, 0.f}, {0.f, 0.f}},
    {{0.5f, 0.5f, 0.5f}, {0.f, 1.f, 0.f}, {1.f, 0.f}},
    {{0.5f, 0.5f, -0.5f}, {0.f, 1.f, 0.f}, {1.f, 1.f}},
    {{-0.5f, 0.5f, -0.5f}, {0.f, 1.f, 0.f}, {0.f, 1.f}},
    // -Y
    {{-0.5f, -0.5f, -0.5f}, {0.f, -1.f, 0.f}, {0.f, 0.f}},
    {{0.5f, -0.5f, -0.5f}, {0.f, -1.f, 0.f}, {1.f, 0.f}},
    {{0.5f, -0.5f, 0.5f}, {0.f, -1.f, 0.f}, {1.f, 1.f}},
    {{-0.5f, -0.5f, 0.5f}, {0.f, -1.f, 0.f}, {0.f, 1.f}},
};

inline constexpr u16 kCubeIndices[kCubeIndexCount] = {
    0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
    12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
};

} // namespace engine
