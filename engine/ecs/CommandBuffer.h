#pragma once

#include "core/Types.h"
#include "ecs/Entity.h"
#include "memory/Allocator.h"

namespace engine {

class World;

// Records structural changes while a query is in flight. Played back by
// World::playback() after the system returns. Payload bytes live in a
// linear bump region so add_component<T>(e, value) can memcpy the value
// without touching the CRT heap.
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

    void bind(void* command_memory, usize command_bytes, void* payload_memory, usize payload_bytes);

    void destroy_entity(Entity entity);
    void add_component_blob(Entity entity, u32 component_id, const void* data, u32 size, u32 alignment);
    void remove_component(Entity entity, u32 component_id);

    void playback(World& world);
    void reset() noexcept;

    [[nodiscard]] u32 command_count() const noexcept { return command_count_; }

    template <typename T>
    void add_component(Entity entity, u32 component_id, const T& value) {
        add_component_blob(entity, component_id, &value, static_cast<u32>(sizeof(T)),
                           static_cast<u32>(alignof(T)));
    }

private:
    Command* commands_       = nullptr;
    u32      command_cap_    = 0;
    u32      command_count_  = 0;
    u8*      payload_        = nullptr;
    usize    payload_cap_    = 0;
    usize    payload_off_    = 0;
};

} // namespace engine
