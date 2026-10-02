#pragma once

#include "core/Types.h"

namespace engine {

// Moving city traffic for the GL city pass: 15 closed left-turn loops spread over the
// 2.4 km grid, carrying 30 vehicles in three classes (Corolla, sports, SUV). Not a generic Vehicle / Path / TrafficLight graph — a loop is a drivable
// circuit with one speed for the whole loop, which keeps the collision argument trivial:
//
//   1. On a loop every vehicle runs at the same speed, so the even spacing it spawned
//      with is invariant — the spacing is the safe following distance, and it can never
//      close up. No following model, no braking, no per-frame collision test.
//   2. The loops are pairwise disjoint: no two loops share a metre of asphalt, so a
//      vehicle on one loop can never meet a vehicle on another, whatever the speeds.
//
// That is what lets the sports cars run 12-15 m/s while the Corollas hold 8-10 m/s and
// the SUVs 7-9 m/s without a single per-frame distance check (which instanced rendering
// cannot afford). One class per loop keeps the model and the speed tied together.
//
// Geometry law: the circuit is offset outwards from the block rectangle by `lane` metres,
// every corner is a left turn of radius `radius`, and the heading follows the city pass
// convention forward = (sin yaw, 0, cos yaw). The offset+left-turn pairing is what puts
// every straight in the right-hand lane of its direction of travel (the centre line stays
// on the driver's left). The sandbox asserts the lane side and the disjointness.
//
// Traffic plan law: loop (i, j) drives the streets around the block at
// x = pitch*i .. pitch*(i+1), z = pitch*j .. pitch*(j+2) — one block wide, two blocks
// long — with i even and j a multiple of 4, which is what makes the loops disjoint.
// Each loop also states how many vehicles it carries and which class they are, so the
// fleet's 30 vehicles are spread over the whole grid instead of piling onto one avenue.

inline constexpr float kCarPi            = 3.14159265358979323846f;
inline constexpr u32   kCarSegmentCap    = 8;  // 4 straights + 4 corner arcs
inline constexpr u32   kCarLoopCap       = 24;
inline constexpr u32   kCarLoopCount     = 15; // 5 Corolla + 5 sports + 5 SUV loops
inline constexpr u32   kCarCorollaLoops  = 5;
inline constexpr u32   kCarSportsLoops   = 5;
inline constexpr u32   kCarSuvLoops      = 5;
inline constexpr u32   kCarMeshCount     = 3;  // 0 Corolla E80, 1 sports, 2 SUV
// Vehicles per loop, per class: 5*3 + 5*2 + 5*1 = 30, evenly spaced around each loop.
inline constexpr u32   kCarCarsPerLoop[kCarMeshCount] = {3u, 2u, 1u};
inline constexpr u32   kCarVehicleCount  =
    kCarCorollaLoops * kCarCarsPerLoop[0] + kCarSportsLoops * kCarCarsPerLoop[1] +
    kCarSuvLoops * kCarCarsPerLoop[2];
inline constexpr u32   kCarAgentCap      = 192; // headroom over kCarVehicleCount
inline constexpr float kCarLaneOffset    = 4.2f; // lane centre, metres off the street centre line
inline constexpr float kCarCornerRadius  = 6.0f; // left-turn radius at every intersection
inline constexpr float kCarCorollaSpeedMin = 8.0f;  // m/s — normal speed limits
inline constexpr float kCarCorollaSpeedMax = 10.0f;
inline constexpr float kCarSportsSpeedMin  = 12.0f; // m/s — the fast loops
inline constexpr float kCarSportsSpeedMax  = 15.0f;
inline constexpr float kCarSuvSpeedMin     = 7.0f;  // m/s — the SUVs roll a little slower
inline constexpr float kCarSuvSpeedMax     = 9.0f;

// One straight or one constant-curvature corner of a loop.
struct CarSegment {
    float sx, sz;  // segment start point (straight) — arcs also use it as the tangent entry
    float cx, cz;  // turn centre, arcs only
    float yaw0;    // heading at the segment start, radians
    float len;     // arc length in metres
    float kappa;   // d(yaw)/ds : 0 straight, +1/radius for a left turn
};

struct CarRing {
    CarSegment seg[kCarSegmentCap];
    u32        nseg;
    float      perimeter;
    float      x0, z0, x1, z1; // block rectangle corners, on the street centre lines
    float      lane;
    float      radius;
    float      speed;     // m/s, the same for every vehicle on this loop
    u32        cars;      // vehicles on this loop, evenly spaced
    int        cls;       // 0 Corolla, 1 sports, 2 SUV — every vehicle here is this model
    double     travelled; // metres every vehicle on this loop has covered, double so it
                          // never drifts (float fmod lost ~1 cm per three laps)
};

// One vehicle on a loop. Mesh 0 = Corolla E80, 1 = sports car, 2 = SUV.
struct CarAgent {
    u32   ring;
    u32   mesh;
    float offset; // metres along the loop; constant, so relative spacing is exact
};

struct CarTraffic {
    CarRing  rings[kCarLoopCap];
    u32      ring_count;
    CarAgent agents[kCarAgentCap];
    u32      agent_count;
    u32      mesh_count[kCarMeshCount];
    float    sim_time;
};

// Builds one left-turn circuit around the rectangle (x0,z0)-(x1,z1).
void car_ring_init(CarRing* r, float x0, float z0, float x1, float z1, float lane, float radius);

// Pose at arc length s (wrapped). Heading matches the city pass: forward = (sin yaw, 0, cos yaw).
void car_ring_pose(const CarRing* r, float s, float* x, float* z, float* yaw);

// Current arc length of a vehicle: the loop's own travelled distance plus the vehicle's
// constant offset. Every vehicle on a loop shares the travelled term, so the spacing
// between two of them is exactly their offset difference — it cannot close up.
[[nodiscard]] float car_agent_s(const CarTraffic* t, u32 index);

// Pose of a vehicle at its current loop position.
void car_agent_pose(const CarTraffic* t, u32 index, float* x, float* z, float* yaw);

// Spawns the citywide plan and spaces each loop's vehicles evenly around it. A class whose
// model failed to load is filled with a model that did load, so the fleet stays complete.
void car_traffic_build(CarTraffic* t, float block_pitch, const u32 available[kCarMeshCount]);

// Advances every vehicle at its loop speed. Free, no allocation, no collision work.
void car_traffic_step(CarTraffic* t, float dt);

} // namespace engine
