# Leonida Engine Changelog

## Unreleased — Traffic orientation and lane side

### Bug fixes
- ✅ Cars drove boot-first: the AABB longest axis of both bodies points at the boot, so
  the ring heading needed 180° on top of it (the sports mesh keeps its extra 180°)
- ✅ Cars drove on the left: the lane offsets were paired with *right* turns, which put
  the street centre line on the driver's right (British-style). The circuits are now
  traversed as left turns, so every straight keeps the centre line on the driver's left
- ✅ Per-straight right-hand-traffic assertion plus a "no lane carries both directions"
  check in the headless sandbox, so a future traversal flip cannot silently break it
- ✅ Closest approach stays 8.40 m over three laps — the opposite-lane separation, which
  is the tightest two vehicles can ever come

### Diagnostics
- ✅ Live body-yaw flip: press F in the city sandbox to add/remove 180° on both car bodies
  (and the startup log prints the current offset), so which end of the authored mesh is
  the bonnet can be settled on screen instead of guessed in code

### Build / CI
- ✅ `actions/upload-artifact@v4`, `linux-perf` fallback, SDL2/GL headers in CI

## Unreleased — Living city traffic

### Moving vehicles (`engine/render/CarTraffic.*`)
- ✅ 8 closed left-turn circuits down the avenue the city sandbox opens on (x = 1200 m, z = 0-960 m)
- ✅ 30 vehicles driving: 20 Toyota Corolla E80 + 10 sports cars, 2 instanced draw calls
- ✅ Right-hand lane discipline, 4.2 m lane centres, 6 m left turns on the 20 m carriageway
- ✅ Constant lap period → fixed timing between circuits, so no two vehicles ever share asphalt
  (verified closest approach: 8.40 m, the opposite-lane separation)
- ✅ Per-frame instance re-upload from `BuildingGlPass::draw()` — no per-frame logging, 60 FPS kept
- ✅ Headless `leonida_city_traffic_sandbox`: geometry, continuity, lane side, spacing and flow

### Build / CI
- ✅ SDL2 + OpenGL viewers are optional CMake targets; configure no longer fails without SDL2
- ✅ CI: actions bumped to v4, SDL2/GL headers installed, traffic sandbox wired into the pipeline

## Version 1.0.0 (Gold Master)

### Core Engine (Phases 1-8)
- ✅ Custom C++20 memory director (zero CRT heap after boot)
- ✅ Archetype ECS with generational 64-bit handles
- ✅ HLSL PBR rendering (GGX/Smith/Schlick)
- ✅ Granular object system (unique components per object type)
- ✅ Vehicle physics with Ackermann steering and tire dynamics
- ✅ Two-Bone IK and animation state machines
- ✅ Massive open-world streaming with zero fragmentation
- ✅ Server-authoritative networking with AOI and client prediction
- ✅ Spatial audio with Doppler and occlusion
- ✅ Dynamic weather affecting physics and rendering

### Content Pipeline (Phase 9)
- ✅ Procedural city generation (2km x 2km, 36,864 windows, 15,228 road nodes)
- ✅ Binary data format (LEONCELL, LEONMISS, LEONAIDA)
- ✅ 5.47 MiB compressed city data

### Gameplay Systems (Phases 10-15)
- ✅ Ballistic system with true projectile physics
- ✅ Weapon system (pistol, rifle, shotgun) with recoil patterns
- ✅ Grid-based inventory with weight penalties
- ✅ Mission system with data-driven objectives and dialog trees
- ✅ Advanced AI (tactical police, gang territories, civilian routines)
- ✅ Full world state serialization (901 bytes save size)
- ✅ Multiplayer modes (Deathmatch, Racing, Co-op)

### Release Engineering (Phase 16)
- ✅ GitHub Actions CI/CD pipeline
- ✅ Automated testing across all 16 phases
- ✅ Performance profiling and optimization
- ✅ Release packaging with asset manifest
- ✅ Benchmark suite for all systems

### Performance Metrics (Final)
- Frame time: <16ms (60 FPS target)
- Memory usage: 0 CRT heap allocations after boot
- Load time: <3 seconds for full 2km x 2km city
- Save/Load: <100ms for full world state
- Network: Server-authoritative with <50ms latency
