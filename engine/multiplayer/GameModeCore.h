#pragma once

#include "core/Types.h"
#include "ecs/CommandBuffer.h"
#include "ecs/Entity.h"
#include "ecs/InstantiationRules.h"

namespace engine {

class World;

inline constexpr u8 kModeDeathmatch  = 0;
inline constexpr u8 kModeRace        = 1;
inline constexpr u8 kModeCoopMission = 2;

inline constexpr u8 kSessionLobby     = 0;
inline constexpr u8 kSessionCountdown = 1;
inline constexpr u8 kSessionActive    = 2;
inline constexpr u8 kSessionPostGame  = 3;

struct GameSessionComponent {
    u32   session_id;
    u8    mode_type;
    u8    session_state;
    float session_timer_s;
    u32   max_players;
    u32   current_player_count;
    u32   host_player_entity_id;
};

struct PlayerSessionStatsComponent {
    u32   player_entity_id;
    u32   score;
    u32   kills;
    u32   deaths;
    u32   team_id;
    float respawn_timer_s;
    bool  is_ready;
};

struct PlayerSessionBindComponent {
    u32 session_id;
};

struct MultiplayerTelemetryComponent {
    u32 sessions_active;
    u32 kills_validated;
    u32 checkpoints_validated;
    u32 revives_completed;
    u32 cheats_rejected;
    u32 packets_replicated;
};

[[nodiscard]] Entity instantiate_game_session(World& world, const InstantiationRequest& request,
                                              const GameSessionComponent& session);

void UpdateGameSessionSystem(World& world, float delta_time, CommandBuffer& cmd);

} // namespace engine
