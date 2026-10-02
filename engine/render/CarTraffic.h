#pragma once

#include "core/Types.h"

namespace engine {

// Moving city traffic for the GL city pass: closed left-turn circuits around
// rectangular block groups. Not a generic Vehicle / Path / TrafficLight graph —
// one ring is one drivable lane loop, so vehicles on a ring can never collide.
//
// Geometry law: the circuit is offset outwards from the block-group rectangle by
// `lane` metres, every corner is a left turn of radius `radius`, and the heading
// follows the city pass convention forward = (sin yaw, 0, cos yaw). Driving on the
// right-hand lane therefore falls out of the offset+left-turn pairing: the lane a
// circuit drives in is always the right-hand lane of its travel direction.

inline constexpr float kCarPi           = 3.14159265358979323846f;
inline constexpr u32   kCarSegmentCap   = 8;  // 4 straights + 4 corner arcs
inline constexpr u32   kCarRingCap      = 12;
inline constexpr u32   kCarAgentCap     = 32; // kCorollaSpawnCap + kSportsSpawnCap
inline constexpr u32   kCarVehicleCount = 30; // 20 Corolla E80 + 10 sports
inline constexpr float kCarLaneOffset   = 4.2f;  // lane centre, metres off the street centre line
inline constexpr float kCarCornerRadius = 6.0f;  // left-turn radius at every intersection
inline constexpr float kCarLapPeriodS   = 67.0f; // one lap in 67 s ≈ 40 km/h, equal on every circuit

// One straight or one constant-curvature corner of a circuit.
struct CarSegment {
    float sx, sz;  // segment start point (straight) — arcs also use it as the tangent entry
    float cx, cz;  // turn centre, arcs only
    float yaw0;    // heading at the segment start, radians
    float len;     // arc length in metres
    float kappa;   // d(yaw)/ds : 0 straight, -1/radius left turn
};

struct CarRing {
    CarSegment seg[kCarSegmentCap];
    u32        nseg;
    float      perimeter;
    float      x0, z0, x1, z1; // block-group corners, on the street centre lines
    float      lane;
    float      radius;
};

// One vehicle on a ring. Mesh 0 = Corolla E80, mesh 1 = sports car.
struct CarAgent {
    u32   ring;
    u32   mesh;
    float s;     // distance travelled along the ring, metres
    float speed; // m/s, constant per ring so ring spacing is preserved forever
};

struct CarTraffic {
    CarRing  rings[kCarRingCap];
    u32      ring_count;
    CarAgent agents[kCarAgentCap];
    u32      agent_count;
    u32      mesh_count[2];
    float    sim_time;
};

// Builds one left-turn circuit around the rectangle (x0,z0)-(x1,z1).
void car_ring_init(CarRing* r, float x0, float z0, float x1, float z1, float lane, float radius);

// Pose at arc length s (wrapped). Heading matches the city pass: forward = (sin yaw, 0, cos yaw).
void car_ring_pose(const CarRing* r, float s, float* x, float* z, float* yaw);

// Spawns the downtown plan: one circuit per block column along the starting avenue.
void car_traffic_build(CarTraffic* t, float block_pitch, u32 corolla_available, u32 sports_available);

// Advances every vehicle at its ring speed. Free, no allocation.
void car_traffic_step(CarTraffic* t, float dt);

} // namespace engine
