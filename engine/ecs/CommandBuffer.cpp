#include "ecs/CommandBuffer.h"

#include "core/Assert.h"
#include "ecs/World.h"

#include <cstring>

namespace engine {

void CommandBuffer::bind(void* command_memory, usize command_bytes,
                         void* payload_memory, usize payload_bytes) {
    ENGINE_ASSERT(command_memory != nullptr, "command buffer storage");
    ENGINE_ASSERT(payload_memory != nullptr, "command payload storage");
    commands_      = static_cast<Command*>(command_memory);
    command_cap_   = static_cast<u32>(command_bytes / sizeof(Command));
    command_count_ = 0;
    payload_       = static_cast<u8*>(payload_memory);
    payload_cap_   = payload_bytes;
    payload_off_   = 0;
}

void CommandBuffer::bind_gpu(void* gpu_memory, usize gpu_bytes, void* gpu_payload,
                             usize gpu_payload_bytes) {
    ENGINE_ASSERT(gpu_memory != nullptr, "gpu command storage");
    ENGINE_ASSERT(gpu_payload != nullptr, "gpu payload storage");
    gpu_commands_     = static_cast<GpuCommand*>(gpu_memory);
    gpu_cap_          = static_cast<u32>(gpu_bytes / sizeof(GpuCommand));
    gpu_count_        = 0;
    gpu_payload_      = static_cast<u8*>(gpu_payload);
    gpu_payload_cap_  = gpu_payload_bytes;
    gpu_payload_off_  = 0;
}

void CommandBuffer::reset() noexcept {
    command_count_    = 0;
    payload_off_      = 0;
    gpu_count_        = 0;
    gpu_payload_off_  = 0;
}

void CommandBuffer::destroy_entity(Entity entity) {
    ENGINE_ASSERT(command_count_ < command_cap_, "command buffer overflow");
    Command& cmd      = commands_[command_count_++];
    cmd.op            = Op::DestroyEntity;
    cmd.entity        = entity;
    cmd.component_id  = kInvalidIndex;
    cmd.payload_off   = 0;
    cmd.payload_size  = 0;
}

void CommandBuffer::add_component_blob(Entity entity, u32 component_id, const void* data,
                                       u32 size, u32 alignment) {
    ENGINE_ASSERT(command_count_ < command_cap_, "command buffer overflow");
    const usize aligned = align_up(payload_off_, static_cast<usize>(alignment));
    ENGINE_ASSERT(aligned + size <= payload_cap_, "command payload overflow");
    std::memcpy(payload_ + aligned, data, size);

    Command& cmd      = commands_[command_count_++];
    cmd.op            = Op::AddComponent;
    cmd.entity        = entity;
    cmd.component_id  = component_id;
    cmd.payload_off   = static_cast<u32>(aligned);
    cmd.payload_size  = size;
    payload_off_      = aligned + size;
}

void CommandBuffer::remove_component(Entity entity, u32 component_id) {
    ENGINE_ASSERT(command_count_ < command_cap_, "command buffer overflow");
    Command& cmd      = commands_[command_count_++];
    cmd.op            = Op::RemoveComponent;
    cmd.entity        = entity;
    cmd.component_id  = component_id;
    cmd.payload_off   = 0;
    cmd.payload_size  = 0;
}

void CommandBuffer::record_gpu(GpuCommand::Kind kind, u32 handle, u32 extra, const void* data,
                               u32 size, const char* name) {
    ENGINE_ASSERT(gpu_commands_ != nullptr, "gpu command stream not bound");
    ENGINE_ASSERT(gpu_count_ < gpu_cap_, "gpu command buffer overflow");

    u32 payload_off = 0;
    if (data && size > 0) {
        const usize aligned = align_up(gpu_payload_off_, static_cast<usize>(16));
        ENGINE_ASSERT(aligned + size <= gpu_payload_cap_, "gpu payload overflow");
        std::memcpy(gpu_payload_ + aligned, data, size);
        payload_off       = static_cast<u32>(aligned);
        gpu_payload_off_  = aligned + size;
    }

    GpuCommand& gpu   = gpu_commands_[gpu_count_++];
    gpu.kind          = kind;
    gpu.handle        = handle;
    gpu.extra         = extra;
    gpu.payload_off   = payload_off;
    gpu.payload_size  = size;
    gpu.name          = name;
}

void CommandBuffer::bind_pipeline(PipelineType type) {
    record_gpu(GpuCommand::Kind::BindPipeline, static_cast<u32>(type), 0, nullptr, 0, nullptr);
}

void CommandBuffer::bind_descriptor_set(u32 set) {
    record_gpu(GpuCommand::Kind::BindDescriptorSet, set, 0, nullptr, 0, nullptr);
}

void CommandBuffer::push_constants(const char* name, const void* data, usize size) {
    record_gpu(GpuCommand::Kind::PushConstants, 0, 0, data, static_cast<u32>(size), name);
}

void CommandBuffer::bind_material(u32 material_id) {
    record_gpu(GpuCommand::Kind::BindMaterial, material_id, 0, nullptr, 0, nullptr);
}

void CommandBuffer::draw_mesh(u32 mesh_id) {
    record_gpu(GpuCommand::Kind::DrawMesh, mesh_id, 0, nullptr, 0, nullptr);
}

void CommandBuffer::playback(World& world) {
    const u32 count = command_count_;
    for (u32 i = 0; i < count; ++i) {
        const Command& cmd = commands_[i];
        switch (cmd.op) {
        case Op::DestroyEntity:
            world.destroy(cmd.entity);
            break;
        case Op::AddComponent:
            if (world.is_alive(cmd.entity)) {
                world.add_component_blob(cmd.entity, cmd.component_id, payload_ + cmd.payload_off);
            }
            break;
        case Op::RemoveComponent:
            if (world.is_alive(cmd.entity)) {
                world.remove_component_id(cmd.entity, cmd.component_id);
            }
            break;
        }
    }
    command_count_ = 0;
    payload_off_   = 0;
}

} // namespace engine
