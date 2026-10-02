// Headless check for the moving city traffic (engine/render/CarTraffic.*).
//
// The GL city pass cannot run on a build machine without a GPU, so the circuit
// maths is verified here instead: geometry, lane discipline, continuity, and the
// fact that no two vehicles ever get close enough to overlap. No SDL2, no GL.
//
//   ./build/leonida_city_traffic_sandbox [--seconds 120]

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
    char buf[256];
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

} // namespace

int main(int argc, char** argv) {
    using namespace engine;
    float seconds = 3.f * kCarLapPeriodS; // three full laps: the timing repeats every lap
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = static_cast<float>(std::atof(argv[++i]));
        }
    }
    std::printf("=== Leonida city traffic sandbox (%.0f s simulated at 60 Hz) ===\n",
                static_cast<double>(seconds));

    CarTraffic traffic{};
    car_traffic_build(&traffic, kCityBlockPitch, 1u, 1u);

    expect(traffic.agent_count == kCarVehicleCount, "vehicle count matches the city fleet");
    expectf(traffic.mesh_count[0] == 20u && traffic.mesh_count[1] == 10u,
            "mesh split: %u Corolla E80 + %u sports", traffic.mesh_count[0], traffic.mesh_count[1]);
    expect(traffic.ring_count >= 4u, "circuits built");

    // ---- geometry of every circuit -------------------------------------------------
    float worst_offset = 0.f;
    float worst_step   = 0.f;
    float worst_yaw_step = 0.f;
    for (u32 r = 0; r < traffic.ring_count; ++r) {
        const CarRing& ring = traffic.rings[r];
        float sum = 0.f;
        bool  segs_ok = true;
        for (u32 i = 0; i < ring.nseg; ++i) {
            sum += ring.seg[i].len;
            segs_ok = segs_ok && ring.seg[i].len > 0.f;
        }
        const bool shape_ok = ring.nseg == kCarSegmentCap && segs_ok &&
                              std::fabs(sum - ring.perimeter) < 1e-3f && ring.x1 > ring.x0 &&
                              ring.z1 > ring.z0 && ring.x0 >= 0.f && ring.z0 >= 0.f &&
                              ring.x1 <= kCityExtentM && ring.z1 <= kCityExtentM;

        // Walk the whole circuit in 5 cm steps: no jumps, no gaps, always on asphalt.
        const float step = 0.05f;
        const u32   n    = static_cast<u32>(ring.perimeter / step);
        float px = 0.f, pz = 0.f, pyaw = 0.f;
        car_ring_pose(&ring, 0.f, &px, &pz, &pyaw);
        const float fx = px, fz = pz;
        bool  on_road = true;
        float ring_off = 0.f;
        for (u32 i = 1; i <= n; ++i) {
            float x = 0.f, z = 0.f, yaw = 0.f;
            car_ring_pose(&ring, static_cast<float>(i) * step, &x, &z, &yaw);
            const float d = dist_xz(px, pz, x, z);
            if (d > worst_step) {
                worst_step = d;
            }
            float dyaw = std::fabs(yaw - pyaw);
            if (dyaw > kCarPi) {
                dyaw = 2.f * kCarPi - dyaw;
            }
            if (dyaw > worst_yaw_step) {
                worst_yaw_step = dyaw;
            }
            const float off = dist_to_street_line(x, z);
            if (off > ring_off) {
                ring_off = off;
            }
            if (off + 1.05f > kCityStreetWidth * 0.5f) {
                on_road = false; // a 2.1 m wide car would hang over the kerb
            }
            px   = x;
            pz   = z;
            pyaw = yaw;
        }
        if (ring_off > worst_offset) {
            worst_offset = ring_off;
        }
        const float closure = dist_xz(px, pz, fx, fz);
        expectf(shape_ok && on_road && closure < 0.15f,
                "circuit %u: %.2f m loop, lane centre %.2f m off the street centre line, "
                "2.1 m car on asphalt, closes to %.3f m",
                r, static_cast<double>(ring.perimeter), static_cast<double>(ring_off),
                static_cast<double>(closure));
    }
    expectf(worst_step < 0.075f, "path is continuous: worst 5 cm step is %.4f m",
            static_cast<double>(worst_step));
    expectf(worst_yaw_step < 0.02f, "heading is continuous: worst 5 cm yaw step is %.4f rad",
            static_cast<double>(worst_yaw_step));

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
            bool ok = false;
            if (std::fabs(fx) > 0.5f) {
                const float line = std::round(g.sz / kCityBlockPitch) * kCityBlockPitch;
                ok = (fx > 0.f) ? (g.sz > line) : (g.sz < line);
            } else {
                const float line = std::round(g.sx / kCityBlockPitch) * kCityBlockPitch;
                ok = (fz > 0.f) ? (g.sx < line) : (g.sx > line);
            }
            if (!ok) {
                ++wrong_side;
            }
        }
    }
    expectf(wrong_side == 0u, "right-hand traffic: %u of %u straights keep the centre line on the driver's left",
            straights - wrong_side, straights);

    // Per street, no side of the centre line may carry traffic in both directions
    // (that would be a head-on lane). Rings that share a lane are fine: same offset,
    // same direction.
    struct SideUse {
        float line;
        int   runs_z;
        int   side;  // +1 / -1, the side of the centre line the lane sits on
        float dir;   // +1 / -1, the direction of travel
    };
    SideUse uses[64];
    u32     n_uses   = 0;
    u32     conflict = 0;
    for (u32 r = 0; r < traffic.ring_count; ++r) {
        const CarRing& ring = traffic.rings[r];
        for (u32 i = 0; i < ring.nseg; ++i) {
            const CarSegment& g = ring.seg[i];
            if (g.kappa != 0.f || n_uses >= 64u) {
                continue;
            }
            const float fx     = std::sin(g.yaw0);
            const float fz     = std::cos(g.yaw0);
            const int   runs_z = std::fabs(fx) < 0.5f ? 1 : 0;
            const float line   = std::round((runs_z ? g.sx : g.sz) / kCityBlockPitch) * kCityBlockPitch;
            const float off    = (runs_z ? g.sx : g.sz) - line;
            uses[n_uses++]     = SideUse{line, runs_z, off > 0.f ? 1 : -1, (runs_z ? fz : fx) > 0.f ? 1.f : -1.f};
        }
    }
    for (u32 a = 0; a < n_uses; ++a) {
        for (u32 b = a + 1u; b < n_uses; ++b) {
            if (uses[a].line == uses[b].line && uses[a].runs_z == uses[b].runs_z &&
                uses[a].side == uses[b].side && uses[a].dir != uses[b].dir) {
                ++conflict;
            }
        }
    }
    expectf(conflict == 0u,
            "no lane on either side of a centre line carries both directions (%u conflicts in %u lanes)",
            conflict, n_uses);

    // ---- simulate: spacing, minimum separation, flow past the start camera ----------
    const float dt      = 1.f / 60.f;
    const u32   frames  = static_cast<u32>(seconds / dt);
    float min_gap       = 1.0e9f;
    u32   min_a = 0, min_b = 0;
    float min_gap_t = 0.f;
    u32   pass_pos = 0, pass_neg = 0;
    const float gate_z = 150.f; // just up the avenue from the sandbox camera (1200, 130)
    float prev_z[kCarAgentCap];
    for (u32 i = 0; i < traffic.agent_count; ++i) {
        float x = 0.f, z = 0.f, yaw = 0.f;
        car_ring_pose(&traffic.rings[traffic.agents[i].ring], traffic.agents[i].s, &x, &z, &yaw);
        prev_z[i] = z;
    }

    for (u32 f = 0; f < frames; ++f) {
        car_traffic_step(&traffic, dt);
        float xs[kCarAgentCap];
        float zs[kCarAgentCap];
        for (u32 i = 0; i < traffic.agent_count; ++i) {
            const CarAgent& a = traffic.agents[i];
            float yaw = 0.f;
            car_ring_pose(&traffic.rings[a.ring], a.s, &xs[i], &zs[i], &yaw);
            if (std::fabs(xs[i] - 1200.f) <= 14.f) {
                if (prev_z[i] < gate_z && zs[i] >= gate_z) {
                    ++pass_pos;
                }
                if (prev_z[i] > gate_z && zs[i] <= gate_z) {
                    ++pass_neg;
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

    // Opposite lanes on a 20 m street are 2 * kCarLaneOffset = 8.4 m apart, so 8 m is
    // about as good as a crossing geometry can get without changing the lane width.
    expectf(min_gap >= 8.0f,
            "no two vehicles share asphalt: closest approach %.2f m (vehicles %u and %u at t=%.1f s)",
            static_cast<double>(min_gap), min_a, min_b, static_cast<double>(min_gap_t));

    // Same-circuit vehicles keep the spacing they spawned with (constant ring speed).
    bool spacing_ok = true;
    for (u32 r = 0; r < traffic.ring_count; ++r) {
        u32 n = 0;
        float ss[kCarAgentCap];
        for (u32 i = 0; i < traffic.agent_count; ++i) {
            if (traffic.agents[i].ring == r) {
                ss[n++] = traffic.agents[i].s;
            }
        }
        if (n < 2u) {
            continue;
        }
        const float perim  = traffic.rings[r].perimeter;
        const float expect = perim / static_cast<float>(n);
        for (u32 i = 1; i < n; ++i) {
            float d = ss[i] - ss[i - 1u];
            if (d < 0.f) {
                d += perim;
            }
            spacing_ok = spacing_ok && std::fabs(d - expect) < 1e-2f;
        }
    }
    expect(spacing_ok, "vehicles on a circuit keep their spawn spacing for the whole run");

    const float minutes = seconds / 60.f;
    std::printf("[ok] traffic flow past the start camera: %.1f vehicles/min away from the camera, "
                "%.1f toward the camera (gate z = %.0f m, |x - 1200| <= 14 m)\n",
                static_cast<double>(static_cast<float>(pass_pos) / minutes),
                static_cast<double>(static_cast<float>(pass_neg) / minutes),
                static_cast<double>(gate_z));

    std::printf("=== %d/%d checks passed ===\n", g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
