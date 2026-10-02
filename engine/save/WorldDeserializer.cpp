#include "save/WorldDeserializer.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "memory/MemorySystem.h"
#include "save/SaveDataStructures.h"

#include <cstdio>
#include <cstring>

namespace engine {

namespace {

u32 g_loaded_entities = 0;

[[nodiscard]] bool read_exact(const u8*& p, const u8* end, void* dst, u32 n) {
    if (p + n > end) {
        return false;
    }
    std::memcpy(dst, p, n);
    p += n;
    return true;
}

[[nodiscard]] u32 align4(u32 v) {
    return (v + 3u) & ~3u;
}

} // namespace

u32 last_deserialize_entity_count() noexcept {
    return g_loaded_entities;
}

bool DeserializeWorldFromFile(World& world, const char* filepath, CommandBuffer& cmd,
                              FrameAllocator& frame_alloc) {
    ENGINE_ASSERT(filepath != nullptr, "load path");
    FILE* f = std::fopen(filepath, "rb");
    if (!f) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        std::fclose(f);
        return false;
    }
    const u32 file_bytes = static_cast<u32>(sz);
    u8* scratch = static_cast<u8*>(frame_alloc.allocate(file_bytes, 16));
    if (!scratch) {
        std::fclose(f);
        return false;
    }
    const usize got = std::fread(scratch, 1, file_bytes, f);
    std::fclose(f);
    if (got != file_bytes) {
        return false;
    }

    const u8* p   = scratch;
    const u8* end = scratch + file_bytes;
    SaveFileHeader header{};
    if (!read_exact(p, end, &header, sizeof(header))) {
        return false;
    }
    if (std::memcmp(header.magic, "LEONSAVE", 8) != 0 || header.version != kSaveVersion) {
        return false;
    }
    SavePreviewRecord preview{};
    if (!read_exact(p, end, &preview, sizeof(preview))) {
        return false;
    }
    u8 key = 0;
    if (!read_exact(p, end, &key, sizeof(key))) {
        return false;
    }
    u32 payload_size = 0;
    if (!read_exact(p, end, &payload_size, sizeof(payload_size))) {
        return false;
    }
    if (p + payload_size > end) {
        return false;
    }
    xor_payload(const_cast<u8*>(p), payload_size, key);
    const u8* payload_end = p + payload_size;

    // Clean slate: CommandBuffer destroy-all, then streaming linear reset.
    // World-arena records/command rings stay mapped (resetting that arena would
    // free the entity table itself).
    world.enqueue_destroy_all_live(cmd);
    cmd.playback(world);
    ENGINE_ASSERT(world.live_entity_count() == 0, "world not empty after reset");
    world.memory().streaming_arena().reset();

    u32 restored = 0;
    while (p < payload_end) {
        SaveEntityBlock block{};
        if (!read_exact(p, payload_end, &block, sizeof(block))) {
            return false;
        }
        const u8* blob_end = p + block.component_data_size;
        if (blob_end > payload_end) {
            return false;
        }
        u32 ncol = 0;
        if (!read_exact(p, blob_end, &ncol, sizeof(ncol))) {
            return false;
        }
        Entity e = world.resurrect(block.entity_handle);
        for (u32 c = 0; c < ncol; ++c) {
            SaveComponentSlice slice{};
            if (!read_exact(p, blob_end, &slice, sizeof(slice))) {
                return false;
            }
            const u32 cid = world.registry().find_id_by_hash(slice.type_hash);
            ENGINE_ASSERT(cid != kInvalidIndex, "unknown component hash in save");
            ENGINE_ASSERT(world.registry().type(cid).size == slice.byte_size, "component size mismatch");
            ENGINE_ASSERT(p + slice.byte_size <= blob_end, "component bytes overrun");
            world.add_component_blob(e, cid, p);
            p += slice.byte_size;
            const u32 pad = align4(slice.byte_size) - slice.byte_size;
            if (p + pad > blob_end) {
                return false;
            }
            p += pad;
        }
        p = blob_end;
        ++restored;
    }

    g_loaded_entities = restored;
    ENGINE_ASSERT(restored == header.total_entities, "entity count mismatch vs header");
    (void)preview;
    return true;
}

} // namespace engine
