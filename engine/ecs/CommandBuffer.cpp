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

void CommandBuffer::reset() noexcept {
    command_count_ = 0;
    payload_off_   = 0;
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
    reset();
}

} // namespace engine
