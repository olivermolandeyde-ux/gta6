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
