#include "ai/PedestrianBehavior.h"

#include "core/Assert.h"
#include "ecs/World.h"

namespace engine {

namespace {

struct NavSample {
    u32    node_id;
    float3 position_ws;
};

[[nodiscard]] u32 pick_flee_node(const NavSample* nodes, u32 node_count, float3 self,
                                 float3 threat, u32 current) {
    if (node_count == 0) {
        return current;
    }
    const float3 away = float3_normalize_or(float3_sub(self, threat), float3{0.f, 0.f, 1.f});
    float best_score = -1.0e30f;
    u32   best_id    = current;
    for (u32 i = 0; i < node_count; ++i) {
        const float3 to_node = float3_sub(nodes[i].position_ws, self);
        const float  dist    = float3_length(to_node);
        const float  align   = float3_dot(float3_normalize_or(to_node, away), away);
        const float  score   = align * 4.0f + dist * 0.05f;
        if (score > best_score) {
            best_score = score;
            best_id    = nodes[i].node_id;
        }
    }
    return best_id;
}

} // namespace

Entity instantiate_pedestrian(World& world, const InstantiationRequest& request,
                              const PedestrianSpawnDesc& desc) {
    validate_instantiation(request);
    PedestrianBehaviorComponent brain{};
    brain.state_id             = kPedestrianStateCalm;
    brain.fear_level           = 0.f;
    brain.threat_position      = desc.position_ws;
    brain.destination_node_id  = desc.destination_node_id;
    brain.reaction_delay_timer = desc.reaction_delay_s;

    PedestrianWorldPose pose{};
    pose.position_ws = desc.position_ws;
    pose.facing_ws   = float3_normalize_or(desc.facing_ws, float3{0.f, 0.f, 1.f});
    return world.instantiate(request, brain, pose);
}

Entity instantiate_pedestrian_threat(World& world, const InstantiationRequest& request,
                                     const PedestrianThreatStimulus& stimulus) {
    validate_instantiation(request);
    return world.instantiate(request, stimulus);
}

Entity instantiate_pedestrian_nav_node(World& world, const InstantiationRequest& request,
                                       u32 node_id, float3 position_ws) {
    validate_instantiation(request);
    PedestrianNavNodeComponent node{};
    node.node_id     = node_id;
    node.position_ws = position_ws;
    return world.instantiate(request, node);
}

void UpdatePedestrianAISystem(World& world, float delta_time, CommandBuffer& cmd,
                              FrameAllocator& frame_alloc) {
    ENGINE_ASSERT(delta_time >= 0.f && delta_time < 0.25f, "pedestrian dt");

    const u32 max_entities = world.entity_count();

    Entity* threat_ents = frame_alloc.allocate_array<Entity>(max_entities);
    u32 threat_count = 0;
    for (Entity e : world.query<PedestrianThreatStimulus>()) {
        if (threat_count < max_entities) {
            threat_ents[threat_count++] = e;
        }
    }

    Entity* nav_ents = frame_alloc.allocate_array<Entity>(max_entities);
    u32 nav_count = 0;
    for (Entity e : world.query<PedestrianNavNodeComponent>()) {
        if (nav_count < max_entities) {
            nav_ents[nav_count++] = e;
        }
    }
    NavSample* nodes = frame_alloc.allocate_array<NavSample>(nav_count);
    for (u32 i = 0; i < nav_count; ++i) {
        const PedestrianNavNodeComponent* n = world.get<PedestrianNavNodeComponent>(nav_ents[i]);
        ENGINE_ASSERT(n != nullptr, "nav snapshot stale");
        nodes[i].node_id     = n->node_id;
        nodes[i].position_ws = n->position_ws;
    }

    Entity* peds = frame_alloc.allocate_array<Entity>(max_entities);
    u32 ped_count = 0;
    for (Entity e : world.query<PedestrianBehaviorComponent, PedestrianWorldPose>()) {
        if (ped_count < max_entities) {
            peds[ped_count++] = e;
        }
    }

    const u32 fleeing_id = world.registry().id_of<PedestrianFleeingTag>();

    for (u32 i = 0; i < ped_count; ++i) {
        Entity e = peds[i];
        PedestrianBehaviorComponent* brain = world.get<PedestrianBehaviorComponent>(e);
        const PedestrianWorldPose*   pose  = world.get<PedestrianWorldPose>(e);
        ENGINE_ASSERT(brain != nullptr && pose != nullptr, "pedestrian snapshot stale");

        float  best_stim = 0.f;
        float3 best_pos  = brain->threat_position;
        for (u32 t = 0; t < threat_count; ++t) {
            const PedestrianThreatStimulus* stim = world.get<PedestrianThreatStimulus>(threat_ents[t]);
            ENGINE_ASSERT(stim != nullptr, "threat snapshot stale");
            const float dist = float3_length(float3_sub(pose->position_ws, stim->position_ws));
            if (dist > stim->radius_m) {
                continue;
            }
            const float falloff = 1.0f - (dist / max_of(stim->radius_m, 0.01f));
            const float stim_v  = clampf(stim->loudness, 0.f, 1.f) * falloff * falloff;
            if (stim_v > best_stim) {
                best_stim = stim_v;
                best_pos  = stim->position_ws;
            }
        }

        if (best_stim > 0.02f) {
            brain->threat_position = best_pos;
        }

        // Fear integrates up under stimulus and decays otherwise (not a generic utility AI).
        const float rise  = 2.40f * best_stim;
        const float decay = 0.18f + 0.10f * brain->fear_level;
        brain->fear_level = clampf(brain->fear_level + (rise - decay) * delta_time, 0.f, 1.f);

        const u32 previous_state = brain->state_id;
        if (brain->fear_level < 0.22f && best_stim < 0.05f) {
            brain->state_id = kPedestrianStateCalm;
            brain->reaction_delay_timer = max_of(brain->reaction_delay_timer, 0.20f);
        } else if (brain->fear_level < 0.62f && brain->state_id != kPedestrianStateFleeing) {
            brain->state_id = kPedestrianStateAlerted;
        } else if (brain->fear_level >= 0.62f) {
            brain->reaction_delay_timer -= delta_time;
            if (brain->reaction_delay_timer <= 0.f) {
                brain->state_id = kPedestrianStateFleeing;
                brain->destination_node_id =
                    pick_flee_node(nodes, nav_count, pose->position_ws, brain->threat_position,
                                   brain->destination_node_id);
            } else {
                brain->state_id = kPedestrianStateAlerted;
            }
        }

        const bool tagged = world.has<PedestrianFleeingTag>(e);
        if (brain->state_id == kPedestrianStateFleeing && !tagged) {
            PedestrianFleeingTag tag{};
            tag.destination_node_id = brain->destination_node_id;
            tag.fear_at_trigger     = brain->fear_level;
            cmd.add_component<PedestrianFleeingTag>(e, fleeing_id, tag);
        } else if (brain->state_id != kPedestrianStateFleeing && tagged) {
            cmd.remove_component(e, fleeing_id);
        }

        (void)previous_state;
    }
}

} // namespace engine
