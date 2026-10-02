#include "multiplayer/GameModeCore.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "gameplay/PlayerController.h"
#include "multiplayer/DeathmatchMode.h"
#include "network/NetworkCore.h"

namespace engine {

namespace {

[[nodiscard]] Entity find_player(World& world, u32 index) {
    for (Entity e : world.query<PlayerStateComponent>()) {
        if (e.index() == index) {
            return e;
        }
    }
    return kNullEntity;
}

} // namespace

Entity instantiate_game_session(World& world, const InstantiationRequest& request,
                                const GameSessionComponent& session) {
    validate_instantiation(request);
    return world.instantiate(request, session);
}

void UpdateGameSessionSystem(World& world, float delta_time, CommandBuffer& cmd) {
    (void)cmd;
    ENGINE_ASSERT(delta_time >= 0.f, "session dt");

    u32 active = 0;
    for (Entity e : world.query<GameSessionComponent>()) {
        GameSessionComponent* s = world.get<GameSessionComponent>(e);
        ENGINE_ASSERT(s != nullptr, "session stale");
        s->session_timer_s += delta_time;

        u32 bound = 0;
        u32 ready = 0;
        for (Entity p : world.query<PlayerSessionBindComponent, PlayerSessionStatsComponent>()) {
            const PlayerSessionBindComponent* b = world.get<PlayerSessionBindComponent>(p);
            PlayerSessionStatsComponent* st = world.get<PlayerSessionStatsComponent>(p);
            if (!b || !st || b->session_id != s->session_id) {
                continue;
            }
            ++bound;
            if (st->is_ready) {
                ++ready;
            }
            if (st->respawn_timer_s > 0.f) {
                st->respawn_timer_s = max_of(0.f, st->respawn_timer_s - delta_time);
                if (st->respawn_timer_s <= 0.f) {
                    Entity body = find_player(world, st->player_entity_id);
                    if (world.is_alive(body)) {
                        PlayerStateComponent* ps = world.get<PlayerStateComponent>(body);
                        if (ps) {
                            ps->health = ps->max_health;
                        }
                    }
                    for (Entity r : world.query<DeathmatchRuleComponent>()) {
                        DeathmatchRuleComponent* rules = world.get<DeathmatchRuleComponent>(r);
                        if (!rules || rules->session_id != s->session_id || rules->spawn_point_count == 0) {
                            continue;
                        }
                        const u32 idx = st->deaths % rules->spawn_point_count;
                        if (world.is_alive(body)) {
                            PlayerStateComponent* ps = world.get<PlayerStateComponent>(body);
                            if (ps) {
                                ps->camera_position = rules->spawn_points[idx];
                            }
                        }
                    }
                }
            }
        }
        s->current_player_count = bound;

        if (s->session_state == kSessionLobby && bound >= 2 && ready == bound) {
            s->session_state   = kSessionCountdown;
            s->session_timer_s = 0.f;
        } else if (s->session_state == kSessionCountdown && s->session_timer_s >= 3.0f) {
            s->session_state   = kSessionActive;
            s->session_timer_s = 0.f;
        } else if (s->session_state == kSessionActive) {
            if (s->mode_type == kModeDeathmatch) {
                for (Entity r : world.query<DeathmatchRuleComponent>()) {
                    const DeathmatchRuleComponent* rules = world.get<DeathmatchRuleComponent>(r);
                    if (!rules || rules->session_id != s->session_id) {
                        continue;
                    }
                    bool hit_score = false;
                    for (Entity p : world.query<PlayerSessionStatsComponent, PlayerSessionBindComponent>()) {
                        const PlayerSessionBindComponent* b = world.get<PlayerSessionBindComponent>(p);
                        const PlayerSessionStatsComponent* st = world.get<PlayerSessionStatsComponent>(p);
                        if (b && st && b->session_id == s->session_id && st->score >= rules->score_limit) {
                            hit_score = true;
                        }
                    }
                    if (hit_score || (rules->time_limit_s > 0.f && s->session_timer_s >= rules->time_limit_s)) {
                        s->session_state = kSessionPostGame;
                    }
                }
            }
        }
        if (s->session_state == kSessionActive || s->session_state == kSessionCountdown
            || s->session_state == kSessionLobby) {
            ++active;
        }
    }

    for (Entity e : world.query<MultiplayerTelemetryComponent>()) {
        MultiplayerTelemetryComponent* t = world.get<MultiplayerTelemetryComponent>(e);
        if (t) {
            t->sessions_active = active;
        }
        break;
    }
}

} // namespace engine
