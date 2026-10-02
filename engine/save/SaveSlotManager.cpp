#include "save/SaveSlotManager.h"

#include "core/Assert.h"
#include "save/SaveDataStructures.h"

#include <cstdio>
#include <cstring>

namespace engine {

void ScanSaveSlots(const char* directory, SaveSlotMetadata* out_metadata, u32 max_slots,
                   FrameAllocator& frame_alloc) {
    ENGINE_ASSERT(directory != nullptr, "save directory");
    ENGINE_ASSERT(out_metadata != nullptr, "metadata out");
    (void)frame_alloc;
    for (u32 i = 0; i < max_slots; ++i) {
        SaveSlotMetadata& m = out_metadata[i];
        std::memset(&m, 0, sizeof(m));
        m.slot_id  = i;
        m.is_valid = false;

        char path[256]{};
        const char* prefix = directory;
        u32 n = 0;
        for (; prefix[n] != '\0' && n + 32 < 256; ++n) {
            path[n] = prefix[n];
        }
        if (n > 0 && path[n - 1] != '/' && path[n - 1] != '\\') {
            path[n++] = '/';
        }
        const char* fname = "save_slot_0.sav";
        char built[32]{};
        built[0] = 's';
        built[1] = 'a';
        built[2] = 'v';
        built[3] = 'e';
        built[4] = '_';
        built[5] = 's';
        built[6] = 'l';
        built[7] = 'o';
        built[8] = 't';
        built[9] = '_';
        built[10] = static_cast<char>('0' + (i % 10));
        built[11] = '.';
        built[12] = 's';
        built[13] = 'a';
        built[14] = 'v';
        built[15] = '\0';
        fname = built;
        for (u32 k = 0; fname[k] != '\0' && n + 1 < 256; ++k) {
            path[n++] = fname[k];
        }
        path[n] = '\0';

        FILE* f = std::fopen(path, "rb");
        if (!f) {
            continue;
        }
        SaveFileHeader header{};
        SavePreviewRecord preview{};
        const bool ok = std::fread(&header, sizeof(header), 1, f) == 1
                     && std::memcmp(header.magic, "LEONSAVE", 8) == 0
                     && header.version == kSaveVersion
                     && std::fread(&preview, sizeof(preview), 1, f) == 1;
        std::fclose(f);
        if (!ok) {
            continue;
        }
        m.timestamp_unix   = header.timestamp_unix;
        m.playtime_seconds = header.playtime_seconds;
        m.player_money     = preview.player_money;
        m.player_location  = preview.player_location;
        copy_slot_name(m.slot_name, header.slot_name);
        m.is_valid = true;
    }
}

} // namespace engine
