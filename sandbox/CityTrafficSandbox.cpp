// Headless check for the moving city traffic (engine/render/CarTraffic.*).
//
// The GL city pass cannot run on a build machine without a GPU, so the circuit maths,
// the lane discipline and - most importantly - the no-collision argument are verified
// here instead. No SDL2, no GL.
//
//   ./build/leonida_city_traffic_sandbox [--seconds 300]
//
// The traffic design has two independent safety arguments, and both are asserted:
//   inside a loop  - one speed for the whole loop, so the even spacing is invariant;
//   between loops  - the loops are pairwise disjoint, with tens of metres to spare.

#include "render/CarTraffic.h"
#include "render/CarWheelFit.h"
#include "world/CityGenerator.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace engine;

namespace {

int g_failed = 0;
int g_checks = 0;

void expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
    }
    std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what);
}

void expectf(bool ok, const char* fmt, ...) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
    }
    char    buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::printf("[%s] %s\n", ok ? "ok" : "FAIL", buf);
}

[[nodiscard]] float dist_to_street_line(float x, float z) {
    const float gx = std::round(x / kCityBlockPitch) * kCityBlockPitch;
    const float gz = std::round(z / kCityBlockPitch) * kCityBlockPitch;
    const float dx = std::fabs(x - gx);
    const float dz = std::fabs(z - gz);
    return dx < dz ? dx : dz;
}

[[nodiscard]] float dist_xz(float ax, float az, float bx, float bz) {
    const float dx = ax - bx;
    const float dz = az - bz;
    return std::sqrt(dx * dx + dz * dz);
}

// Walk a whole loop in `step` metre steps. Always covers the full perimeter (a loop here
// is 743 m, so nothing may cut the walk short); samples are stored only when `out` is
// given, which is what the disjointness check needs.
struct LoopSamples {
    float x[512];
    float z[512];
    u32   n;
    float off_max;    // furthest the lane centre strays from a street centre line
    bool  on_asphalt; // a 2.1 m wide car stays inside the 20 m carriageway
    float closure;    // distance between the first and the last point of the walk
};

void walk_loop(const CarRing& ring, float step, LoopSamples* out) {
    if (out) {
        out->n = 0;
    }
    float off_max    = 0.f;
    bool  on_asphalt = true;
    const u32 n      = static_cast<u32>(ring.perimeter / step);
    float     px = 0.f, pz = 0.f;
    car_ring_pose(&ring, 0.f, &px, &pz, nullptr);
    const float fx = px, fz = pz;
    for (u32 i = 1; i <= n; ++i) {
        float x = 0.f, z = 0.f;
        car_ring_pose(&ring, static_cast<float>(i) * step, &x, &z, nullptr);
        const float off = dist_to_street_line(x, z);
        if (off > off_max) {
            off_max = off;
        }
        if (off + 1.05f > kCityStreetWidth * 0.5f) {
            on_asphalt = false; // a 2.1 m wide car would hang over the kerb
        }
        if (out && out->n < 512u) {
            out->x[out->n] = x;
            out->z[out->n] = z;
            ++out->n;
        }
        px = x;
        pz = z;
    }
    if (out) {
        out->off_max    = off_max;
        out->on_asphalt = on_asphalt;
        out->closure    = dist_xz(px, pz, fx, fz);
    }
}

} // namespace

int main(int argc, char** argv) {
    float seconds = 300.f; // > 3 laps of the slowest loop (743 m at 8.2 m/s ≈ 91 s)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = static_cast<float>(std::atof(argv[++i]));
        }
    }
    std::printf("=== Leonida city traffic sandbox (%.0f s simulated at 60 Hz) ===\n",
                static_cast<double>(seconds));

    CarTraffic traffic{};
    car_traffic_build(&traffic, kCityBlockPitch, 1u, 1u);

    const u32 corolla_cars = (kCarLoopCount - kCarSportsLoops) * kCarCarsPerLoop;
    const u32 sports_cars  = kCarSportsLoops * kCarCarsPerLoop;
    expect(traffic.agent_count == kCarVehicleCount, "vehicle count matches the citywide plan");
    expectf(traffic.mesh_count[0] == corolla_cars && traffic.mesh_count[1] == sports_cars,
            "mesh split: %u Corolla E80 + %u sports across %u loops", traffic.mesh_count[0],
            traffic.mesh_count[1], traffic.ring_count);
    expect(traffic.ring_count == kCarLoopCount, "every planned loop was built");

    // ---- geometry and lattice law of every loop -------------------------------------
    float worst_yaw_step = 0.f;
    u32   lattice_bad    = 0;
    for (u32 r = 0; r < traffic.ring_count; ++r) {
        const CarRing& ring = traffic.rings[r];
        float          sum  = 0.f;
        bool           segs_ok = true;
        for (u32 i = 0; i < ring.nseg; ++i) {
            sum += ring.seg[i].len;
            segs_ok = segs_ok && ring.seg[i].len > 0.f;
        }
        const bool shape_ok = ring.nseg == kCarSegmentCap && segs_ok &&
                              std::fabs(sum - ring.perimeter) < 1e-3f && ring.x1 > ring.x0 &&
                              ring.z1 > ring.z0 && ring.x0 >= 0.f && ring.z0 >= 0.f &&
                              ring.x1 <= kCityExtentM && ring.z1 <= kCityExtentM;
        // The lattice law: i0 even, j0 a multiple of 4, one block wide, two blocks long.
        const float i0f = std::round(ring.x0 / kCityBlockPitch);
        const float j0f = std::round(ring.z0 / kCityBlockPitch);
        const bool  lattice_ok = std::fabs(ring.x0 - i0f * kCityBlockPitch) < 0.01f &&
                                std::fabs(ring.z0 - j0f * kCityBlockPitch) < 0.01f &&
                                std::fabs(ring.x1 - ring.x0 - kCityBlockPitch) < 0.01f &&
                                std::fabs(ring.z1 - ring.z0 - 2.f * kCityBlockPitch) < 0.01f &&
                                (static_cast<int>(i0f) % 2) == 0 && (static_cast<int>(j0f) % 4) == 0;
        if (!lattice_ok) {
            ++lattice_bad;
        }

        LoopSamples s{};
        walk_loop(ring, 0.05f, &s);
        const CarRing& ring_ref = ring;
        (void)ring_ref;
        expectf(shape_ok && lattice_ok && s.on_asphalt && s.closure < 0.15f,
                "loop %2u: %.0f m, lane %.2f m off the centre line, 2.1 m car on asphalt, "
                "closes to %.3f m, lattice %s",
                r, static_cast<double>(ring.perimeter), static_cast<double>(s.off_max),
                static_cast<double>(s.closure), lattice_ok ? "ok" : "BROKEN");

        // Heading continuity along a fine walk.
        const float step = 0.05f;
        const u32   n    = static_cast<u32>(ring.perimeter / step);
        float       pyaw = 0.f;
        car_ring_pose(&ring, 0.f, nullptr, nullptr, &pyaw);
        for (u32 i = 1; i <= n; ++i) {
            float yaw = 0.f;
            car_ring_pose(&ring, static_cast<float>(i) * step, nullptr, nullptr, &yaw);
            float dyaw = std::fabs(yaw - pyaw);
            if (dyaw > kCarPi) {
                dyaw = 2.f * kCarPi - dyaw;
            }
            if (dyaw > worst_yaw_step) {
                worst_yaw_step = dyaw;
            }
            pyaw = yaw;
        }
    }
    expect(lattice_bad == 0u, "every loop follows the disjoint lattice law (i even, j % 4 == 0)");
    expectf(worst_yaw_step < 0.02f, "heading is continuous: worst 5 cm yaw step is %.4f rad",
            static_cast<double>(worst_yaw_step));

    // ---- safety argument 1: the loops are pairwise disjoint -------------------------
    static LoopSamples samples[kCarLoopCap];
    for (u32 r = 0; r < traffic.ring_count; ++r) {
        walk_loop(traffic.rings[r], 4.f, &samples[r]);
    }
    float loop_gap     = 1.0e9f;
    u32   loop_gap_a   = 0;
    u32   loop_gap_b   = 0;
    for (u32 a = 0; a < traffic.ring_count; ++a) {
        for (u32 b = a + 1u; b < traffic.ring_count; ++b) {
            for (u32 i = 0; i < samples[a].n; ++i) {
                for (u32 j = 0; j < samples[b].n; ++j) {
                    const float d = dist_xz(samples[a].x[i], samples[a].z[i], samples[b].x[j],
                                            samples[b].z[j]);
                    if (d < loop_gap) {
                        loop_gap   = d;
                        loop_gap_a = a;
                        loop_gap_b = b;
                    }
                }
            }
        }
    }
    expectf(loop_gap > 40.f,
            "loops share no asphalt: closest two loops are %.1f m apart (loops %u and %u)",
            static_cast<double>(loop_gap), loop_gap_a, loop_gap_b);

    // ---- safety argument 2: one speed per loop, so the spacing never closes up ------
    u32   class_bad   = 0;
    float slow_max    = 0.f;
    float fast_min    = 1.0e9f;
    float spacing_min = 1.0e9f;
    for (u32 r = 0; r < traffic.ring_count; ++r) {
        const CarRing& ring = traffic.rings[r];
        const float    lo   = ring.sports != 0 ? kCarSportsSpeedMin : kCarCorollaSpeedMin;
        const float    hi   = ring.sports != 0 ? kCarSportsSpeedMax : kCarCorollaSpeedMax;
        if (ring.speed < lo || ring.speed > hi) {
            ++class_bad;
        }
        if (ring.sports != 0) {
            fast_min = fast_min < ring.speed ? fast_min : ring.speed;
        } else {
            slow_max = slow_max > ring.speed ? slow_max : ring.speed;
        }
        const float spacing = ring.perimeter / static_cast<float>(kCarCarsPerLoop);
        spacing_min         = spacing_min < spacing ? spacing_min : spacing;
    }
    expect(class_bad == 0u, "every loop speed is inside its class range (8-10 / 12-15 m/s)");
    expectf(fast_min > slow_max, "sports loops are faster than every Corolla loop (%.1f > %.1f m/s)",
            static_cast<double>(fast_min), static_cast<double>(slow_max));
    expectf(spacing_min > 60.f,
            "safe following distance at spawn: %.1f m between vehicles on a loop, invariant because "
            "each loop runs at one speed",
            static_cast<double>(spacing_min));

    // ---- simulate: nothing ever closes up ------------------------------------------
    const float dt     = 1.f / 60.f;
    const u32   frames = static_cast<u32>(seconds / dt);
    float       min_gap    = 1.0e9f;
    u32         min_a = 0, min_b = 0;
    float       min_gap_t  = 0.f;
    const CarRing& cam_loop = traffic.rings[0];
    const float gate_z      = 150.f; // just up the avenue from the sandbox camera
    const float north_lane  = cam_loop.x0 - cam_loop.lane;
    const float south_lane  = cam_loop.x1 + cam_loop.lane;
    u32         pass_north  = 0;
    u32         pass_south  = 0;
    float       prev_z[kCarAgentCap];

    {
        float xs[kCarAgentCap], zs[kCarAgentCap];
        for (u32 i = 0; i < traffic.agent_count; ++i) {
            float yaw = 0.f;
            car_agent_pose(&traffic, i, &xs[i], &zs[i], &yaw);
            prev_z[i] = zs[i];
        }
        for (u32 f = 0; f < frames; ++f) {
            car_traffic_step(&traffic, dt);
            for (u32 i = 0; i < traffic.agent_count; ++i) {
                const CarAgent& a = traffic.agents[i];
                float           yaw = 0.f;
                car_agent_pose(&traffic, i, &xs[i], &zs[i], &yaw);
                if (a.ring == 0u) {
                    if (std::fabs(xs[i] - north_lane) < 8.f && prev_z[i] < gate_z &&
                        zs[i] >= gate_z) {
                        ++pass_north;
                    }
                    if (std::fabs(xs[i] - south_lane) < 8.f && prev_z[i] > gate_z &&
                        zs[i] <= gate_z) {
                        ++pass_south;
                    }
                }
                prev_z[i] = zs[i];
            }
            for (u32 i = 0; i < traffic.agent_count; ++i) {
                for (u32 j = i + 1u; j < traffic.agent_count; ++j) {
                    const float d = dist_xz(xs[i], zs[i], xs[j], zs[j]);
                    if (d < min_gap) {
                        min_gap   = d;
                        min_a     = i;
                        min_b     = j;
                        min_gap_t = static_cast<float>(f) * dt;
                    }
                }
            }
        }
    }
    expectf(min_gap > 60.f,
            "no two vehicles ever share asphalt: closest straight-line approach %.1f m (vehicles "
            "%u and %u at t=%.0f s); %.1f m of arc spacing, and the shortest chord across a "
            "corner is 67 m",
            static_cast<double>(min_gap), min_a, min_b, static_cast<double>(min_gap_t),
            static_cast<double>(spacing_min));

    // Same-loop vehicles keep the spacing they spawned with (one speed per loop).
    bool spacing_ok = true;
    for (u32 r = 0; r < traffic.ring_count; ++r) {
        u32   n    = 0;
        float ss[kCarAgentCap];
        for (u32 i = 0; i < traffic.agent_count; ++i) {
            if (traffic.agents[i].ring == r) {
                ss[n++] = car_agent_s(&traffic, i);
            }
        }
        if (n < 2u) {
            continue;
        }
        const float perim  = traffic.rings[r].perimeter;
        const float expect_spacing = perim / static_cast<float>(n);
        for (u32 i = 1; i < n; ++i) {
            float d = ss[i] - ss[i - 1u];
            if (d < 0.f) {
                d += perim;
            }
            spacing_ok = spacing_ok && std::fabs(d - expect_spacing) < 1e-3f;
        }
    }
    expect(spacing_ok, "vehicles on a loop keep their spawn spacing for the whole run");

    // ---- lane discipline: right-hand traffic on every straight ----------------------
    // Law: in right-hand traffic the street centre line is on the driver's left.
    // left = up x forward, with forward = (sin yaw, 0, cos yaw):
    //   travel +Z -> left = +X -> the lane must sit at x < centre line
    //   travel -Z -> left = -X -> the lane must sit at x > centre line
    //   travel +X -> left = -Z -> the lane must sit at z > centre line
    //   travel -X -> left = +Z -> the lane must sit at z < centre line
    u32 straights  = 0;
    u32 wrong_side = 0;
    for (u32 r = 0; r < traffic.ring_count; ++r) {
        const CarRing& ring = traffic.rings[r];
        for (u32 i = 0; i < ring.nseg; ++i) {
            const CarSegment& g = ring.seg[i];
            if (g.kappa != 0.f) {
                continue;
            }
            ++straights;
            const float fx = std::sin(g.yaw0);
            const float fz = std::cos(g.yaw0);
            bool        ok = false;
            if (std::fabs(fx) > 0.5f) {
                const float line = std::round(g.sz / kCityBlockPitch) * kCityBlockPitch;
                ok               = (fx > 0.f) ? (g.sz > line) : (g.sz < line);
            } else {
                const float line = std::round(g.sx / kCityBlockPitch) * kCityBlockPitch;
                ok               = (fz > 0.f) ? (g.sx < line) : (g.sx > line);
            }
            if (!ok) {
                ++wrong_side;
            }
        }
    }
    expectf(wrong_side == 0u,
            "right-hand traffic: %u of %u straights keep the centre line on the driver's left",
            straights - wrong_side, straights);

    // ---- car wheel maths: the part that cannot be eyeballed on screen ----------------
    {
        // A synthetic Y-up car: X = length, Y = height, Z = width (so Z is the axle axis).
        const float r      = 0.35f; // tyre radius
        const float halfw  = 0.10f; // half tyre width
        const float track  = 1.5f;
        const float base   = 2.6f;
        const float truth[4][3] = {{-base * 0.5f, r, -track * 0.5f},
                                   {-base * 0.5f, r, track * 0.5f},
                                   {base * 0.5f, r, -track * 0.5f},
                                   {base * 0.5f, r, track * 0.5f}};
        float pts[4 * 64 * 3];
        u32   n = 0;
        for (int w = 0; w < 4; ++w) {
            for (int side = 0; side < 2; ++side) {
                const float zw = truth[w][2] + (side ? halfw : -halfw);
                for (int k = 0; k < 16; ++k) {
                    const float a  = static_cast<float>(k) * 6.2831853f / 16.f;
                    pts[n * 3 + 0] = truth[w][0] + r * std::cos(a);
                    pts[n * 3 + 1] = truth[w][1] + r * std::sin(a);
                    pts[n * 3 + 2] = zw;
                    ++n;
                }
            }
        }
        u32         scratch[4 * 64];
        CarWheelSet set{};
        const bool  ok4 = car_wheel_fit(pts, 3u, n, scratch, &set);
        expect(ok4 && set.count == 4, "wheel fit finds all four wheels in one merged primitive");
        expect(set.axle_axis == 2, "wheel fit calls the thin axis the axle");
        float worst_center = 0.f;
        float worst_radius = 0.f;
        if (ok4 && set.count == 4) {
            for (int w = 0; w < 4; ++w) {
                float best = 1.0e9f;
                for (int c = 0; c < 4; ++c) {
                    const float dx = set.center[c][0] - truth[w][0];
                    const float dy = std::fabs(set.center[c][1] - truth[w][1]);
                    const float dz = set.center[c][2] - truth[w][2];
                    const float dd = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (dd < best) {
                        best = dd;
                    }
                }
                if (best > worst_center) {
                    worst_center = best;
                }
                for (int c = 0; c < 4; ++c) {
                    const float dr = std::fabs(set.radius[c] - r);
                    if (dr > worst_radius) {
                        worst_radius = dr;
                    }
                }
            }
        }
        expectf(ok4 && worst_center < 0.02f && worst_radius < 0.02f,
                "wheel centres land within %.3f m and radii within %.3f m of the modelled wheels",
                static_cast<double>(worst_center), static_cast<double>(worst_radius));

        // Roll direction: while the car drives forward, the tyre's contact patch has to
        // move backwards along the car's nose — that is rolling without slipping. This
        // has to hold for both bodies and their mirrored yaw offsets, through the whole
        // AABB-basis-then-yaw chain the renderer actually uses.
        const float e[3] = {4.6f, 1.5f, 2.0f}; // X length, Y height, Z width of a car mesh
        int         fwd  = 0;
        if (e[1] > e[fwd]) {
            fwd = 1;
        }
        if (e[2] > e[fwd]) {
            fwd = 2;
        }
        int up = (fwd == 0) ? 1 : 0;
        for (int i = 0; i < 3; ++i) {
            if (i != fwd && e[i] < e[up]) {
                up = i;
            }
        }
        float fu[3] = {0.f, 0.f, 0.f};
        float uu[3] = {0.f, 0.f, 0.f};
        float ru[3];
        fu[fwd] = 1.f;
        uu[up]  = 1.f;
        ru[0]   = fu[1] * uu[2] - fu[2] * uu[1];
        ru[1]   = fu[2] * uu[0] - fu[0] * uu[2];
        ru[2]   = fu[0] * uu[1] - fu[1] * uu[0];
        const float R[9] = {ru[0], uu[0], fu[0], ru[1], uu[1], fu[1], ru[2], uu[2], fu[2]};
        const int   axle = 2; // thin axis, as fitted above

        // model -> ring frame (rows of R), then the heading yaw, exactly as the pass does.
        auto to_world = [&](const float p[3], float yaw_off, float out[3]) {
            const float bx = R[0] * p[0] + R[3] * p[1] + R[6] * p[2];
            const float by = R[1] * p[0] + R[4] * p[1] + R[7] * p[2];
            const float bz = R[2] * p[0] + R[5] * p[1] + R[8] * p[2];
            const float c = std::cos(yaw_off), sn = std::sin(yaw_off);
            out[0] = c * bx + sn * bz;
            out[1] = by;
            out[2] = -sn * bx + c * bz;
        };

        const float body_offsets[2]    = {kCarPi, 2.f * kCarPi}; // Corolla, sports
        const float dpsi               = 0.02f;                  // radians of wheel spin
        bool        roll_ok            = true;
        float       worst_forward      = -1.0e9f;
        float       worst_slip         = 0.f;
        for (int b = 0; b < 2; ++b) {
            const float dir = car_wheel_roll_dir(fwd, up, axle);
            // A material point on the tread at the bottom of the tyre: the contact patch.
            // The shader spins about the wheel centre, so rotate the offset, not the point.
            const float ctr[3] = {1.3f, r, -0.75f};  // front-left wheel centre, model space
            const float off[3] = {0.f, -r, 0.f};     // centre -> contact patch
            const float a  = dpsi * dir;
            const float ca = std::cos(a), sa = std::sin(a);
            const float p0[3] = {ctr[0] + off[0], ctr[1] + off[1], ctr[2] + off[2]};
            const float p1[3] = {ctr[0] + off[0] * ca - off[1] * sa,
                                 ctr[1] + off[0] * sa + off[1] * ca, ctr[2] + off[2]};
            float w0[3], w1[3], nose[3];
            to_world(p0, body_offsets[b], w0);
            to_world(p1, body_offsets[b], w1);
            to_world(fu, body_offsets[b], nose); // the car's nose, in the world
            const float dx = w1[0] - w0[0], dy = w1[1] - w0[1], dz = w1[2] - w0[2];
            const float forward_m = dx * nose[0] + dy * nose[1] + dz * nose[2];
            worst_forward = forward_m > worst_forward ? forward_m : worst_forward;
            roll_ok       = roll_ok && forward_m < -1.0e-6f;
            // Rolling without slipping: the patch travels the arc length r * dpsi.
            const float travel = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float slip   = std::fabs(travel - r * dpsi) / (r * dpsi);
            if (slip > worst_slip) {
                worst_slip = slip;
            }
        }
        expectf(roll_ok, "wheel roll direction: the contact patch moves backwards along the nose "
                        "through the whole basis+yaw chain (worst forward drift %.5f m)",
                static_cast<double>(worst_forward));
        expectf(worst_slip < 0.01f,
                "wheels roll without slipping: the patch travels the arc length r*angle (worst "
                "error %.4f%%)",
                static_cast<double>(worst_slip * 100.f));

        // The shape-only fallback (used when a car's names never say "wheel") accepts a
        // fitted disc only if it stands on the model floor: a wheel does, a headlight, a
        // mirror and a spare tyre in the boot do not, so they can never be spun.
        const float car_h = 1.50f; // model height, metres
        expect(car_wheel_on_ground(0.35f, 0.35f, 0.f, car_h), "a tyre standing on the road passes "
                                                              "the floor test");
        expect(!car_wheel_on_ground(0.65f, 0.22f, 0.f, car_h),
               "a headlight 0.43 m above the road is refused");
        expect(!car_wheel_on_ground(0.75f, 0.35f, 0.f, car_h),
               "a spare tyre resting in the boot is refused");
        expect(car_wheel_on_ground(0.36f, 0.35f, 0.01f, car_h),
               "a wheel one centimetre off the road is still accepted (meshes are never exact)");

        // The real asset names from the Corolla GLB. The tire rotated while the brake parts
        // did not, because "brake" was not in the keyword list at all and the geometry test
        // refuses a disc that has a caliper bolted to it.
        const char* const kBrakeNodes[4] = {"wheelbrake.Ft.L_metal_rough_plus_0",
                                            "wheelbrake.Ft.R_metal_rough_plus_0",
                                            "wheelbrake.Bk.L_metal_rough_plus_0",
                                            "wheelbrake.Bk.R_metal_rough_plus_0"};
        bool brakes_named = true;
        bool brakes_hardware = true;
        for (const char* n : kBrakeNodes) {
            brakes_named = brakes_named && car_wheel_name_looks_like_wheel(n);
            brakes_hardware = brakes_hardware && car_wheel_name_is_hardware(n);
        }
        expect(brakes_named, "the asset's brake nodes are recognised as wheel parts");
        expect(brakes_hardware,
               "the asset's brake nodes count as wheel hardware, so a caliper may make the "
               "part wider than the disc");
        expect(car_wheel_name_looks_like_wheel("wheel.Bk.R_tire_0"),
               "the tire node that already rotated is still recognised");
        const char* const kBodyParts[6] = {"body_0", "door.Ft.L_0", "glass_windscreen_0",
                                            "bumper.R_0", "spoiler_0", "headlight.L_0"};
        for (const char* n : kBodyParts) {
            expectf(!car_wheel_name_looks_like_wheel(n), "'%s' is not a wheel part", n);
        }
        const char* const kNotHardware[2] = {"wheel_0", "tire.Ft.L_0"};
        for (const char* n : kNotHardware) {
            expectf(car_wheel_name_looks_like_wheel(n) && !car_wheel_name_is_hardware(n),
                    "'%s' is a wheel part but not hardware, so it gets the strict limits", n);
        }

        // A brake disc with its caliper, a rim with bolt heads and a hub cap are all part of
        // the wheel, but the disc test refuses them (that is what keeps bumpers still). The
        // attach rule matches them to the wheel they are concentric with, so they turn too.
        {
            float       centers[4 * 3] = {1.3f, 0.f, -0.75f,   // front left
                                          1.3f, 0.f, 0.75f,    // front right
                                          -1.3f, 0.f, -0.75f,  // rear left
                                          -1.3f, 0.f, 0.75f};  // rear right
            const float radii[4]       = {0.32f, 0.32f, 0.32f, 0.32f};

            // A brake disc + caliper: compact, concentric, but the caliper sticks out, which
            // is exactly what car_wheel_fit refuses. Its centre may sit a little off.
            const float brake_c[3] = {1.30f, 0.02f, -0.72f};
            const float brake_e[3] = {0.62f, 0.62f, 0.24f};
            expect(car_wheel_attach_to(brake_c, brake_e, centers, radii, 4, kAttachOffsetHardware,
                                       kAttachExtentHardware) == 0,
                   "a brake disc with a caliper attaches to the wheel it is concentric with");

            // A rim with bolt heads, same story.
            const float rim_c[3] = {-1.28f, 0.01f, 0.76f};
            const float rim_e[3] = {0.60f, 0.60f, 0.22f};
            expect(car_wheel_attach_to(rim_c, rim_e, centers, radii, 4, kAttachOffsetHardware,
                                       kAttachExtentHardware) == 3,
                   "a rim with bolt heads attaches to its own wheel, not a neighbour");

            // A hub cap: small and exactly concentric.
            const float cap_c[3] = {1.30f, 0.00f, 0.75f};
            const float cap_e[3] = {0.30f, 0.30f, 0.10f};
            expect(car_wheel_attach_to(cap_c, cap_e, centers, radii, 4, kAttachOffsetHardware,
                                       kAttachExtentHardware) == 1,
                   "a hub cap attaches to its own wheel");

            // But a fender, a sill or a whole underside is metres wide: never attached,
            // however close its centre happens to be.
            const float fender_c[3] = {1.30f, 0.30f, -0.75f};
            const float fender_e[3] = {1.60f, 0.70f, 1.90f};
            expect(car_wheel_attach_to(fender_c, fender_e, centers, radii, 4) == -1,
                   "a fender is refused even though it sits over a wheel");
            const float floor_c[3] = {0.00f, -0.20f, 0.00f};
            const float floor_e[3] = {4.60f, 0.20f, 1.80f};
            expect(car_wheel_attach_to(floor_c, floor_e, centers, radii, 4) == -1,
                   "a whole underside is refused");

            // A wheel arch / arch trim is named "wheel..." and may sit close to the wheel, but
            // it is bodywork: it must never be attached, which is why a part named just
            // "wheel" has to be dead concentric and no larger than a disc.
            const float arch_c[3] = {1.30f, 0.34f, -0.75f};
            const float arch_e[3] = {0.80f, 0.60f, 0.30f};
            expect(car_wheel_attach_to(arch_c, arch_e, centers, radii, 4) == -1,
                   "a wheel arch trim over the wheel is refused by the generic limits");
            const char* const kBodyworkNames[5] = {"wheel_trim.Bk.L_0", "wheelarch.Ft.L_0",
                                                "wheel_well.R_0", "hjulbue.Ft.L_0",
                                                "wheel_arch_trim.Bk.R_0"};
        for (const char* n : kBodyworkNames) {
            expectf(!car_wheel_name_looks_like_wheel(n),
                    "'%s' is bodywork: spelled with \"wheel\", but not a wheel part", n);
        }

            // Something small, but nowhere near a wheel: a headlight, a mirror, a badge.
            const float lamp_c[3] = {2.20f, 0.65f, -1.00f};
            const float lamp_e[3] = {0.30f, 0.20f, 0.30f};
            expect(car_wheel_attach_to(lamp_c, lamp_e, centers, radii, 4) == -1,
                   "a compact part that is not concentric with a wheel stays static");

            // No wheel found at all: nothing can be attached, and nothing crashes.
            expect(car_wheel_attach_to(brake_c, brake_e, centers, radii, 0u) == -1,
                   "with no fitted wheel there is nothing to attach to");
        }

        // Both axle conventions must be handled: a model authored with its length on Z and
        // width on X is the mirror image, and its wheel sign has to mirror with it.
        expect(car_wheel_roll_dir(2, 1, 0) == car_wheel_roll_dir(fwd, up, axle) * -1.f,
               "the spin sign follows the model's axis order, so a mirrored mesh rolls forward too");
    }

    const float minutes = seconds / 60.f;
    std::printf("[ok] flow past the camera loop (z = %.0f m): %.1f vehicles/min northbound beside "
                "the lens, %.1f/min southbound on the far side of the block\n",
                static_cast<double>(gate_z),
                static_cast<double>(static_cast<float>(pass_north) / minutes),
                static_cast<double>(static_cast<float>(pass_south) / minutes));
    expect(pass_north > 0u && pass_south > 0u, "the camera loop carries both directions of travel");

    std::printf("=== %d/%d checks passed ===\n", g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
