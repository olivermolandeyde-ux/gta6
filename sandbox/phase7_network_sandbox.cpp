#include "Engine.h"
#include "network/InterestManagement.h"
#include "network/NetworkCore.h"
#include "network/StateReplication.h"
#include "objects/BreakableWindow.h"

#include <cstdio>

int main() {
    using namespace engine;

    MemoryBudget budget{};
    budget.world_arena_bytes     = 32ull * 1024ull * 1024ull;
    budget.streaming_arena_bytes = 8ull * 1024ull * 1024ull;
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
    persistent.debug_label = "net_peer";

    NetworkEndpoint ep_server{0x7F000001u, 7777};
    NetworkEndpoint ep_c1{0x7F000002u, 1001};
    NetworkEndpoint ep_c2{0x7F000003u, 1002};

    (void)instantiate_network_peer(world, persistent, ep_server, true, 0, 0);
    Entity client1 = instantiate_network_peer(world, persistent, ep_c1, false, 0, 0);
    Entity client2 = instantiate_network_peer(world, persistent, ep_c2, false, 5, 5);

    InstantiationRequest cell00{};
    cell00.domain          = InstantiationDomain::StreamingCell;
    cell00.debug_label     = "window_00";
    cell00.cell.cell_x     = 0;
    cell00.cell.cell_y     = 0;
    BreakableWindowSpawnDesc glass{};
    glass.mesh_id_intact    = 10;
    glass.mesh_id_shattered = 11;
    glass.fracture_seed     = 9;
    glass.center_ws         = float3{4.f, 1.2f, 4.f};
    Entity window = instantiate_breakable_window(world, cell00, glass);
    StreamingCellId wcell{};
    wcell.cell_x = 0;
    wcell.cell_y = 0;
    (void)attach_network_identity(world, window, /*network_id=*/42, wcell, false);

    world.begin_frame(1);
    AOIQueryResult aoi_c1 = CalculateAreaOfInterest(world, ep_c1, /*cell_radius=*/1,
                                                    engine.memory().frame());
    ENGINE_ASSERT(aoi_contains(aoi_c1, window), "window in (0,0) missing from client 1 AOI");
    ENGINE_ASSERT(!aoi_contains(aoi_c1, client2), "client 2 in (5,5) leaked into client 1 AOI");
    ENGINE_ASSERT(aoi_contains(aoi_c1, client1), "client 1 should see themselves");

    NetworkRPC shoot{};
    shoot.target_entity_network_id = 42;
    shoot.rpc_type                 = kRpcShoot;
    shoot.payload_pos              = glass.center_ws;
    enqueue_network_rpc(world, shoot, ep_c1, ep_server);

    ProcessNetworkPackets(world, 1.f / 60.f, engine.memory().frame());
    UpdateStateReplicationSystem(world, 1.f / 60.f, engine.memory().frame(),
                                 world.frame_commands());
    world.frame_commands().playback(world);

    ENGINE_ASSERT(world.has<WindowShatteredTag>(window), "server did not shatter the window");

    const ClientNetInboxComponent* inbox1 = world.get<ClientNetInboxComponent>(client1);
    const ClientNetInboxComponent* inbox2 = world.get<ClientNetInboxComponent>(client2);
    ENGINE_ASSERT(inbox1 && inbox2, "inboxes missing");
    ENGINE_ASSERT(inbox1->last_shattered_network_id == 42, "client 1 missed WindowShattered");
    ENGINE_ASSERT(inbox2->last_shattered_network_id == 0, "client 2 received out-of-AOI shatter");
    ENGINE_ASSERT(inbox2->received_count == 0, "client 2 inbox not empty");

    ClientInputCommand move{};
    move.sequence         = 7;
    move.velocity         = float3{2.f, 0.f, 0.f};
    move.shoot_network_id = 0;
    submit_client_input(world, ep_c1, move);
    const float3 before = world.get<ServerStateComponent>(client1)->server_position;
    UpdateStateReplicationSystem(world, 0.5f, engine.memory().frame(), world.frame_commands());
    const ClientPredictionComponent* pred = world.get<ClientPredictionComponent>(client1);
    ENGINE_ASSERT(pred != nullptr, "prediction missing");
    ENGINE_ASSERT(pred->predicted_position.x > before.x, "client prediction did not advance");

    std::printf("MICRO-PHASE 7 sandbox passed\n");
    std::printf("  AOI c1 count           : %u\n", aoi_c1.count);
    std::printf("  window shattered       : yes\n");
    std::printf("  c1 shattered net id    : %u\n", inbox1->last_shattered_network_id);
    std::printf("  c2 received count      : %u\n", inbox2->received_count);
    std::printf("  predicted x            : %.3f (was %.3f)\n",
                static_cast<double>(pred->predicted_position.x),
                static_cast<double>(before.x));

    engine.shutdown();
    return 0;
}
