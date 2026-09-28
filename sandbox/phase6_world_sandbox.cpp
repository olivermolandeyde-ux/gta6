#include "Engine.h"
#include "eco/PowerGridSystem.h"
#include "eco/TrafficSystem.h"
#include "objects/StreetLight.h"
#include "world/WorldStreamer.h"

#include <cstdio>

int main() {
    using namespace engine;

    MemoryBudget budget{};
    budget.world_arena_bytes     = 32ull * 1024ull * 1024ull;
    budget.streaming_arena_bytes = 16ull * 1024ull * 1024ull;
    budget.chunk_pool_bytes      = 16ull * 1024ull * 1024ull;
    budget.record_pool_bytes     = 4ull * 1024ull * 1024ull;
    budget.meta_pool_bytes       = 1ull * 1024ull * 1024ull;
    budget.stack_bytes           = 1ull * 1024ull * 1024ull;
    budget.frame_bytes           = 4ull * 1024ull * 1024ull;

    Engine engine;
    engine.boot(budget, /*world_id=*/1);
    World& world = engine.world();

    InstantiationRequest persistent{};
    persistent.domain      = InstantiationDomain::PersistentWorld;
    persistent.debug_label = "phase6";

    StreamObserverComponent observer{};
    observer.world_pos = float3{8.f, 0.f, 8.f}; // cell (0,0) at 32 m
    Entity player = world.instantiate(persistent, observer);
    (void)instantiate_world_streamer(world, persistent, player, /*radius=*/0, 32.f);

    world.begin_frame(1);
    UpdateWorldStreamerSystem(world, 1.f / 60.f, world.frame_commands());
    world.frame_commands().playback(world);

    ENGINE_ASSERT(streaming_cell_is_loaded(world, 0, 0), "home cell did not load");
    ENGINE_ASSERT(streaming_cell_arena_used(world, 0, 0) > 0, "cell arena unused after load");
    ENGINE_ASSERT(streaming_cell_entity_count(world, 0, 0) == 2, "expected 2 streamed residents");

    u32 live_00 = 0;
    for (Entity e : world.query<StreamingCellId>()) {
        const StreamingCellId* id = world.get<StreamingCellId>(e);
        if (id && id->cell_x == 0 && id->cell_y == 0) {
            ++live_00;
        }
    }
    ENGINE_ASSERT(live_00 == 2, "resident count mismatch");

    world.get<StreamObserverComponent>(player)->world_pos = float3{40.f, 0.f, 8.f}; // cell (1,0)
    world.begin_frame(2);
    UpdateWorldStreamerSystem(world, 1.f / 60.f, world.frame_commands());
    world.frame_commands().playback(world);

    ENGINE_ASSERT(!streaming_cell_is_loaded(world, 0, 0), "old cell still resident");
    ENGINE_ASSERT(streaming_cell_arena_used(world, 0, 0) == 0, "old cell LinearAllocator not reset");
    ENGINE_ASSERT(streaming_cell_is_loaded(world, 1, 0), "new cell did not load");

    u32 leftover_00 = 0;
    for (Entity e : world.query<StreamingCellId>()) {
        const StreamingCellId* id = world.get<StreamingCellId>(e);
        if (id && id->cell_x == 0 && id->cell_y == 0) {
            ++leftover_00;
        }
    }
    ENGINE_ASSERT(leftover_00 == 0, "unloaded cell entities survived playback");

    TrafficLaneNodeComponent n1{};
    n1.node_id = 1;
    n1.world_position = float3{0.f, 0.f, 0.f};
    n1.forward_direction = float3{0.f, 0.f, 1.f};
    n1.speed_limit_m_s = 13.0f;
    n1.next_node_id = 2;
    n1.left_lane_node_id = 0;
    n1.right_lane_node_id = 0;
    TrafficLaneNodeComponent n2 = n1;
    n2.node_id = 2;
    n2.world_position = float3{0.f, 0.f, 8.f};
    n2.next_node_id = 3;
    TrafficLaneNodeComponent n3 = n1;
    n3.node_id = 3;
    n3.world_position = float3{0.f, 0.f, 16.f};
    n3.next_node_id = 0;
    (void)instantiate_traffic_lane(world, persistent, n1);
    (void)instantiate_traffic_lane(world, persistent, n2);
    (void)instantiate_traffic_lane(world, persistent, n3);

    Entity car_a = instantiate_traffic_vehicle(world, persistent, 0, 1, n1.world_position, 12.f);
    Entity car_b = instantiate_traffic_vehicle(world, persistent, 0, 2, n2.world_position, 5.f);
    Entity car_c = instantiate_traffic_vehicle(world, persistent, 0, 3, n3.world_position, 2.f);

    world.begin_frame(3);
    UpdateTrafficSystemSystem(world, 1.f / 60.f, engine.memory().frame());

    const TrafficVehicleComponent* va = world.get<TrafficVehicleComponent>(car_a);
    const TrafficVehicleComponent* vb = world.get<TrafficVehicleComponent>(car_b);
    const TrafficVehicleComponent* vc = world.get<TrafficVehicleComponent>(car_c);
    ENGINE_ASSERT(va && vb && vc, "traffic cars missing");
    ENGINE_ASSERT(va->vehicle_in_front_entity_id == car_b.index(), "A should see B");
    ENGINE_ASSERT(vb->vehicle_in_front_entity_id == car_c.index(), "B should see C");
    ENGINE_ASSERT(vc->vehicle_in_front_entity_id == 0, "lead car should be clear");
    ENGINE_ASSERT(va->follow_distance_m > 7.0f && va->follow_distance_m < 9.0f, "A follow distance");
    ENGINE_ASSERT(vb->follow_distance_m > 7.0f && vb->follow_distance_m < 9.0f, "B follow distance");

    StreetLightSpawnDesc lamp_desc{};
    lamp_desc.light_handle       = 7;
    lamp_desc.power_grid_node_id = 42;
    lamp_desc.flicker_probability = 0.f;
    lamp_desc.wear_factor        = 0.f;
    Entity lamp = instantiate_street_light(world, persistent, lamp_desc);
    world.get<StreetLightComponent>(lamp)->current_voltage = 1.0f;

    PowerGridNodeComponent grid{};
    grid.node_id               = 42;
    grid.world_position        = float3{4.f, 6.f, 4.f};
    grid.current_load          = 0.4f;
    grid.max_capacity          = 1.0f;
    grid.is_operational        = true;
    grid.connected_lights[0]   = lamp.index();
    grid.connected_light_count = 1;
    (void)instantiate_power_grid_node(world, persistent, grid);

    PowerGridEvent ev{};
    ev.damaged_node_id = 42;
    world.begin_frame(4);
    UpdatePowerGridSystem(world, &ev, 1, world.frame_commands());
    world.frame_commands().playback(world);

    const StreetLightComponent* lit = world.get<StreetLightComponent>(lamp);
    ENGINE_ASSERT(lit != nullptr && lit->current_voltage == 0.0f, "node 42 outage did not zero voltage");
    ENGINE_ASSERT(world.has<StreetLightOutageTag>(lamp), "outage tag missing");

    std::printf("MICRO-PHASE 6 sandbox passed\n");
    std::printf("  cell 0,0 loaded after move : %s\n", streaming_cell_is_loaded(world, 0, 0) ? "yes" : "no");
    std::printf("  cell 0,0 arena used        : %zu\n", streaming_cell_arena_used(world, 0, 0));
    std::printf("  cell 1,0 loaded            : %s\n", streaming_cell_is_loaded(world, 1, 0) ? "yes" : "no");
    std::printf("  follow A->B                : %.3f m\n", static_cast<double>(va->follow_distance_m));
    std::printf("  follow B->C                : %.3f m\n", static_cast<double>(vb->follow_distance_m));
    std::printf("  lamp 42 voltage            : %.3f\n", static_cast<double>(lit->current_voltage));

    engine.shutdown();
    return 0;
}
