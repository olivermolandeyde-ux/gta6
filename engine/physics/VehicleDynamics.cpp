#include "physics/VehicleDynamics.h"

#include "core/Assert.h"
#include "ecs/World.h"
#include "memory/MemorySystem.h"
#include "objects/VehicleEngine.h"

#include <cmath>

namespace engine {

namespace {

constexpr float kWheelInertia     = 0.85f;   // kg·m^2, one corner
constexpr float kPacejkaB         = 10.0f;
constexpr float kPacejkaC         = 1.30f;
constexpr u32   kMaxWheelsPerBody = 4;

[[nodiscard]] float3x3 orthonormalize(float3x3 r) {
    r.c2 = float3_normalize_or(r.c2, float3{0.f, 0.f, 1.f});
    r.c0 = float3_normalize_or(float3_cross(r.c1, r.c2), float3{1.f, 0.f, 0.f});
    // Recompute up so the basis stays right-handed after the right-axis fix.
    r.c1 = float3_normalize_or(float3_cross(r.c2, r.c0), float3{0.f, 1.f, 0.f});
    r.c0 = float3_cross(r.c1, r.c2);
    return r;
}

// Integrate R with ω via first-order skew: R ← R + dt * [ω]_× R, then Gram-Schmidt.
[[nodiscard]] float3x3 integrate_rotation(float3x3 r, float3 omega, float dt) {
    const float3 d0 = float3_cross(omega, r.c0);
    const float3 d1 = float3_cross(omega, r.c1);
    const float3 d2 = float3_cross(omega, r.c2);
    r.c0 = float3_add(r.c0, float3_scale(d0, dt));
    r.c1 = float3_add(r.c1, float3_scale(d1, dt));
    r.c2 = float3_add(r.c2, float3_scale(d2, dt));
    return orthonormalize(r);
}

// Magic-formula sample: D·sin(C·atan(B·x)), D = μ Fz.
[[nodiscard]] float pacejka(float x, float mu_fz) {
    return mu_fz * std::sin(kPacejkaC * std::atan(kPacejkaB * x));
}

void apply_ackermann(VehicleSteeringComponent* steer, WheelNodeComponent* wheel,
                     const VehicleTireContactComponent* tire) {
    const float delta = clampf(steer->bicycle_steer_rad, -steer->max_steer_rad, steer->max_steer_rad);
    steer->bicycle_steer_rad = delta;
    if (!tire->is_front) {
        wheel->steer_angle_rad = 0.f;
        return;
    }
    if (std::fabs(delta) < 1.0e-5f) {
        steer->ackermann_inner_rad = 0.f;
        steer->ackermann_outer_rad = 0.f;
        wheel->steer_angle_rad     = 0.f;
        return;
    }
    const float L = steer->wheelbase_m;
    const float T = steer->track_width_m;
    const float R = L / std::tan(delta); // signed turn radius to bicycle COM
    const float inner = std::atan(L / (R - std::copysign(T * 0.5f, R)));
    const float outer = std::atan(L / (R + std::copysign(T * 0.5f, R)));
    steer->ackermann_inner_rad = inner;
    steer->ackermann_outer_rad = outer;
    // Positive bicycle delta = turn left. Inner wheel is the left when turning left.
    const bool inner_side = (delta > 0.f) ? (tire->is_right == 0) : (tire->is_right != 0);
    wheel->steer_angle_rad = inner_side ? inner : outer;
}

} // namespace

Entity instantiate_vehicle_chassis(World& world, const InstantiationRequest& request,
                                   const VehicleDynamicsSpawnDesc& desc,
                                   VehicleDynamicsSpawnResult* out_wheels) {
    validate_instantiation(request);
    ENGINE_ASSERT(desc.mass_kg > 1.f, "chassis mass");
    ENGINE_ASSERT(desc.spring_k > 0.f, "spring k");
    ENGINE_ASSERT(out_wheels != nullptr, "wheel output");

    VehicleChassisComponent chassis{};
    chassis.mass_kg              = desc.mass_kg;
    chassis.inertia_tensor       = desc.inertia_tensor;
    chassis.center_of_mass_offset = desc.com_offset;
    chassis.velocity             = float3{0.f, 0.f, 0.f};
    chassis.angular_velocity     = float3{0.f, 0.f, 0.f};
    chassis.rotation_matrix      = float3x3_identity();

    VehicleChassisPose pose{};
    pose.position_ws = desc.position_ws;

    VehicleSteeringComponent steer{};
    steer.wheelbase_m          = desc.wheelbase_m;
    steer.track_width_m        = desc.track_width_m;
    steer.max_steer_rad        = 0.62f;
    steer.bicycle_steer_rad    = 0.f;
    steer.ackermann_inner_rad  = 0.f;
    steer.ackermann_outer_rad  = 0.f;

    VehicleExternalLoadComponent load{};
    load.force_ws  = float3{0.f, -desc.extra_load_g * desc.mass_kg * kGravityMs2, 0.f};
    load.torque_ws = float3{0.f, 0.f, 0.f};

    Entity body = world.instantiate(request, chassis, pose, steer, load);
    const u32 chassis_index = body.index();

    const float half_track = desc.track_width_m * 0.5f;
    const float half_wb    = desc.wheelbase_m * 0.5f;
    const float3 corners[4] = {
        float3{-half_track, 0.f,  half_wb}, // FL
        float3{ half_track, 0.f,  half_wb}, // FR
        float3{-half_track, 0.f, -half_wb}, // RL
        float3{ half_track, 0.f, -half_wb}, // RR
    };
    const u8 front[4]  = {1, 1, 0, 0};
    const u8 right[4]  = {0, 1, 0, 1};
    Entity* slots[4]   = {&out_wheels->wheel_fl, &out_wheels->wheel_fr, &out_wheels->wheel_rl,
                          &out_wheels->wheel_rr};

    InstantiationRequest wheel_req = request;
    wheel_req.debug_label = "wheel_node";

    for (u32 i = 0; i < 4; ++i) {
        WheelNodeComponent wheel{};
        wheel.chassis_entity_id         = chassis_index;
        wheel.local_attachment_point    = corners[i];
        wheel.suspension_rest_length_m  = desc.rest_length_m;
        wheel.suspension_travel_m       = desc.travel_m;
        wheel.spring_constant_n_m       = desc.spring_k;
        wheel.damping_ratio             = desc.damping_ratio;
        wheel.current_deflection_m      = 0.f;
        wheel.steer_angle_rad           = 0.f;
        wheel.angular_velocity_rad_s    = 0.f;
        wheel.slip_ratio                = 0.f;
        wheel.slip_angle_rad            = 0.f;

        VehicleTireContactComponent tire{};
        tire.radius_m           = desc.tire_radius_m;
        tire.friction_mu        = desc.friction_mu;
        tire.rolling_resistance = 0.015f;
        tire.contact_ws         = float3{0.f, 0.f, 0.f};
        tire.normal_force_n     = 0.f;
        tire.in_contact         = 0;
        tire.is_front           = front[i];
        tire.is_right           = right[i];
        tire._pad               = 0;

        *slots[i] = world.instantiate(wheel_req, wheel, tire);
    }
    out_wheels->chassis = body;
    return body;
}

void UpdateVehicleDynamicsSystem(World& world, float delta_time, FrameAllocator& frame_alloc,
                                 CommandBuffer& cmd) {
    ENGINE_ASSERT(delta_time > 0.f && delta_time < 0.05f, "vehicle dt");

    const u32 max_entities = world.entity_count();
    Entity* chassis_snap = frame_alloc.allocate_array<Entity>(max_entities);
    u32 chassis_count = 0;
    for (Entity e : world.query<VehicleChassisComponent, VehicleChassisPose>()) {
        if (chassis_count < max_entities) {
            chassis_snap[chassis_count++] = e;
        }
    }

    Entity* wheel_snap = frame_alloc.allocate_array<Entity>(max_entities);
    u32 wheel_count = 0;
    for (Entity e : world.query<WheelNodeComponent, VehicleTireContactComponent>()) {
        if (wheel_count < max_entities) {
            wheel_snap[wheel_count++] = e;
        }
    }

    const u32 airborne_id = world.registry().id_of<VehicleWheelAirborneTag>();
    const u64 frame_i     = world.frame_index();

    for (u32 c = 0; c < chassis_count; ++c) {
        Entity body = chassis_snap[c];
        VehicleChassisComponent* chassis = world.get<VehicleChassisComponent>(body);
        VehicleChassisPose*      pose    = world.get<VehicleChassisPose>(body);
        VehicleSteeringComponent* steer  = world.get<VehicleSteeringComponent>(body);
        VehicleExternalLoadComponent* load = world.get<VehicleExternalLoadComponent>(body);
        ENGINE_ASSERT(chassis != nullptr && pose != nullptr, "chassis snapshot stale");

        Entity wheel_ents[kMaxWheelsPerBody]{};
        u32    n_wheels = 0;
        for (u32 w = 0; w < wheel_count && n_wheels < kMaxWheelsPerBody; ++w) {
            WheelNodeComponent* node = world.get<WheelNodeComponent>(wheel_snap[w]);
            if (node && node->chassis_entity_id == body.index()) {
                wheel_ents[n_wheels++] = wheel_snap[w];
            }
        }

        float3 force  = {0.f, -chassis->mass_kg * kGravityMs2, 0.f};
        float3 torque = {0.f, 0.f, 0.f};
        if (load) {
            force  = float3_add(force, load->force_ws);
            torque = float3_add(torque, load->torque_ws);
        }

        // ICE drive: map throttle to a longitudinal force budget at the rear contact patches.
        float drive_force_budget = 0.f;
        if (const VehicleEngineComponent* ice = world.get<VehicleEngineComponent>(body)) {
            const float throttle = clampf(ice->throttle_input, 0.f, 1.f);
            const float wear     = clampf(ice->wear_factor, 0.f, 1.f);
            drive_force_budget   = throttle * 5200.f * (1.0f - 0.45f * wear);
            if (ice->temperature_c > 120.f || ice->current_rpm < 200.f) {
                drive_force_budget *= 0.35f;
            }
        }

        const float3 com_ws =
            float3_add(pose->position_ws, float3x3_mul(chassis->rotation_matrix, chassis->center_of_mass_offset));

        float rear_share = 0.f;
        for (u32 wi = 0; wi < n_wheels; ++wi) {
            const VehicleTireContactComponent* t =
                world.get<VehicleTireContactComponent>(wheel_ents[wi]);
            if (t && t->is_front == 0) {
                rear_share += 1.f;
            }
        }
        if (rear_share < 1.f) {
            rear_share = 1.f;
        }

        for (u32 wi = 0; wi < n_wheels; ++wi) {
            Entity we = wheel_ents[wi];
            WheelNodeComponent* wheel = world.get<WheelNodeComponent>(we);
            VehicleTireContactComponent* tire = world.get<VehicleTireContactComponent>(we);
            ENGINE_ASSERT(wheel != nullptr && tire != nullptr, "wheel snapshot stale");

            if (steer) {
                apply_ackermann(steer, wheel, tire);
            }

            const float3 attach_local = float3_add(wheel->local_attachment_point, chassis->center_of_mass_offset);
            const float3 attach_ws =
                float3_add(pose->position_ws, float3x3_mul(chassis->rotation_matrix, attach_local));

            const float radius = tire->radius_m;
            const float length = attach_ws.y - radius; // strut length along world up, flat road
            float x = wheel->suspension_rest_length_m - length;
            float airborne = 0.f;
            if (x <= 0.f) {
                x = 0.f;
                airborne = 1.f;
            } else if (x > wheel->suspension_travel_m) {
                x = wheel->suspension_travel_m;
            }

            const float v_comp = (x - wheel->current_deflection_m) / delta_time;
            wheel->current_deflection_m = x;

            // Corner mass for critical damping scale: m/n_wheels.
            const float m_corner = chassis->mass_kg / static_cast<float>(n_wheels > 0 ? n_wheels : 1);
            const float c = wheel->damping_ratio * 2.0f * std::sqrt(wheel->spring_constant_n_m * m_corner);

            // F = -(k * x) - (c * v) along the downward (positive-x) axis.
            float f_down = 0.f;
            if (airborne < 0.5f) {
                f_down = -(wheel->spring_constant_n_m * x) - (c * v_comp);
            }
            const float f_up = -f_down; // on chassis, world +Y
            const float3 f_strut = {0.f, f_up, 0.f};
            force = float3_add(force, f_strut);

            const float3 r = float3_sub(attach_ws, com_ws);
            torque = float3_add(torque, float3_cross(r, f_strut));

            tire->contact_ws     = float3{attach_ws.x, 0.f, attach_ws.z};
            tire->normal_force_n = (airborne < 0.5f) ? f_up : 0.f;
            tire->in_contact     = (airborne < 0.5f) ? 1 : 0;

            const bool tagged = world.has<VehicleWheelAirborneTag>(we);
            if (tire->in_contact == 0 && !tagged) {
                VehicleWheelAirborneTag tag{};
                tag.frame = static_cast<u32>(frame_i);
                cmd.add_component<VehicleWheelAirborneTag>(we, airborne_id, tag);
            } else if (tire->in_contact != 0 && tagged) {
                cmd.remove_component(we, airborne_id);
            }

            if (tire->in_contact == 0) {
                wheel->slip_ratio     = 0.f;
                wheel->slip_angle_rad = 0.f;
                continue;
            }

            const float steer_s = std::sin(wheel->steer_angle_rad);
            const float steer_c = std::cos(wheel->steer_angle_rad);
            const float3 chassis_fwd   = chassis->rotation_matrix.c2;
            const float3 chassis_right = chassis->rotation_matrix.c0;
            const float3 wheel_fwd = float3_normalize_or(
                float3_add(float3_scale(chassis_fwd, steer_c), float3_scale(chassis_right, steer_s)),
                float3{0.f, 0.f, 1.f});
            const float3 wheel_right = float3_normalize_or(float3_cross(float3{0.f, 1.f, 0.f}, wheel_fwd),
                                                           float3{1.f, 0.f, 0.f});

            const float3 v_point = float3_add(chassis->velocity, float3_cross(chassis->angular_velocity, r));
            const float v_long = float3_dot(v_point, wheel_fwd);
            const float v_lat  = float3_dot(v_point, wheel_right);

            const float w_r = wheel->angular_velocity_rad_s * radius;
            const float denom_k = max_of(std::fabs(v_long), max_of(std::fabs(w_r), 0.5f));
            wheel->slip_ratio     = clampf((w_r - v_long) / denom_k, -1.5f, 1.5f);
            wheel->slip_angle_rad = std::atan2(v_lat, max_of(std::fabs(v_long), 0.35f));

            const float mu_fz = tire->friction_mu * max_of(tire->normal_force_n, 0.f);
            float fx = pacejka(wheel->slip_ratio, mu_fz);
            float fy = -pacejka(wheel->slip_angle_rad, mu_fz);

            // Rolling resistance opposing longitudinal velocity.
            fx -= tire->rolling_resistance * tire->normal_force_n * (v_long >= 0.f ? 1.f : -1.f);

            if (tire->is_front == 0 && drive_force_budget != 0.f) {
                fx += drive_force_budget / rear_share;
            }

            // Friction circle clip.
            const float f_xy = std::sqrt(fx * fx + fy * fy);
            if (f_xy > mu_fz && f_xy > 1.0e-4f) {
                const float s = mu_fz / f_xy;
                fx *= s;
                fy *= s;
            }

            const float3 f_tire = float3_add(float3_scale(wheel_fwd, fx), float3_scale(wheel_right, fy));
            force  = float3_add(force, f_tire);
            torque = float3_add(torque, float3_cross(float3_sub(tire->contact_ws, com_ws), f_tire));

            // Wheel spin: τ = Fx * r, plus relaxation toward rolling.
            const float tau_spin = -fx * radius;
            wheel->angular_velocity_rad_s += (tau_spin / kWheelInertia) * delta_time;
            const float rolling_w = v_long / max_of(radius, 0.05f);
            wheel->angular_velocity_rad_s += (rolling_w - wheel->angular_velocity_rad_s) * clampf(delta_time * 4.0f, 0.f, 1.f);
        }

        const float inv_m = 1.0f / chassis->mass_kg;
        chassis->velocity = float3_add(chassis->velocity, float3_scale(force, inv_m * delta_time));
        pose->position_ws = float3_add(pose->position_ws, float3_scale(chassis->velocity, delta_time));

        const float3 tau_local = float3x3_mul_transpose(chassis->rotation_matrix, torque);
        float3 alpha_local{
            tau_local.x / max_of(chassis->inertia_tensor.x, 1.f),
            tau_local.y / max_of(chassis->inertia_tensor.y, 1.f),
            tau_local.z / max_of(chassis->inertia_tensor.z, 1.f)};
        const float3 alpha_ws = float3x3_mul(chassis->rotation_matrix, alpha_local);
        chassis->angular_velocity =
            float3_add(chassis->angular_velocity, float3_scale(alpha_ws, delta_time));
        chassis->rotation_matrix =
            integrate_rotation(chassis->rotation_matrix, chassis->angular_velocity, delta_time);
    }
}

} // namespace engine
