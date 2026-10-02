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

// One circuit per city block column, laid out along the avenue the city sandbox
// opens on (camera starts at x = 1200, z = 130 looking north up the street at
// x = 1200). Each circuit is one block wide and two blocks long, so the two
// streets flanking the avenue carry both directions of traffic and vehicles run
// straight for 240 m before the next left turn.
//
// Every circuit has the same perimeter, so holding the lap period constant gives
// every vehicle the same speed and a fixed timing between circuits: the phases
// below can then be chosen once and stay valid forever (see the sandbox, which
// replays laps and proves no two vehicles share asphalt).
struct CarRingPlan {
    u32   i0, j0, i1, j1; // corners in street-grid lines, times block pitch
    u32   vehicles;
    float phase; // arc-length offset of the first vehicle, metres
};

constexpr u32 kPlanCount = 8;

// Phases were searched once (see the city traffic sandbox) so that no two circuits
// reach a shared intersection at the same instant. Because every circuit has the
// same perimeter and the same lap period, these offsets hold forever, and they still
// hold after the traversal was flipped to left turns: the closest approach over ten
// laps is 8.40 m, exactly the opposite-lane separation, so nothing can come nearer.
// Retune them if the vehicle counts per circuit change — the sandbox checks it.
const CarRingPlan kPlan[kPlanCount] = {
    {9u, 0u, 10u, 2u, 4u, 0.f},      // west of the avenue, z 0-240 m
    {9u, 2u, 10u, 4u, 4u, 33.17f},   // west of the avenue, next block north
    {9u, 4u, 10u, 6u, 4u, 15.62f},
    {9u, 6u, 10u, 8u, 4u, 29.51f},
    {10u, 0u, 11u, 2u, 4u, 155.09f}, // east of the avenue, z 0-240 m
    {10u, 2u, 11u, 4u, 4u, 143.32f},
    {10u, 4u, 11u, 6u, 3u, 95.09f},
    {10u, 6u, 11u, 8u, 3u, 135.73f},
};

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

    u32 vehicle = 0;
    for (u32 p = 0; p < kPlanCount && t->ring_count < kCarRingCap; ++p) {
        const CarRingPlan& plan = kPlan[p];
        if (plan.vehicles == 0u) {
            continue;
        }
        CarRing* ring = &t->rings[t->ring_count];
        car_ring_init(ring, block_pitch * static_cast<float>(plan.i0),
                      block_pitch * static_cast<float>(plan.j0),
                      block_pitch * static_cast<float>(plan.i1),
                      block_pitch * static_cast<float>(plan.j1), kCarLaneOffset, kCarCornerRadius);
        if (ring->nseg == 0u) {
            continue;
        }
        const u32   n       = plan.vehicles;
        const float spacing = ring->perimeter / static_cast<float>(n);
        // Same lap period everywhere: equal speed, and timing between circuits never drifts.
        const float speed   = ring->perimeter / kCarLapPeriodS;
        for (u32 k = 0; k < n && t->agent_count < kCarAgentCap; ++k) {
            CarAgent* a = &t->agents[t->agent_count];
            a->ring     = t->ring_count;
            // Every third vehicle is the sports car: 20 Corolla + 10 sports.
            a->mesh     = (sports_available && (!corolla_available || (vehicle % 3u) == 0u)) ? 1u : 0u;
            a->speed    = speed;
            a->s        = wrap_len((0.5f + static_cast<float>(k)) * spacing + plan.phase,
                                   ring->perimeter);
            ++t->mesh_count[a->mesh];
            ++t->agent_count;
            ++vehicle;
        }
        ++t->ring_count;
    }
}

void car_traffic_step(CarTraffic* t, float dt) {
    if (!t || dt <= 0.f || t->agent_count == 0u) {
        return;
    }
    if (dt > 0.25f) {
        dt = 0.25f; // never teleport vehicles after a stall
    }
    for (u32 i = 0; i < t->agent_count; ++i) {
        CarAgent& a = t->agents[i];
        a.s         = wrap_len(a.s + a.speed * dt, t->rings[a.ring].perimeter);
    }
    t->sim_time += dt;
}

} // namespace engine
