# Leonida Engine Changelog

## Unreleased — Wheels that spin

### Rendering (`engine/render/CarWheelFit.h`, `TreeGlb.*`, `shaders/tree.vert`)
- ✅ The car wheels now rotate while driving: `wheel_angle = distance_travelled / wheel_radius`,
  uploaded as one float per vehicle and applied in the vertex shader, so the extra cost is a
  float per instance and a handful of uniforms per car primitive — the instanced draws, the
  two draw calls and the 60 FPS budget are untouched
- ✅ The wheel radius is measured from the mesh, not assumed: the loader finds the wheel
  meshes (name/material keywords), fits every cluster of vertices as a disc, and multiplies
  the fitted radius by the same scale the body uses, so it is a real world radius (the
  Corolla's tyres come out near the expected 0.3 m)
- ✅ A primitive is only spun if its geometry actually fits a wheel (thin axle axis, round
  plane), so bumpers, mirrors and spoilers can never be caught by a name; primitives without
  wheels pay nothing at all (`uWheelCount == 0`)
- ✅ Spin direction rolls the tyre, it does not skid it: the contact patch moves backwards
  along the car's nose, and the sign is derived from the model's own axes, so mirrored
  meshes and both body yaw offsets come out right
- ✅ If a car's mesh and material names never say "wheel", the loader falls back to shape:
  a fitted disc is accepted only when it is thin about the car's width axis *and* its lowest
  point stands on the model floor, so headlights, mirrors, exhaust tips and a spare tyre in
  the boot are refused and the fallback cannot spin the wrong part
- ✅ Wheel parts whose geometry is not a clean disc — a brake disc with its caliper, a rim
  with bolt heads, a hub cap — used to be refused and left static, so a spinning tyre had a
  frozen rim inside it. The name still says they belong to the wheel, so they are now
  matched to the wheel they are concentric with and spin with it: same centre, same axis,
  same per-vehicle angle as the tyre. A part that is metres wide (a fender, a sill, a whole
  underside) or that is not concentric with a wheel is still refused, so nothing else moves
- ✅ The keyword list lacked **"brake"**, which is the word the Corolla's brake nodes use:
  `wheelbrake.Ft.L_metal_rough_plus_0`. Brakes, rims and rotors now count as wheel hardware
  and get looser limits when they are matched to a wheel, because a caliper makes such a
  part wider than the disc. A name that says bodywork (a wheel arch, a wheel well, a wheel
  trim, "hjulbue") is disqualified however it is spelled, and a keyword glued to the end of
  another word only counts after "wheel" or "hjul" — a bare substring test for "rim" would
  otherwise happily match "tRIM"
- ✅ The Corolla's brakes stayed still because all four live in **one** primitive: the loader
  parked the whole cloud as a single box the size of the car, and a box that big can never be
  concentric with a wheel, so every brake was refused every frame. A wheel-named part is now
  split into its wheels first (the same mid-plane rule the disc test uses), and each part is
  matched to its own wheel — so a primitive holding all four brakes gets four centres, which
  `TreePrim` and the shader already support. One brake per primitive and rims with bolt
  heads work the same way
- ✅ Last resort for wheel hardware whose parts overlap so the split cannot separate them:
  the whole primitive takes every fitted wheel centre, and the vertex shader rotates each
  vertex about the nearest one — the same rule that already lets a tyre primitive hold all
  four wheels. Guarded by the part's size relative to the wheels, so a sill or an underside
  is still refused
- ✅ One decisive report line per car model, so a wheel part that is *not* rotating can never
  be silent again: `[glb] Corolla E80 wheel report: 8 primitive(s) spin — 4 by name, 4 by
  shape, 4 attached to a wheel; 16 wheel(s), nothing wheel-like left static`. A part that is
  refused says why in numbers: `… left static — nearest fitted wheel centre is 2.31 m away,
  part is 0.62 m across (wheel hardware)`
- ✅ The shadow pass spins the wheels too (identical uniforms, so the spokes in the shadow
  turn with the wheel), and both inline fallback shaders carry the same code as the files
- ✅ Sandbox: 38 new headless checks (fit finds 4 wheels in a merged primitive, refuses a
  box, the patch rolls backwards through the whole basis+yaw chain for both bodies, and
  without slipping, plus the floor test, the attach rule and the real asset node names
  from the Corolla) — 77/77
- ✅ Startup reports what it found: `[glb] … WHEEL mesh …` per wheel primitive and
  `[cars] … N wheel part(s) in M primitive(s) spin about Z, tyre radius 0.29 m world, roll -1`

## Unreleased — Citywide traffic with no collisions

### Traffic (`engine/render/CarTraffic.*`)
- ✅ 16 loops spread over the whole 2.4 km grid instead of one avenue, 128 vehicles
  (96 Corolla E80 + 32 sports), still 2 instanced draw calls and no per-frame logging
- ✅ Varied speeds: Corolla loops 8.2-9.8 m/s (30-35 km/h), sports loops 12.6-14.2 m/s
  (45-51 km/h) — the sports cars never share a path with the slow ones
- ✅ Zero-collision guarantee without per-frame physics, from two invariants:
  1. one speed per loop, so the even spacing a loop spawned with can never close up
     (92.9 m between vehicles); the loop's travelled distance is a shared double clock
     and the vehicles are constant offsets, so the spacing cannot even drift
  2. the loops are pairwise disjoint — closest two loops are 111.6 m apart — so no
     vehicle on one loop can meet a vehicle on another, whatever the speeds
- ✅ Disjointness comes from the lattice law: i even, j a multiple of 4, asserted
  in the sandbox (as is the lattice itself, the speed classes, the spacing and the flow)
- ✅ Right-hand traffic re-asserted on all 64 straights after the change
- ✅ Hardware check on the Mac: the fleet drives nose-first with the default body yaw
  offset, so the boot-first bug is closed for good (F in the sandbox flips it if a
  future model is authored the other way round)

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
