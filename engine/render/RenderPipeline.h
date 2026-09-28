#pragma once
#include "ecs/World.h"
#include "ecs/CommandBuffer.h"
#include "memory/FrameAllocator.h"

namespace engine {

struct RenderableComponent {
    u32 mesh_id;       // Handle to GPU vertex buffer
    u32 material_id;   // Handle to PBR material instance
    u32 transform_id;  // Handle to TransformComponent
};

struct TransformComponent {
    float position[3];
    float padding;
    float rotation[4]; // Quaternion (x, y, z, w)
    float scale[3];
    float padding2;
};

void RenderGeometryPass(World& world, CommandBuffer& cmd, FrameAllocator& frame_alloc);

} // namespace engine
