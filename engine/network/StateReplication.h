#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "memory/FrameAllocator.h"
#include "network/InterestManagement.h"
#include "network/NetworkCore.h"

namespace engine {

class World;

struct ClientPredictionComponent {
    u32    last_acknowledged_input_sequence;
    float3 predicted_position;
    float3 predicted_velocity;
};

struct ServerStateComponent {
    float3 server_position;
    float3 server_velocity;
    u32    server_sequence;
    float  interpolation_alpha; // For smoothing remote players
};

struct ClientInputCommand {
    u32    sequence;
    float3 velocity;
    u32    shoot_network_id; // 0 = none
    float3 shoot_point;
};

void submit_client_input(World& world, NetworkEndpoint client, const ClientInputCommand& input);

void UpdateStateReplicationSystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                                  CommandBuffer& cmd);

} // namespace engine
