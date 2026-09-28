#include "mission/MissionDataLoader.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "mission/DialogSystem.h"
#include "mission/MissionMarker.h"
#include "mission/ObjectiveTracker.h"

#include <cstdio>
#include <cstring>

namespace engine {

namespace {

void copy_text(char* dst, u32 cap, const char* src) {
    if (cap == 0) {
        return;
    }
    u32 i = 0;
    if (src) {
        for (; src[i] != '\0' && i + 1 < cap; ++i) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
    for (u32 z = i + 1; z < cap; ++z) {
        dst[z] = '\0';
    }
}

void fill_header(MissionFileHeader* h, u32 missions) {
    std::memset(h, 0, sizeof(*h));
    std::memcpy(h->magic, "LEONMISS", 8);
    h->version       = 1;
    h->mission_count = missions;
}

void spawn_objective(World& world, const MissionObjectiveComponent& obj, float trigger_m, u8 tracker_type) {
    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "mission_objective";
    if (obj.objective_type == 0) {
        GotoObjectiveComponent g{};
        g.mission_id       = obj.mission_id;
        g.objective_index  = obj.objective_index;
        g.target_location  = obj.target_location;
        g.radius_m         = trigger_m < 0.f ? 8.f : trigger_m;
        g.is_completed     = false;
        (void)world.instantiate(req, obj, g);
    } else if (obj.objective_type == 1) {
        ThreatenAimObjectiveComponent t{};
        t.mission_id      = obj.mission_id;
        t.objective_index = obj.objective_index;
        t.clerk_entity_id = obj.target_entity_id;
        t.aim_dot_min     = 0.92f;
        t.max_range_m     = 12.f;
        t.is_completed    = false;
        KillObjectiveComponent k{};
        k.mission_id       = obj.mission_id;
        k.objective_index  = obj.objective_index;
        k.target_entity_id = obj.target_entity_id;
        k.required_count   = 1;
        k.current_count    = 0;
        k.is_completed     = false;
        (void)world.instantiate(req, obj, t, k);
    } else if (obj.objective_type == 2) {
        CollectObjectiveComponent c{};
        c.mission_id      = obj.mission_id;
        c.objective_index = obj.objective_index;
        c.item_type_id    = obj.target_entity_id;
        c.required_count  = obj.required_count;
        c.is_completed    = false;
        (void)world.instantiate(req, obj, c);
    } else if (obj.objective_type == 3) {
        DeliverObjectiveComponent d{};
        d.mission_id      = obj.mission_id;
        d.objective_index = obj.objective_index;
        d.item_type_id    = obj.target_entity_id;
        d.dropoff_ws      = obj.target_location;
        d.radius_m        = trigger_m;
        d.is_completed    = false;
        (void)world.instantiate(req, obj, d);
    } else if (obj.objective_type == 4) {
        EscortObjectiveComponent es{};
        es.mission_id       = obj.mission_id;
        es.objective_index  = obj.objective_index;
        es.escort_entity_id = obj.target_entity_id;
        es.destination_ws   = obj.target_location;
        es.radius_m         = trigger_m;
        es.is_completed     = false;
        (void)world.instantiate(req, obj, es);
    } else if (obj.objective_type == 5) {
        StealVehicleObjectiveComponent s{};
        s.mission_id        = obj.mission_id;
        s.objective_index   = obj.objective_index;
        s.chassis_entity_id = obj.target_entity_id;
        s.is_completed      = false;
        (void)world.instantiate(req, obj, s);
    } else if (obj.objective_type == 6) {
        DestroyObjectiveComponent d{};
        d.mission_id       = obj.mission_id;
        d.objective_index  = obj.objective_index;
        d.target_entity_id = obj.target_entity_id;
        d.is_completed     = false;
        (void)world.instantiate(req, obj, d);
    } else {
        (void)world.instantiate(req, obj);
    }

    ObjectiveTrackerComponent tr{};
    tr.mission_id          = obj.mission_id;
    tr.objective_index     = obj.objective_index;
    tr.tracker_type        = tracker_type;
    tr.target_entity_id    = obj.target_entity_id;
    tr.target_location     = obj.target_location;
    tr.trigger_distance_m  = trigger_m;
    tr.is_triggered        = false;
    InstantiationRequest treq{};
    treq.domain      = InstantiationDomain::PersistentWorld;
    treq.debug_label = "objective_tracker";
    (void)instantiate_objective_tracker(world, treq, tr);

    MissionMarkerComponent mk{};
    mk.mission_id       = obj.mission_id;
    mk.objective_index  = obj.objective_index;
    mk.world_position   = obj.target_location;
    mk.marker_type      = 0;
    mk.icon_id          = 40 + obj.objective_index;
    mk.color_rgb        = float3{1.f, 0.85f, 0.12f};
    mk.is_visible       = false;
    mk.pulse_frequency_hz = 1.2f;
    InstantiationRequest mreq{};
    mreq.domain      = InstantiationDomain::PersistentWorld;
    mreq.debug_label = "mission_marker";
    (void)instantiate_mission_marker(world, mreq, mk);
}

} // namespace

bool WriteTestConvenienceStoreMission(const char* filepath) {
    ENGINE_ASSERT(filepath != nullptr, "mission path");
    FILE* f = std::fopen(filepath, "wb");
    if (!f) {
        return false;
    }

    MissionFileHeader header{};
    fill_header(&header, 1);

    MissionFileEntry entry{};
    std::memset(&entry, 0, sizeof(entry));
    entry.definition.mission_id = 1201;
    copy_text(entry.definition.name, 64, "The Convenience Store Robbery");
    copy_text(entry.definition.description, 256,
              "Hit the all-night market, empty the register, and lose the heat.");
    entry.definition.giver_entity_id         = 0;
    entry.definition.start_location          = float3{100.f, 0.f, 200.f};
    entry.definition.prerequisite_mission_id = 0;
    entry.definition.reward_money            = 500;
    entry.definition.reward_reputation       = 10;
    entry.definition.reward_weapon_type_id   = 0;
    entry.definition.is_repeatable           = false;
    entry.definition.difficulty_level        = 2;
    entry.objective_count                    = 3;
    entry.dialog_node_count                  = 3;
    entry.response_count                     = 2;

    MissionObjectiveComponent objs[3]{};
    objs[0].mission_id       = 1201;
    objs[0].objective_index  = 0;
    objs[0].objective_type   = 0;
    objs[0].target_location  = float3{150.f, 0.f, 250.f};
    objs[0].required_count   = 1;
    copy_text(objs[0].description, 128, "Go to the convenience store");

    objs[1].mission_id       = 1201;
    objs[1].objective_index  = 1;
    objs[1].objective_type   = 1;
    objs[1].target_location  = float3{150.f, 0.f, 250.f};
    objs[1].required_count   = 1;
    copy_text(objs[1].description, 128, "Point weapon at clerk");

    objs[2].mission_id       = 1201;
    objs[2].objective_index  = 2;
    objs[2].objective_type   = 0;
    objs[2].target_location  = float3{150.f, 0.f, 250.f};
    objs[2].required_count   = 1;
    copy_text(objs[2].description, 128, "Escape the police");

    DialogNodeComponent nodes[3]{};
    nodes[0].dialog_id      = 1201;
    nodes[0].node_id        = 1;
    copy_text(nodes[0].speaker_name, 32, "Stranger");
    copy_text(nodes[0].dialog_text, 256, "Register's fat after midnight. You in?");
    nodes[0].audio_event_id = 12011;
    nodes[0].duration_s     = 0.8f;
    nodes[0].next_node_id   = 2;
    nodes[0].is_player_choice = false;

    nodes[1].dialog_id      = 1201;
    nodes[1].node_id        = 2;
    copy_text(nodes[1].speaker_name, 32, "Clerk");
    copy_text(nodes[1].dialog_text, 256, "I don't want any trouble.");
    nodes[1].audio_event_id = 12012;
    nodes[1].duration_s     = 1.0f;
    nodes[1].next_node_id   = 3;
    nodes[1].is_player_choice = true;

    nodes[2].dialog_id      = 1201;
    nodes[2].node_id        = 3;
    copy_text(nodes[2].speaker_name, 32, "Stranger");
    copy_text(nodes[2].dialog_text, 256, "Walk out calm. Cops are two blocks out.");
    nodes[2].audio_event_id = 12013;
    nodes[2].duration_s     = 0.8f;
    nodes[2].next_node_id   = 0;
    nodes[2].is_player_choice = false;

    DialogResponseComponent resp[2]{};
    resp[0].dialog_id             = 1201;
    resp[0].parent_node_id        = 2;
    resp[0].response_index        = 0;
    copy_text(resp[0].response_text, 128, "Empty it. Now.");
    resp[0].next_node_id          = 3;
    resp[0].prerequisite_flag_id  = 0;
    resp[0].increases_reputation  = false;

    resp[1].dialog_id             = 1201;
    resp[1].parent_node_id        = 2;
    resp[1].response_index        = 1;
    copy_text(resp[1].response_text, 128, "Forget it.");
    resp[1].next_node_id          = 0;
    resp[1].prerequisite_flag_id  = 0;
    resp[1].increases_reputation  = false;

    const bool ok = std::fwrite(&header, sizeof(header), 1, f) == 1
                 && std::fwrite(&entry, sizeof(entry), 1, f) == 1
                 && std::fwrite(objs, sizeof(objs[0]), 3, f) == 3
                 && std::fwrite(nodes, sizeof(nodes[0]), 3, f) == 3
                 && std::fwrite(resp, sizeof(resp[0]), 2, f) == 2;
    std::fclose(f);
    return ok;
}

bool LoadMissionsFromFile(World& world, const char* filepath, CommandBuffer& cmd) {
    (void)cmd;
    ENGINE_ASSERT(filepath != nullptr, "mission path");
    FILE* f = std::fopen(filepath, "rb");
    if (!f) {
        return false;
    }

    MissionFileHeader header{};
    if (std::fread(&header, sizeof(header), 1, f) != 1) {
        std::fclose(f);
        return false;
    }
    if (std::memcmp(header.magic, "LEONMISS", 8) != 0 || header.version != 1) {
        std::fclose(f);
        return false;
    }

    InstantiationRequest req{};
    req.domain      = InstantiationDomain::PersistentWorld;
    req.debug_label = "mission_bundle";

    u32 loaded = 0;
    for (u32 m = 0; m < header.mission_count; ++m) {
        MissionFileEntry entry{};
        if (std::fread(&entry, sizeof(entry), 1, f) != 1) {
            std::fclose(f);
            return false;
        }

        MissionStateComponent state{};
        state.mission_id              = entry.definition.mission_id;
        state.current_state           = kMissionNotStarted;
        state.current_objective_index = 0;
        state.time_started            = 0.f;
        state.time_limit_s            = 0.f;
        state.is_timer_running        = false;
        (void)instantiate_mission(world, req, entry.definition, state);

        for (u32 i = 0; i < entry.objective_count; ++i) {
            MissionObjectiveComponent obj{};
            if (std::fread(&obj, sizeof(obj), 1, f) != 1) {
                std::fclose(f);
                return false;
            }
            u8 tracker = 4;
            float trig = 8.f;
            if (obj.objective_index == 1) {
                tracker = 0;
                trig    = 12.f;
            } else if (obj.objective_index == 2) {
                tracker = 4;
                trig    = -200.f; // escape: player >= 200 m from store
            }
            spawn_objective(world, obj, trig, tracker);
        }
        for (u32 i = 0; i < entry.dialog_node_count; ++i) {
            DialogNodeComponent node{};
            if (std::fread(&node, sizeof(node), 1, f) != 1) {
                std::fclose(f);
                return false;
            }
            (void)instantiate_dialog_node(world, req, node);
        }
        for (u32 i = 0; i < entry.response_count; ++i) {
            DialogResponseComponent response{};
            if (std::fread(&response, sizeof(response), 1, f) != 1) {
                std::fclose(f);
                return false;
            }
            (void)instantiate_dialog_response(world, req, response);
        }

        DialogStateComponent ds{};
        ds.active_dialog_id = entry.definition.mission_id;
        ds.current_node_id  = 1;
        ds.is_active        = false;
        ds.elapsed_time_s   = 0.f;
        (void)instantiate_dialog_state(world, req, ds);
        ++loaded;
    }
    std::fclose(f);

    bool have_tel = false;
    for (Entity e : world.query<MissionTelemetryComponent>()) {
        MissionTelemetryComponent* t = world.get<MissionTelemetryComponent>(e);
        if (t) {
            t->missions_loaded += loaded;
            have_tel = true;
        }
        break;
    }
    if (!have_tel) {
        MissionTelemetryComponent tel{};
        tel.missions_loaded = loaded;
        InstantiationRequest treq{};
        treq.domain      = InstantiationDomain::PersistentWorld;
        treq.debug_label = "mission_telemetry";
        (void)world.instantiate(treq, tel);
    }

    bool have_clock = false;
    for (Entity e : world.query<MissionSimClockComponent>()) {
        have_clock = world.is_alive(e);
        break;
    }
    if (!have_clock) {
        MissionSimClockComponent clock{};
        clock.world_time_s = 0.f;
        InstantiationRequest creq{};
        creq.domain      = InstantiationDomain::PersistentWorld;
        creq.debug_label = "mission_clock";
        (void)world.instantiate(creq, clock);
    }
    return loaded == header.mission_count;
}

} // namespace engine
