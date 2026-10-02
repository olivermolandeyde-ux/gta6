#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"
#include "memory/Allocator.h"

namespace engine {

class World;

enum class PipelineType : u32 {
    ForwardPBR = 0,
    SkinSSS    = 1,
};

inline constexpr u32 GlobalLightingSet = 0;

// Records structural changes while a query is in flight. Played back by
// World::playback() after the system returns. Payload bytes live in a
// linear bump region so add_component<T>(e, value) can memcpy the value
// without touching the CRT heap.
//
// GPU draw ops are a parallel stream (not played back as ECS mutations).
// RenderGeometryPass records into this stream after snapshotting entities
// from the FrameAllocator (L2 / L6).
class CommandBuffer {
public:
    enum class Op : u8 {
        DestroyEntity = 1,
        AddComponent  = 2,
        RemoveComponent = 3,
    };

    struct Command {
        Op     op            = Op::DestroyEntity;
        u32    component_id  = kInvalidIndex;
        u32    payload_off   = 0;
        u32    payload_size  = 0;
        Entity entity        = kNullEntity;
    };

    struct GpuCommand {
        enum class Kind : u8 {
            BindPipeline      = 1,
            BindDescriptorSet = 2,
            PushConstants     = 3,
            BindMaterial      = 4,
            DrawMesh          = 5,
        };
        Kind        kind         = Kind::BindPipeline;
        u32         handle       = 0;
        u32         extra        = 0;
        u32         payload_off  = 0;
        u32         payload_size = 0;
        const char* name         = nullptr;
    };

    void bind(void* command_memory, usize command_bytes, void* payload_memory, usize payload_bytes);
    void bind_gpu(void* gpu_memory, usize gpu_bytes, void* gpu_payload, usize gpu_payload_bytes);

    void destroy_entity(Entity entity);
    void add_component_blob(Entity entity, u32 component_id, const void* data, u32 size, u32 alignment);
    void remove_component(Entity entity, u32 component_id);

    void bind_pipeline(PipelineType type);
    void bind_descriptor_set(u32 set);
    void push_constants(const char* name, const void* data, usize size);
    void bind_material(u32 material_id);
    void draw_mesh(u32 mesh_id);

    void playback(World& world);
    void reset() noexcept;

    [[nodiscard]] u32 command_count() const noexcept { return command_count_; }
    [[nodiscard]] u32 gpu_command_count() const noexcept { return gpu_count_; }
    [[nodiscard]] const GpuCommand* gpu_commands() const noexcept { return gpu_commands_; }

    template <typename T>
    void add_component(Entity entity, u32 component_id, const T& value) {
        add_component_blob(entity, component_id, &value, static_cast<u32>(sizeof(T)),
                           static_cast<u32>(alignof(T)));
    }

private:
    void record_gpu(GpuCommand::Kind kind, u32 handle, u32 extra, const void* data, u32 size,
                    const char* name);

    Command* commands_       = nullptr;
    u32      command_cap_    = 0;
    u32      command_count_  = 0;
    u8*      payload_        = nullptr;
    usize    payload_cap_    = 0;
    usize    payload_off_    = 0;

    GpuCommand* gpu_commands_     = nullptr;
    u32         gpu_cap_          = 0;
    u32         gpu_count_        = 0;
    u8*         gpu_payload_      = nullptr;
    usize       gpu_payload_cap_  = 0;
    usize       gpu_payload_off_  = 0;
};

} // namespace engine
