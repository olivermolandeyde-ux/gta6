#include "eco/TrafficSystem.h"

#include "core/Assert.h"
#include "ecs/World.h"

namespace engine {

namespace {

inline constexpr u32 kLaneHashSlots = 256;

struct LaneHashSlot {
    u32 node_id;
    u32 vehicle_index;
    u16 generation;
    u16 occupied;
};

[[nodiscard]] u32 hash_node(u32 node_id) noexcept {
    u32 x = node_id * 0x9E3779B9u;
    x ^= x >> 16;
    return x;
}

void hash_insert(LaneHashSlot* table, u32 cap, u32 node_id, Entity vehicle) {
    u32 slot = hash_node(node_id) & (cap - 1);
    for (u32 probe = 0; probe < cap; ++probe) {
        LaneHashSlot& s = table[slot];
        if (s.occupied == 0 || s.node_id == node_id) {
            s.node_id        = node_id;
            s.vehicle_index  = vehicle.index();
            s.generation     = vehicle.generation();
            s.occupied       = 1;
            return;
        }
        slot = (slot + 1) & (cap - 1);
    }
}

[[nodiscard]] Entity hash_find(const LaneHashSlot* table, u32 cap, u32 node_id, u16 world_id) {
    if (node_id == 0) {
        return kNullEntity;
    }
    u32 slot = hash_node(node_id) & (cap - 1);
    for (u32 probe = 0; probe < cap; ++probe) {
        const LaneHashSlot& s = table[slot];
        if (s.occupied == 0) {
            return kNullEntity;
        }
        if (s.node_id == node_id) {
            return Entity::make(s.vehicle_index, s.generation, world_id);
        }
        slot = (slot + 1) & (cap - 1);
    }
    return kNullEntity;
}

struct LaneSample {
    u32    node_id;
    u32    next_node_id;
    u32    left_lane_node_id;
    u32    right_lane_node_id;
    float  speed_limit_m_s;
    float3 world_position;
    float3 forward_direction;
};

[[nodiscard]] const LaneSample* find_lane(const LaneSample* lanes, u32 count, u32 node_id) {
    for (u32 i = 0; i < count; ++i) {
        if (lanes[i].node_id == node_id) {
            return &lanes[i];
        }
    }
    return nullptr;
}

} // namespace

Entity instantiate_traffic_lane(World& world, const InstantiationRequest& request,
                                const TrafficLaneNodeComponent& node) {
    validate_instantiation(request);
    return world.instantiate(request, node);
}

Entity instantiate_traffic_vehicle(World& world, const InstantiationRequest& request,
                                   u32 chassis_entity_id, u32 lane_node_id, float3 position_ws,
                                   float speed_m_s) {
    validate_instantiation(request);
    TrafficVehicleComponent v{};
    v.vehicle_chassis_entity_id  = chassis_entity_id;
    v.current_lane_node_id       = lane_node_id;
    v.target_speed_m_s           = speed_m_s;
    v.current_speed_m_s          = speed_m_s;
    v.follow_distance_m          = 0.f;
    v.vehicle_in_front_entity_id = 0;

    TrafficVehiclePose pose{};
    pose.position_ws = position_ws;
    pose.heading_ws  = float3{0.f, 0.f, 1.f};
    return world.instantiate(request, v, pose);
}

void UpdateTrafficSystemSystem(World& world, float delta_time, FrameAllocator& frame_alloc) {
    ENGINE_ASSERT(delta_time >= 0.f && delta_time < 0.25f, "traffic dt");

    const u32 max_entities = world.entity_count();

    Entity* lane_ents = frame_alloc.allocate_array<Entity>(max_entities);
    u32 lane_count = 0;
    for (Entity e : world.query<TrafficLaneNodeComponent>()) {
        if (lane_count < max_entities) {
            lane_ents[lane_count++] = e;
        }
    }
    LaneSample* lanes = frame_alloc.allocate_array<LaneSample>(lane_count);
    for (u32 i = 0; i < lane_count; ++i) {
        const TrafficLaneNodeComponent* n = world.get<TrafficLaneNodeComponent>(lane_ents[i]);
        ENGINE_ASSERT(n != nullptr, "lane snapshot stale");
        lanes[i].node_id            = n->node_id;
        lanes[i].next_node_id       = n->next_node_id;
        lanes[i].left_lane_node_id  = n->left_lane_node_id;
        lanes[i].right_lane_node_id = n->right_lane_node_id;
        lanes[i].speed_limit_m_s    = n->speed_limit_m_s;
        lanes[i].world_position     = n->world_position;
        lanes[i].forward_direction  = n->forward_direction;
    }

    Entity* cars = frame_alloc.allocate_array<Entity>(max_entities);
    u32 car_count = 0;
    for (Entity e : world.query<TrafficVehicleComponent, TrafficVehiclePose>()) {
        if (car_count < max_entities) {
            cars[car_count++] = e;
        }
    }

    // Occupancy hash: node_id → vehicle. Average O(1) probe, never O(N²) over cars.
    LaneHashSlot* hash = frame_alloc.allocate_array<LaneHashSlot>(kLaneHashSlots);
    for (u32 i = 0; i < kLaneHashSlots; ++i) {
        hash[i].occupied = 0;
        hash[i].node_id  = 0;
    }
    for (u32 i = 0; i < car_count; ++i) {
        const TrafficVehicleComponent* v = world.get<TrafficVehicleComponent>(cars[i]);
        ENGINE_ASSERT(v != nullptr, "traffic snapshot stale");
        hash_insert(hash, kLaneHashSlots, v->current_lane_node_id, cars[i]);
    }

    for (u32 i = 0; i < car_count; ++i) {
        Entity e = cars[i];
        TrafficVehicleComponent* v = world.get<TrafficVehicleComponent>(e);
        TrafficVehiclePose*      p = world.get<TrafficVehiclePose>(e);
        ENGINE_ASSERT(v && p, "traffic pose stale");

        const LaneSample* lane = find_lane(lanes, lane_count, v->current_lane_node_id);
        const float limit = lane ? lane->speed_limit_m_s : 11.0f;

        Entity front = kNullEntity;
        if (lane) {
            front = hash_find(hash, kLaneHashSlots, lane->next_node_id, world.world_id());
            if (front.is_null()) {
                // One extra hop in case the bumper is two nodes ahead on a short graph.
                const LaneSample* next = find_lane(lanes, lane_count, lane->next_node_id);
                if (next) {
                    front = hash_find(hash, kLaneHashSlots, next->next_node_id, world.world_id());
                }
            }
        }

        if (!front.is_null() && front != e && world.is_alive(front)) {
            const TrafficVehiclePose* fp = world.get<TrafficVehiclePose>(front);
            const TrafficVehicleComponent* fv = world.get<TrafficVehicleComponent>(front);
            ENGINE_ASSERT(fp && fv, "lead vehicle stale");
            v->vehicle_in_front_entity_id = front.index();
            v->follow_distance_m = float3_length(float3_sub(fp->position_ws, p->position_ws));

            const float headway = 4.0f + 0.35f * v->current_speed_m_s;
            const float gap_err = v->follow_distance_m - headway;
            float acc_speed     = fv->current_speed_m_s + gap_err * 0.85f;
            if (v->follow_distance_m < 3.0f) {
                acc_speed = 0.f;
            }
            v->target_speed_m_s = clampf(min_of(limit, acc_speed), 0.f, limit);

            // Lane change if blocked and a neighbour lane is empty (O(1) hash probe).
            if (v->follow_distance_m < headway * 0.6f && lane) {
                const u32 alt = lane->left_lane_node_id != 0 ? lane->left_lane_node_id
                                                             : lane->right_lane_node_id;
                if (alt != 0 && hash_find(hash, kLaneHashSlots, alt, world.world_id()).is_null()) {
                    v->current_lane_node_id = alt;
                }
            }
        } else {
            v->vehicle_in_front_entity_id = 0;
            v->follow_distance_m          = 0.f;
            v->target_speed_m_s           = limit;
        }

        const float tau   = 0.40f;
        const float alpha = 1.0f - (tau <= 1.0e-4f ? 1.0f : (1.0f > delta_time / tau ? 0.0f : 0.0f));
        (void)alpha;
        const float k = clampf(delta_time / tau, 0.f, 1.f);
        v->current_speed_m_s += (v->target_speed_m_s - v->current_speed_m_s) * k;

        const float3 fwd = lane ? float3_normalize_or(lane->forward_direction, float3{0.f, 0.f, 1.f})
                                : p->heading_ws;
        p->heading_ws  = fwd;
        p->position_ws = float3_add(p->position_ws, float3_scale(fwd, v->current_speed_m_s * delta_time));
    }
}

} // namespace engine
