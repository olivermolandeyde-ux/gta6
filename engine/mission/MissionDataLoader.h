#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "mission/MissionSystem.h"

namespace engine {

class World;

struct MissionFileHeader {
    char magic[8];
    u32  version;
    u32  mission_count;
};

struct MissionFileEntry {
    MissionDefinitionComponent definition;
    u32 objective_count;
    u32 dialog_node_count;
    u32 response_count;
};

bool WriteTestConvenienceStoreMission(const char* filepath);

bool LoadMissionsFromFile(World& world, const char* filepath, CommandBuffer& cmd);

} // namespace engine
