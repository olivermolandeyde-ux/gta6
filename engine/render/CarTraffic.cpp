#include "render/CarTraffic.h"

#include <cmath>
#include <cstring>

namespace engine {

namespace {

inline float wrap_len(float s, float perim) {
    s = std::fmod(s, perim);
    return s < 0.f ? s + perim : s;
}

inline float wrap_yaw(float y) {
    while (y > kCarPi) {
        y -= 2.f * kCarPi;
    }
    while (y <= -kCarPi) {
        y += 2.f * kCarPi;
    }
    return y;
}

void put_seg(CarRing* r, u32 i, float sx, float sz, float cx, float cz, float yaw0, float len,
             float kappa) {
    CarSegment& g = r->seg[i];
    g.sx    = sx;
    g.sz    = sz;
    g.cx    = cx;
    g.cz    = cz;
    g.yaw0  = yaw0;
    g.len   = len;
    g.kappa = kappa;
}

// The citywide plan: 16 loops, all mutually disjoint, spread over the 2.4 km grid.
// i0 must be even and j0 a multiple of 4 - that is what keeps neighbouring loops from
// sharing an intersection (two loops whose rectangles touch would have corner arcs that
// cross, and then their timings would matter). Speeds differ per loop, which is safe
// precisely because the loops never share asphalt.
//
//   Corolla loops: 8.2 - 9.8 m/s (30 - 35 km/h), normal traffic
//   sports loops: 12.6 - 14.2 m/s (45 - 51 km/h), the fast loops
//
// Loop 0 runs the streets around the block the city sandbox camera starts on
// (x = 1200..1320 m, z = 0..240 m), so there is traffic right beside the lens.
struct CarLoopPlan {
    u32   i0, j0; // street-grid line indices; metres = index * block pitch
    float speed;  // m/s, constant for every vehicle on this loop
    int   sports; // 1 = sports model on this loop
};

constexpr u32 kPlanCount = kCarLoopCount;

constexpr CarLoopPlan kPlan[kPlanCount] = {
    // Corolla loops — normal speeds, spread corner to corner.
    {10u, 0u, 9.0f, 0},   // beside the sandbox camera
    {10u, 4u, 8.4f, 0},
    {6u, 0u, 9.6f, 0},
    {14u, 0u, 8.8f, 0},
    {2u, 4u, 9.2f, 0},
    {18u, 4u, 8.6f, 0},
    {0u, 12u, 9.4f, 0},
    {4u, 0u, 9.0f, 0},
    {8u, 8u, 8.2f, 0},
    {12u, 12u, 9.8f, 0},
    {16u, 0u, 8.4f, 0},
    {18u, 16u, 9.2f, 0},
    // Sports loops — faster, on their own paths so they never catch a Corolla.
    {8u, 0u, 13.4f, 1},
    {12u, 0u, 12.6f, 1},
    {10u, 12u, 14.2f, 1},
    {16u, 12u, 13.0f, 1},
};

constexpr u32 count_sports_loops() {
    u32 n = 0;
    for (u32 i = 0; i < kPlanCount; ++i) {
        if (kPlan[i].sports != 0) {
            ++n;
        }
    }
    return n;
}
static_assert(count_sports_loops() == kCarSportsLoops, "sports loop count must match CarTraffic.h");
static_assert(sizeof(kPlan) / sizeof(kPlan[0]) == kCarLoopCount, "plan must list every loop");

} // namespace

void car_ring_init(CarRing* r, float x0, float z0, float x1, float z1, float lane, float radius) {
    if (!r) {
        return;
    }
    std::memset(r, 0, sizeof(*r));
    r->x0     = x0;
    r->z0     = z0;
    r->x1     = x1;
    r->z1     = z1;
    r->lane   = lane;
    r->radius = radius;
    if (radius <= 0.f || x1 <= x0 || z1 <= z0) {
        return;
    }

    const float L = lane;
    const float R = radius;
    // Tangent points sit 2*(R - L) closer to the intersection centre than the
    // centre-line distance, hence the +2L-2R on both straights.
    const float dx     = (x1 - x0) + 2.f * (L - R);
    const float dz     = (z1 - z0) + 2.f * (L - R);
    const float quarter = 0.5f * kCarPi * R;
    const float kappa   = 1.f / R; // left turns: the heading increases
    if (dx <= 0.f || dz <= 0.f) {
        return;
    }
    const float hy = 0.5f * kCarPi; // 90°

    // Right-hand traffic, verified against the heading convention: travelling
    // +Z (yaw 0) the driver's left is +X, so the lane at x = centre - L keeps the
    // street centre line on the driver's left, as does every other straight below.
    // Left turns then fall out of the loop, and the arcs sweep through the junction.
    // Order: -Z lane straight, +X lane straight, +Z lane straight, -X lane straight,
    // each pair joined by a left turn.
    put_seg(r, 0u, x0 - L, z0 - L + R, 0.f, 0.f, 0.f, dz, 0.f);
    put_seg(r, 1u, x0 - L, z1 + L - R, x0 - L + R, z1 + L - R, 0.f, quarter, kappa);
    put_seg(r, 2u, x0 - L + R, z1 + L, 0.f, 0.f, hy, dx, 0.f);
    put_seg(r, 3u, x1 + L - R, z1 + L, x1 + L - R, z1 + L - R, hy, quarter, kappa);
    put_seg(r, 4u, x1 + L, z1 + L - R, 0.f, 0.f, kCarPi, dz, 0.f);
    put_seg(r, 5u, x1 + L, z0 - L + R, x1 + L - R, z0 - L + R, kCarPi, quarter, kappa);
    put_seg(r, 6u, x1 + L - R, z0 - L, 0.f, 0.f, -hy, dx, 0.f);
    put_seg(r, 7u, x0 - L + R, z0 - L, x0 - L + R, z0 - L + R, -hy, quarter, kappa);
    r->nseg      = kCarSegmentCap;
    r->perimeter = 2.f * dx + 2.f * dz + 4.f * quarter;
}

void car_ring_pose(const CarRing* r, float s, float* x, float* z, float* yaw) {
    if (!r || r->nseg == 0u || r->perimeter <= 0.f) {
        if (x) {
            *x = 0.f;
        }
        if (z) {
            *z = 0.f;
        }
        if (yaw) {
            *yaw = 0.f;
        }
        return;
    }
    s = wrap_len(s, r->perimeter);
    u32 i = 0;
    while (i + 1u < r->nseg && s >= r->seg[i].len) {
        s -= r->seg[i].len;
        ++i;
    }
    const CarSegment& g = r->seg[i];
    if (g.kappa == 0.f) {
        if (x) {
            *x = g.sx + s * std::sin(g.yaw0);
        }
        if (z) {
            *z = g.sz + s * std::cos(g.yaw0);
        }
        if (yaw) {
            *yaw = wrap_yaw(g.yaw0);
        }
        return;
    }
    const float y   = g.yaw0 + g.kappa * s;
    const float inv = 1.f / g.kappa; // signed radius; negative for a left turn
    if (x) {
        *x = g.cx - inv * std::cos(y);
    }
    if (z) {
        *z = g.cz + inv * std::sin(y);
    }
    if (yaw) {
        *yaw = wrap_yaw(y);
    }
}

void car_traffic_build(CarTraffic* t, float block_pitch, u32 corolla_available,
                       u32 sports_available) {
    if (!t) {
        return;
    }
    std::memset(t, 0, sizeof(*t));
    if ((!corolla_available && !sports_available) || block_pitch <= 0.f) {
        return;
    }
    for (u32 p = 0; p < kPlanCount && t->ring_count < kCarLoopCap; ++p) {
        const CarLoopPlan& plan = kPlan[p];
        CarRing*           ring = &t->rings[t->ring_count];
        car_ring_init(ring, block_pitch * static_cast<float>(plan.i0),
                      block_pitch * static_cast<float>(plan.j0),
                      block_pitch * static_cast<float>(plan.i0 + 1u),
                      block_pitch * static_cast<float>(plan.j0 + 2u), kCarLaneOffset,
                      kCarCornerRadius);
        if (ring->nseg == 0u) {
            continue;
        }
        ring->speed     = plan.speed;
        ring->sports    = plan.sports;
        ring->travelled = 0.0;

        // Even spacing is the whole collision story: one speed per loop, so no vehicle
        // ever gains on the one ahead of it. The stagger keeps the loops from looking
        // like one machine — it never changes the spacing, so it stays safe.
        const u32   n       = kCarCarsPerLoop;
        const float spacing = ring->perimeter / static_cast<float>(n);
        const float phase   = 0.37f * spacing * static_cast<float>(t->ring_count);
        for (u32 k = 0; k < n && t->agent_count < kCarAgentCap; ++k) {
            CarAgent*  a        = &t->agents[t->agent_count];
            const bool want_fast = plan.sports != 0 && sports_available != 0u;
            a->ring             = t->ring_count;
            a->mesh             = want_fast ? 1u : 0u;
            if (corolla_available == 0u && !want_fast) {
                a->mesh = 1u; // Corolla mesh missing: fill the slow loops with the other one
            }
            a->offset = wrap_len((0.5f + static_cast<float>(k)) * spacing + phase, ring->perimeter);
            ++t->mesh_count[a->mesh];
            ++t->agent_count;
        }
        ++t->ring_count;
    }
}

[[nodiscard]] float car_agent_s(const CarTraffic* t, u32 index) {
    if (!t || index >= t->agent_count) {
        return 0.f;
    }
    const CarAgent& a = t->agents[index];
    const CarRing&  r = t->rings[a.ring];
    if (r.perimeter <= 0.f) {
        return 0.f;
    }
    // The travelled term is shared by the whole loop, so any rounding in it is common to
    // every vehicle on that loop and cancels out of the spacing between them.
    const double leg = std::fmod(r.travelled, static_cast<double>(r.perimeter));
    return wrap_len(static_cast<float>(leg) + a.offset, r.perimeter);
}

void car_agent_pose(const CarTraffic* t, u32 index, float* x, float* z, float* yaw) {
    if (!t || index >= t->agent_count) {
        if (x) {
            *x = 0.f;
        }
        if (z) {
            *z = 0.f;
        }
        if (yaw) {
            *yaw = 0.f;
        }
        return;
    }
    car_ring_pose(&t->rings[t->agents[index].ring], car_agent_s(t, index), x, z, yaw);
}

void car_traffic_step(CarTraffic* t, float dt) {
    if (!t || dt <= 0.f || t->ring_count == 0u) {
        return;
    }
    if (dt > 0.25f) {
        dt = 0.25f; // never teleport vehicles after a stall
    }
    for (u32 r = 0; r < t->ring_count; ++r) {
        t->rings[r].travelled +=
            static_cast<double>(t->rings[r].speed) * static_cast<double>(dt);
    }
    t->sim_time += dt;
}

} // namespace engine
