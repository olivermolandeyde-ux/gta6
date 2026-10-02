#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "memory/FrameAllocator.h"

namespace engine {

class World;

bool DeserializeWorldFromFile(World& world, const char* filepath, CommandBuffer& cmd,
                              FrameAllocator& frame_alloc);

[[nodiscard]] u32 last_deserialize_entity_count() noexcept;

} // namespace engine
