#pragma once

#include "core/Types.h"
#include "memory/PoolAllocator.h"
#include "save/SaveDataStructures.h"

namespace engine {

class World;

bool SerializeWorldToFile(World& world, const char* filepath, u32 slot_id, PoolAllocator& pool_alloc);

[[nodiscard]] u32 last_serialize_entity_count() noexcept;
[[nodiscard]] u32 last_serialize_file_bytes() noexcept;

} // namespace engine
