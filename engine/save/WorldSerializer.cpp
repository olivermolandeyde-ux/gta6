#include "save/WorldSerializer.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"
#include "mission/MissionSystem.h"
#include "mission/RewardSystem.h"

#include <cstdio>
#include <cstring>

namespace engine {

namespace {

u32 g_saved_entities = 0;
u32 g_saved_bytes    = 0;

[[nodiscard]] bool ctx_write(SerializationContext* ctx, const void* src, u32 bytes) {
    ENGINE_ASSERT(ctx != nullptr && ctx->buffer != nullptr, "serialize ctx");
    if (ctx->write_offset + bytes > ctx->capacity) {
        return false;
    }
    std::memcpy(ctx->buffer + ctx->write_offset, src, bytes);
    ctx->write_offset += bytes;
    return true;
}

[[nodiscard]] u32 align4(u32 v) {
    return (v + 3u) & ~3u;
}

void fill_preview(World& world, SavePreviewRecord* preview) {
    std::memset(preview, 0, sizeof(*preview));
    for (Entity e : world.query<PlayerStateComponent>()) {
        const PlayerStateComponent* p = world.get<PlayerStateComponent>(e);
        if (p) {
            preview->player_location = p->camera_position;
        }
        break;
    }
    for (Entity e : world.query<PlayerWalletComponent>()) {
        const PlayerWalletComponent* w = world.get<PlayerWalletComponent>(e);
        if (w) {
            preview->player_money = static_cast<float>(w->cash_usd);
        }
        break;
    }
    for (Entity e : world.query<MissionStateComponent>()) {
        const MissionStateComponent* s = world.get<MissionStateComponent>(e);
        if (s) {
            preview->mission_state     = s->current_state;
            preview->mission_objective = s->current_objective_index;
        }
        break;
    }
}

} // namespace

u32 last_serialize_entity_count() noexcept {
    return g_saved_entities;
}

u32 last_serialize_file_bytes() noexcept {
    return g_saved_bytes;
}

bool SerializeWorldToFile(World& world, const char* filepath, u32 slot_id, PoolAllocator& pool_alloc) {
    ENGINE_ASSERT(filepath != nullptr, "save path");
    void* block = pool_alloc.allocate();
    ENGINE_ASSERT(block != nullptr, "save pool block");

    SerializationContext ctx{};
    ctx.buffer       = static_cast<u8*>(block);
    ctx.capacity     = static_cast<u32>(pool_alloc.block_size());
    ctx.write_offset = 0;

    SaveFileHeader header{};
    std::memcpy(header.magic, "LEONSAVE", 8);
    header.version          = kSaveVersion;
    header.timestamp_unix   = 1711500000ull;
    header.playtime_seconds = 0;
    header.total_entities   = 0;
    header.total_bytes      = 0;
    char namebuf[32]{};
    namebuf[0] = 'S';
    namebuf[1] = 'l';
    namebuf[2] = 'o';
    namebuf[3] = 't';
    namebuf[4] = ' ';
    namebuf[5] = static_cast<char>('0' + (slot_id % 10));
    copy_slot_name(header.slot_name, namebuf);
    if (slot_id == 1) {
        copy_slot_name(header.slot_name, "Slot 1 - Downtown");
    }

    SavePreviewRecord preview{};
    fill_preview(world, &preview);

    if (!ctx_write(&ctx, &header, sizeof(header))) {
        pool_alloc.deallocate(block);
        return false;
    }
    if (!ctx_write(&ctx, &preview, sizeof(preview))) {
        pool_alloc.deallocate(block);
        return false;
    }
    const u8 key = kSaveXorKey;
    if (!ctx_write(&ctx, &key, sizeof(key))) {
        pool_alloc.deallocate(block);
        return false;
    }
    u32 payload_size_slot = ctx.write_offset;
    u32 payload_size      = 0;
    if (!ctx_write(&ctx, &payload_size, sizeof(payload_size))) {
        pool_alloc.deallocate(block);
        return false;
    }
    const u32 payload_begin = ctx.write_offset;

    u32 saved = 0;
    // Dense archetype iteration: chunks are packed [0, count), no holes.
    for (u32 a = 0; a < world.archetype_count(); ++a) {
        Archetype& arch = world.archetype_at(a);
        if (arch.column_count == 0) {
            continue; // skip the empty archetype
        }
        for (u16 c = 0; c < arch.chunk_count; ++c) {
            Chunk* chunk = arch.chunks[c];
            const u16 count = chunk->header().count;
            if (count == 0) {
                continue;
            }
            const Entity* ents = chunk->entities();
            for (u16 row = 0; row < count; ++row) {
                u32 blob = sizeof(u32); // column_count
                for (u32 col = 0; col < arch.column_count; ++col) {
                    blob += static_cast<u32>(sizeof(SaveComponentSlice));
                    blob += align4(arch.columns[col].size);
                }
                SaveEntityBlock block_hdr{};
                block_hdr.entity_handle       = ents[row].packed;
                block_hdr.archetype_id        = arch.index;
                block_hdr.component_data_size = blob;
                if (!ctx_write(&ctx, &block_hdr, sizeof(block_hdr))) {
                    pool_alloc.deallocate(block);
                    return false;
                }
                const u32 ncol = arch.column_count;
                if (!ctx_write(&ctx, &ncol, sizeof(ncol))) {
                    pool_alloc.deallocate(block);
                    return false;
                }
                for (u32 col = 0; col < arch.column_count; ++col) {
                    const ChunkColumn& cc = arch.columns[col];
                    const ComponentType& type = world.registry().type(cc.component_id);
                    SaveComponentSlice slice{};
                    slice.type_hash = type.hash;
                    slice.byte_size = cc.size;
                    if (!ctx_write(&ctx, &slice, sizeof(slice))) {
                        pool_alloc.deallocate(block);
                        return false;
                    }
                    const u8* src = chunk->bytes + cc.offset + static_cast<usize>(row) * cc.size;
                    if (!ctx_write(&ctx, src, cc.size)) {
                        pool_alloc.deallocate(block);
                        return false;
                    }
                    const u32 pad = align4(cc.size) - cc.size;
                    if (pad > 0) {
                        const u32 zeros = 0;
                        if (!ctx_write(&ctx, &zeros, pad)) {
                            pool_alloc.deallocate(block);
                            return false;
                        }
                    }
                }
                ++saved;
            }
        }
    }

    payload_size = ctx.write_offset - payload_begin;
    xor_payload(ctx.buffer + payload_begin, payload_size, kSaveXorKey);

    header.total_entities = saved;
    header.total_bytes    = payload_size;
    std::memcpy(ctx.buffer, &header, sizeof(header));
    std::memcpy(ctx.buffer + payload_size_slot, &payload_size, sizeof(payload_size));

    FILE* f = std::fopen(filepath, "wb");
    if (!f) {
        pool_alloc.deallocate(block);
        return false;
    }
    const usize wrote = std::fwrite(ctx.buffer, 1, ctx.write_offset, f);
    std::fclose(f);
    g_saved_entities = saved;
    g_saved_bytes    = static_cast<u32>(wrote);
    pool_alloc.deallocate(block);
    return wrote == ctx.write_offset;
}

} // namespace engine
