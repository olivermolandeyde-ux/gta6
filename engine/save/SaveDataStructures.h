#pragma once

#include "core/Types.h"

namespace engine {

inline constexpr u8  kSaveXorKey     = 0xA5;
inline constexpr u32 kSaveVersion    = 14;
inline constexpr u32 kSaveMagicSize  = 8;

struct SaveFileHeader {
    char magic[8];
    u32  version;
    u64  timestamp_unix;
    u32  playtime_seconds;
    u32  total_entities;
    u32  total_bytes;
    char slot_name[32];
};

struct SaveEntityBlock {
    u64 entity_handle;
    u32 archetype_id;
    u32 component_data_size;
};

struct SaveComponentSlice {
    u32 type_hash;
    u32 byte_size;
};

struct SavePreviewRecord {
    float  player_money;
    float3 player_location;
    u32    mission_state;
    u32    mission_objective;
};

struct SerializationContext {
    u8* buffer;
    u32 capacity;
    u32 write_offset;
};

inline void xor_payload(u8* bytes, u32 count, u8 key) noexcept {
    for (u32 i = 0; i < count; ++i) {
        bytes[i] = static_cast<u8>(bytes[i] ^ key);
    }
}

inline void copy_slot_name(char* dst, const char* src) noexcept {
    u32 i = 0;
    if (src) {
        for (; src[i] != '\0' && i + 1 < 32; ++i) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
    for (u32 z = i + 1; z < 32; ++z) {
        dst[z] = '\0';
    }
}

} // namespace engine
