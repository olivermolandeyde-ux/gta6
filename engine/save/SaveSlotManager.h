#pragma once

#include "core/Types.h"
#include "memory/FrameAllocator.h"

namespace engine {

struct SaveSlotMetadata {
    u32    slot_id;
    char   slot_name[32];
    u64    timestamp_unix;
    u32    playtime_seconds;
    float  player_money;
    float3 player_location;
    bool   is_valid;
};

void ScanSaveSlots(const char* directory, SaveSlotMetadata* out_metadata, u32 max_slots,
                   FrameAllocator& frame_alloc);

} // namespace engine
