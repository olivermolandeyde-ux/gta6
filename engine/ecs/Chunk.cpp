#include "ecs/Chunk.h"

#include "core/Assert.h"

namespace engine {

u16 compute_chunk_capacity(u32 bytes_per_row, u32 header_bytes) {
    ENGINE_ASSERT(bytes_per_row > 0, "empty row layout");
    ENGINE_ASSERT(header_bytes < kChunkBytes, "header larger than chunk");
    const u32 available = kChunkBytes - header_bytes;
    const u32 cap       = available / bytes_per_row;
    ENGINE_ASSERT(cap > 0, "components too large to fit a single row in a 16 KiB chunk");
    ENGINE_ASSERT(cap <= 0xFFFFu, "chunk capacity overflows u16");
    return static_cast<u16>(cap);
}

} // namespace engine
