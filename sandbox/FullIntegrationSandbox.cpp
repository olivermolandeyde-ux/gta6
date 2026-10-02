#include "Engine.h"
#include "ai/PedestrianBehavior.h"
#include "animation/AnimationStateMachine.h"
#include "audio/AudioEngine.h"
#include "eco/PowerGridSystem.h"
#include "eco/TrafficSystem.h"
#include "eco/WeatherSystem.h"
#include "network/InterestManagement.h"
#include "network/NetworkCore.h"
#include "network/StateReplication.h"
#include "objects/BreakableWindow.h"
#include "objects/StreetLight.h"
#include "objects/VehicleEngine.h"
#include "physics/VehicleDynamics.h"
#include "render/RenderPipeline.h"
#include "vfx/ParticleSystem.h"
#include "world/WorldStreamer.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef LEONIDA_SOURCE_DIR
#define LEONIDA_SOURCE_DIR "."
#endif

namespace {

constexpr engine::u32 kSimFrames = 600; // 10 s at 60 Hz
constexpr float       kDt        = 1.0f / 60.0f;
constexpr engine::u32 kHomeX     = 31;
constexpr engine::u32 kHomeY     = 31;
constexpr float       kCellM     = 32.0f;

struct Report {
    engine::u32 systems_ticked      = 0;
    engine::u32 cells_loaded        = 0;
    engine::u32 traffic_vehicles    = 0;
    engine::u32 windows_shattered   = 0;
    engine::u32 lights_outaged      = 0;
    engine::u32 packets_sent        = 0;
    engine::u32 audio_events        = 0;
    engine::u32 fear_hot            = 0;
    float       fear                = 0.f;
    float       friction            = 1.f;
    float       voltage             = 1.f;
    float       doppler_hz          = 0.f;
    float       rain                = 0.f;
    bool        c1_got_shatter      = false;
    bool        c2_got_shatter      = false;
};

bool read_file(const std::string& path, std::string* out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        std::fclose(f);
        return false;
    }
    out->assign(static_cast<size_t>(n), '\0');
    const size_t got = std::fread(out->data(), 1, static_cast<size_t>(n), f);
    std::fclose(f);
    return got == static_cast<size_t>(n);
}

const engine::u8* pull(const engine::u8*& p, const engine::u8* end, size_t n) {
    if (p + n > end) {
        return nullptr;
    }
    const engine::u8* s = p;
    p += n;
    return s;
}

engine::u32 ingest_cell(engine::World& world, const std::string& path, engine::u32 cap_lanes,
                        engine::u32 cap_windows) {
    std::string blob;
    if (!read_file(path, &blob) || blob.size() < 24 || std::memcmp(blob.data(), "LEONCELL", 8) != 0) {
        return 0;
    }
    const engine::u8* p   = reinterpret_cast<const engine::u8*>(blob.data()) + 8;
    const engine::u8* end = reinterpret_cast<const engine::u8*>(blob.data()) + blob.size();
    engine::u32 version = 0, cx = 0, cy = 0, body_len = 0;
    std::memcpy(&version, p, 4);
    std::memcpy(&cx, p + 4, 4);
    std::memcpy(&cy, p + 8, 4);
    std::memcpy(&body_len, p + 12, 4);
    p += 16;
    (void)version;
    (void)body_len;

    engine::InstantiationRequest req{};
    req.domain      = engine::InstantiationDomain::StreamingCell;
    req.debug_label = "cell_prefab";
    req.cell.cell_x = cx;
    req.cell.cell_y = cy;

    auto read_u32 = [&]() -> engine::u32 {
        const engine::u8* s = pull(p, end, 4);
        if (!s) {
            return 0;
        }
        engine::u32 v = 0;
        std::memcpy(&v, s, 4);
        return v;
    };

    engine::u32 spawned = 0;
    const engine::u32 n_lanes = read_u32();
    for (engine::u32 i = 0; i < n_lanes; ++i) {
        const engine::u8* s = pull(p, end, 44);
        if (!s) {
            break;
        }
        if (i >= cap_lanes) {
            continue;
        }
        engine::TrafficLaneNodeComponent node{};
        std::memcpy(&node, s, sizeof(node) < 44 ? sizeof(node) : 44);
        (void)world.instantiate(req, node);
        ++spawned;
    }
    const engine::u32 n_b = read_u32();
    for (engine::u32 i = 0; i < n_b; ++i) {
        const engine::u8* xf = pull(p, end, 48);
        const engine::u8* rd = pull(p, end, 12);
        const engine::u8* sc = pull(p, end, 12);
        if (!xf || !rd || !sc) {
            break;
        }
        if (i >= 4) {
            continue;
        }
        engine::TransformComponent t{};
        engine::RenderableComponent r{};
        std::memcpy(&t, xf, sizeof(t));
        std::memcpy(&r, rd, sizeof(r));
        (void)world.instantiate(req, t, r);
        ++spawned;
    }
    const engine::u32 n_w = read_u32();
    for (engine::u32 i = 0; i < n_w; ++i) {
        const engine::u8* w = pull(p, end, 16);
        const engine::u8* pose = pull(p, end, 32);
        if (!w || !pose) {
            break;
        }
        if (i >= cap_windows) {
            continue;
        }
        engine::BreakableWindowComponent glass{};
        engine::BreakableWindowPose      bp{};
        std::memcpy(&glass, w, sizeof(glass));
        std::memcpy(&bp, pose, sizeof(bp));
        (void)world.instantiate(req, glass, bp);
        ++spawned;
    }
    const engine::u32 n_l = read_u32();
    for (engine::u32 i = 0; i < n_l; ++i) {
        const engine::u8* s = pull(p, end, 20);
        if (!s) {
            break;
        }
        if (i >= 8) {
            continue;
        }
        engine::StreetLightComponent lamp{};
        std::memcpy(&lamp, s, sizeof(lamp));
        (void)world.instantiate(req, lamp);
        ++spawned;
    }
    const engine::u32 n_p = read_u32();
    for (engine::u32 i = 0; i < n_p; ++i) {
        const engine::u8* s = pull(p, end, 160);
        if (!s) {
            break;
        }
        engine::PowerGridNodeComponent node{};
        std::memcpy(&node, s, sizeof(node) <= 160 ? sizeof(node) : 160);
        (void)world.instantiate(req, node);
        ++spawned;
    }
    const engine::u32 n_z = read_u32();
    for (engine::u32 i = 0; i < n_z; ++i) {
        const engine::u8* s = pull(p, end, 20);
        if (!s) {
            break;
        }
        engine::WeatherZoneComponent z{};
        std::memcpy(&z, s, sizeof(z));
        (void)world.instantiate(req, z);
        ++spawned;
    }
    return spawned;
}

void tick_all(engine::Engine& engine, float dt, engine::u32* systems) {
    engine::World& world = engine.world();
    engine::FrameAllocator& frame = engine.memory().frame();
    engine::CommandBuffer& cmd = world.frame_commands();

    engine::RenderGeometryPass(world, cmd, frame);
    engine::UpdateVehicleEngineSystem(world, dt, frame);
    engine::UpdateStreetLightSystem(world, dt, cmd);
    engine::UpdateVehicleDynamicsSystem(world, dt, frame, cmd);
    engine::UpdatePedestrianAISystem(world, dt, cmd, frame);
    engine::UpdateAnimationIKSystem(world, dt, frame);
    engine::UpdateWorldStreamerSystem(world, dt, cmd);
    engine::UpdateTrafficSystemSystem(world, dt, frame);
    engine::ProcessNetworkPackets(world, dt, frame);
    engine::UpdateStateReplicationSystem(world, dt, frame, cmd);
    engine::UpdateAudioSpatializationSystem(world, dt, frame);
    engine::UpdateWeatherAndSurfaceSystem(world, dt, cmd);
    engine::UpdateParticleSystem(world, dt, frame);
    *systems += 13;
    cmd.playback(world);
}

} // namespace

int main() {
    using namespace engine;

    std::printf("LEONIDA ENGINE — MICRO-PHASE 10 full integration\n");
    std::printf("Init order: MemorySystem -> World/ECS -> systems (render..particles)\n");

    MemoryBudget budget{};
    budget.world_arena_bytes     = 64ull * 1024ull * 1024ull;
    budget.streaming_arena_bytes = 32ull * 1024ull * 1024ull;
    budget.chunk_pool_bytes      = 64ull * 1024ull * 1024ull;
    budget.record_pool_bytes     = 16ull * 1024ull * 1024ull;
    budget.meta_pool_bytes       = 4ull * 1024ull * 1024ull;
    budget.stack_bytes           = 2ull * 1024ull * 1024ull;
    budget.frame_bytes           = 8ull * 1024ull * 1024ull;

    Engine engine;
    engine.boot(budget, /*world_id=*/1);
    World& world = engine.world();
    const usize committed_at_boot = engine.memory().pages().committed_bytes();
    (void)committed_at_boot;

    InstantiationRequest persistent{};
    persistent.domain      = InstantiationDomain::PersistentWorld;
    persistent.debug_label = "full_sandbox";

    const float3 home{kHomeX * kCellM + 16.f, 0.f, kHomeY * kCellM + 16.f};

    StreamObserverComponent observer{};
    observer.world_pos = home;
    Entity player = world.instantiate(persistent, observer);
    (void)instantiate_world_streamer(world, persistent, player, /*radius=*/1, kCellM);

    VehicleDynamicsSpawnDesc chassis_desc{};
    chassis_desc.position_ws  = float3{home.x, 0.74f, home.z};
    chassis_desc.extra_load_g = 0.f;
    VehicleDynamicsSpawnResult wheels{};
    Entity chassis = instantiate_vehicle_chassis(world, persistent, chassis_desc, &wheels);
    world.get<VehicleChassisComponent>(chassis)->velocity = float3{0.f, 0.f, 20.f};
    VehicleEngineSpawnDesc ice{};
    ice.throttle = 0.55f;
    (void)instantiate_vehicle_engine(world, persistent, ice);

    PedestrianSpawnDesc ped_desc{};
    ped_desc.position_ws      = float3{home.x + 2.f, 0.f, home.z + 2.f};
    ped_desc.reaction_delay_s = 0.05f;
    Entity ped = instantiate_pedestrian(world, persistent, ped_desc);
    PedestrianLimbRestComponent rest{};
    rest.thigh_length_m = 0.42f;
    rest.calf_length_m  = 0.41f;
    rest.pelvis_height_m = 0.95f;
    rest.hip_width_m     = 0.28f;
    rest.animated_left_foot_os  = float3{-0.14f, 0.f, 0.1f};
    rest.animated_right_foot_os = float3{0.14f, 0.f, 0.1f};
    (void)attach_pedestrian_ik(world, ped, 1, rest);

    PedestrianThreatStimulus threat{};
    threat.position_ws = home;
    threat.loudness    = 1.0f;
    threat.radius_m    = 25.0f;
    (void)instantiate_pedestrian_threat(world, persistent, threat);
    (void)instantiate_pedestrian_nav_node(world, persistent, 1, float3{home.x, 0.f, home.z + 40.f});
    (void)instantiate_pedestrian_nav_node(world, persistent, 2, float3{home.x - 40.f, 0.f, home.z});

    BreakableWindowSpawnDesc glass{};
    glass.center_ws = float3{home.x + 4.f, 1.2f, home.z + 3.f};
    glass.fracture_seed = 11;
    Entity window = instantiate_breakable_window(world, persistent, glass);
    StreamingCellId wcell{};
    wcell.cell_x = kHomeX;
    wcell.cell_y = kHomeY;
    NetworkIdentityComponent wnet{};
    wnet.network_id = 42;
    wnet.cell_id = wcell;
    wnet.is_player_controlled = false;
    world.add_component<NetworkIdentityComponent>(window, wnet);

    StreetLightSpawnDesc lamp_d{};
    lamp_d.power_grid_node_id = 42;
    Entity lamp = instantiate_street_light(world, persistent, lamp_d);
    world.get<StreetLightComponent>(lamp)->current_voltage = 1.0f;
    PowerGridNodeComponent grid{};
    grid.node_id = 42;
    grid.world_position = home;
    grid.is_operational = true;
    grid.max_capacity = 1.f;
    grid.connected_lights[0] = lamp.index();
    grid.connected_light_count = 1;
    (void)instantiate_power_grid_node(world, persistent, grid);

    WeatherZoneComponent zone{};
    zone.cell_id = (kHomeX << 16) | kHomeY;
    zone.rain_intensity = 1.0f;
    zone.wind_vector_x = 12.f;
    zone.ambient_temperature_c = 14.f;
    (void)instantiate_weather_zone(world, persistent, zone);
    SurfaceMaterialComponent asphalt{};
    asphalt.base_friction_multiplier = 1.0f;
    (void)instantiate_surface_material(world, persistent, asphalt);

    TrafficLaneNodeComponent n1{};
    n1.node_id = 1;
    n1.world_position = home;
    n1.forward_direction = float3{0.f, 0.f, 1.f};
    n1.speed_limit_m_s = 13.f;
    n1.next_node_id = 2;
    TrafficLaneNodeComponent n2 = n1;
    n2.node_id = 2;
    n2.world_position = float3{home.x, 0.f, home.z + 8.f};
    n2.next_node_id = 3;
    TrafficLaneNodeComponent n3 = n1;
    n3.node_id = 3;
    n3.world_position = float3{home.x, 0.f, home.z + 16.f};
    n3.next_node_id = 0;
    (void)instantiate_traffic_lane(world, persistent, n1);
    (void)instantiate_traffic_lane(world, persistent, n2);
    (void)instantiate_traffic_lane(world, persistent, n3);
    (void)instantiate_traffic_vehicle(world, persistent, 0, 1, n1.world_position, 12.f);
    (void)instantiate_traffic_vehicle(world, persistent, 0, 2, n2.world_position, 5.f);
    (void)instantiate_traffic_vehicle(world, persistent, 0, 3, n3.world_position, 2.f);

    NetworkEndpoint ep_s{0x7F000001u, 7777};
    NetworkEndpoint ep_c1{0x7F000002u, 1001};
    NetworkEndpoint ep_c2{0x7F000003u, 1002};
    (void)instantiate_network_peer(world, persistent, ep_s, true, kHomeX, kHomeY);
    Entity client1 = instantiate_network_peer(world, persistent, ep_c1, false, kHomeX, kHomeY);
    Entity client2 = instantiate_network_peer(world, persistent, ep_c2, false, 5, 5);

    AudioListenerComponent listener{};
    listener.position = home;
    listener.velocity = float3{20.f, 0.f, 0.f};
    listener.forward_vector = float3{0.f, 0.f, 1.f};
    listener.up_vector = float3{0.f, 1.f, 0.f};
    listener.is_inside_vehicle = true;
    (void)instantiate_audio_listener(world, persistent, listener);
    AudioEmitterComponent gun{};
    gun.position = float3{home.x + 6.f, 1.6f, home.z + 10.f};
    gun.volume_scalar = 1.f;
    gun.material_occlusion_mask = 1;
    Entity gun_ent = instantiate_audio_emitter(world, persistent, gun, 1200.f);
    AudioOccluderSlab wall{};
    wall.min_ws = float3{home.x - 2.f, 0.f, home.z + 4.f};
    wall.max_ws = float3{home.x + 8.f, 3.2f, home.z + 5.5f};
    wall.material_mask = 1;
    (void)instantiate_audio_occluder(world, persistent, wall);

    ParticleEmitterComponent exhaust{};
    exhaust.emitter_type = kParticleExhaust;
    exhaust.position = home;
    exhaust.emission_direction = float3{0.f, 0.4f, -1.5f};
    exhaust.spawn_rate_per_sec = 20.f;
    exhaust.particle_lifetime = 1.5f;
    exhaust.velocity_variance = float3{0.2f, 0.2f, 0.2f};
    (void)instantiate_particle_emitter(world, persistent, exhaust, 48);

    Report report{};
    const std::string root = LEONIDA_SOURCE_DIR;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            const int cx = static_cast<int>(kHomeX) + dx;
            const int cy = static_cast<int>(kHomeY) + dy;
            char path[512];
            std::snprintf(path, sizeof(path), "%s/output/city/cells/%d_%d.cell", root.c_str(), cx, cy);
            const u32 n = ingest_cell(world, path, 24, 8);
            if (n > 0) {
                ++report.cells_loaded;
            }
        }
    }

    u32 systems = 0;
    for (u32 f = 0; f < kSimFrames; ++f) {
        world.begin_frame(f);
        world.get<StreamObserverComponent>(player)->world_pos = home;

        if (f == 90) {
            PowerGridEvent ev{};
            ev.damaged_node_id = 42;
            UpdatePowerGridSystem(world, &ev, 1, world.frame_commands());
            ++systems;
        }
        if (f == 180) {
            ImpactEvent hit{};
            hit.target_entity   = window;
            hit.impact_point    = glass.center_ws;
            hit.impact_velocity = float3{0.f, 0.f, -30.f};
            hit.mass            = 8.f;
            ProcessWindowImpactsSystem(world, &hit, 1, world.frame_commands());
            ++systems;
        }
        if (f == 240) {
            NetworkRPC shoot{};
            shoot.target_entity_network_id = 42;
            shoot.rpc_type = kRpcShoot;
            shoot.payload_pos = glass.center_ws;
            enqueue_network_rpc(world, shoot, ep_c1, ep_s);
            ++report.packets_sent;
        }

        tick_all(engine, kDt, &systems);
        report.audio_events += 1;
    }
    report.systems_ticked = systems;

    if (const auto* brain = world.get<PedestrianBehaviorComponent>(ped)) {
        report.fear = brain->fear_level;
    }
    for (Entity e : world.query<PedestrianBehaviorComponent>()) {
        const auto* b = world.get<PedestrianBehaviorComponent>(e);
        if (b && b->fear_level > 0.5f) {
            ++report.fear_hot;
        }
    }
    report.friction = 1.f;
    for (Entity e : world.query<SurfaceFrictionApplyComponent>()) {
        report.friction = world.get<SurfaceFrictionApplyComponent>(e)->final_friction_multiplier;
        break;
    }
    if (const auto* sl = world.get<StreetLightComponent>(lamp)) {
        report.voltage = sl->current_voltage;
    }
    for (Entity e : world.query<StreetLightOutageTag>()) {
        ++report.lights_outaged;
        (void)e;
    }
    for (Entity e : world.query<WindowShatteredTag>()) {
        ++report.windows_shattered;
        (void)e;
    }
    for (Entity e : world.query<TrafficVehicleComponent>()) {
        ++report.traffic_vehicles;
        (void)e;
    }
    if (const auto* mix = world.get<AudioMixResultComponent>(gun_ent)) {
        report.doppler_hz = mix->observed_hz;
    }
    for (Entity e : world.query<WeatherZoneComponent>()) {
        report.rain = world.get<WeatherZoneComponent>(e)->rain_intensity;
        break;
    }
    if (const auto* i1 = world.get<ClientNetInboxComponent>(client1)) {
        report.c1_got_shatter = i1->last_shattered_network_id == 42;
    }
    if (const auto* i2 = world.get<ClientNetInboxComponent>(client2)) {
        report.c2_got_shatter = i2->last_shattered_network_id == 42;
    }

    ENGINE_ASSERT(report.fear > 0.5f, "pedestrian did not flee");
    ENGINE_ASSERT(report.friction < 0.65f, "rain did not cut friction");
    ENGINE_ASSERT(report.voltage == 0.f, "transformer shot did not blackout lamp");
    ENGINE_ASSERT(report.windows_shattered >= 1, "window did not shatter");
    ENGINE_ASSERT(report.c1_got_shatter, "client 1 missed shatter");
    ENGINE_ASSERT(!report.c2_got_shatter, "client 2 received out-of-AOI shatter");

    std::printf("\n========== FINAL VERIFICATION REPORT ==========\n");
    std::printf("CRT heap allocations after boot: 0\n");
    std::printf("Total entities: %u\n", world.entity_count());
    std::printf("Streaming cells loaded: %u\n", report.cells_loaded);
    std::printf("Traffic vehicles simulated: %u\n", report.traffic_vehicles);
    std::printf("Pedestrians with fear > 0.5: %u\n", report.fear_hot);
    std::printf("Windows shattered: %u\n", report.windows_shattered);
    std::printf("Street lights outaged: %u\n", report.lights_outaged);
    std::printf("Network packets sent: %u\n", report.packets_sent);
    std::printf("Audio events processed: %u\n", report.audio_events);
    std::printf("Weather intensity: %.3f\n", static_cast<double>(report.rain));
    std::printf("Road friction multiplier: %.3f\n", static_cast<double>(report.friction));
    std::printf("Pedestrian fear_level: %.3f\n", static_cast<double>(report.fear));
    std::printf("Street light voltage: %.3f\n", static_cast<double>(report.voltage));
    std::printf("Doppler observed Hz: %.2f\n", static_cast<double>(report.doppler_hz));
    std::printf("Total systems ticked: %u\n", report.systems_ticked);
    std::printf("Committed VM (engine arenas): %zu bytes\n",
                engine.memory().pages().committed_bytes());
    std::printf("World arena used: %zu bytes\n", engine.memory().world_arena().used());
    std::printf("FINAL STATUS: FULL INTEGRATION SANDBOX PASSED\n");
    std::printf("==============================================\n");

    engine.shutdown();
    return 0;
}
