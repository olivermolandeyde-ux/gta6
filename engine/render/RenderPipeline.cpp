#include "render/RenderPipeline.h"

namespace engine {

void RenderGeometryPass(World& world, CommandBuffer& cmd, FrameAllocator& frame_alloc) {
    // L6 compliance: Allocate query snapshot in frame arena to avoid structural change invalidation
    u32 max_entities = world.entity_count();
    auto* snapshot = frame_alloc.allocate_array<Entity>(max_entities);
    u32 count = 0;

    // Dense iteration over entities that have BOTH Transform and Renderable
    for (Entity e : world.query<TransformComponent, RenderableComponent>()) {
        if (count < max_entities) {
            snapshot[count++] = e;
        }
    }

    // Bind global render state (L1: No allocations here)
    cmd.bind_pipeline(PipelineType::ForwardPBR);
    cmd.bind_descriptor_set(GlobalLightingSet);

    for (u32 i = 0; i < count; ++i) {
        Entity e = snapshot[i];
        auto* transform = world.get<TransformComponent>(e);
        auto* renderable = world.get<RenderableComponent>(e);

        // Push constants are fastest for per-object draw calls
        cmd.push_constants("ObjectTransform", transform->position, sizeof(float) * 10);
        cmd.bind_material(renderable->material_id);
        cmd.draw_mesh(renderable->mesh_id);
    }
}

} // namespace engine
