# Leonida Engine

A C++20 open-world simulation core built from scratch. No generic `Prop` / `Actor` / `Vehicle` / `Weapon` / `Item` bases. No CRT heap on the simulation path after `MemorySystem::boot()`.

```
  ██████╗ ██████╗  ██████╗      ██╗███████╗ ██████╗████████╗
  ██╔══██╗██╔══██╗██╔═══██╗     ██║██╔════╝██╔════╝╚══██╔══╝
  ██████╔╝██████╔╝██║   ██║     ██║█████╗  ██║        ██║
  ██╔═══╝ ██╔══██╗██║   ██║██   ██║██╔══╝  ██║        ██║
  ██║     ██║  ██║╚██████╔╝╚█████╔╝███████╗╚██████╗   ██║
  ╚═╝     ╚═╝  ╚═╝ ╚═════╝  ╚════╝ ╚══════╝ ╚═════╝   ╚═╝
  PHASE 18 — Terrain · Preetham sky · volumetric clouds · day/night
```

## Architecture

| Layer | Law |
|---|---|
| Memory | Page → world/streaming linear arenas, 16 KiB chunk pool, lock-free pools, double-buffered frame arena |
| ECS | Generational 64-bit entities, 256-bit signatures, SoA chunks, deferred `CommandBuffer` |
| Instantiation | L1–L8 in `engine/ecs/InstantiationRules.h` |

Systems tick as free functions (no virtual `System` base): render, ICE, glass, lights, chassis/Ackermann, pedestrian flee + two-bone IK, cell streamer, traffic ACC, power grid, AOI replication, Doppler/occlusion audio, rain/friction, particles.

## Build and run

```bash
# Master pipeline (engine + shaders + 2 km city + 10 s sandbox)
chmod +x scripts/build_all.sh
./scripts/build_all.sh
```

Windows: `scripts\build_all.bat`

CMake targets:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Native terrain (SDL2 + OpenGL 3.3). macOS: brew install sdl2
cmake --build build --target leonida_terrain
./build/leonida_terrain
cmake --build build --target generate_city
cmake --build build --target run_full_sandbox
```

Citygen only:

```bash
tools/.venv/bin/python tools/citygen/master_generate.py --size-km 2 --seed 42 --out output/city
```

## Phases

1. Memory director + archetype ECS  
2. Geometry pass, GGX PBR, skin SSS  
3. `VehicleEngine`, `BreakableWindow`, `StreetLight`  
4. Spring-damper / Ackermann / Pacejka, clearcoat + tire HLSL  
5. Pedestrian flee AI, two-bone IK, foot IK  
6. Streaming cells, traffic, power grid  
7. AOI hash, RPC pool, client prediction  
8. Doppler/occlusion audio, rain friction, wind particles  
9. Python citygen → `LEONCELL` binary prefabs  
10. Master build + 600-frame full integration sandbox  
11. Ballistics, dedicated weapons, vehicle enter/exit, grid inventory, HUD  
12. Data-driven missions (`LEONMISS`), dialog trees, objective trackers, rewards  
13. Tactical police, gang territories, civilian routines, danger-aware navigation  
14. Binary world save/load (`LEONSAVE`), XOR payload, save-slot metadata  
15. Server-authoritative deathmatch, racing, co-op revive (AOI replication)  
16. Gold Master: GitHub Actions CI/CD, release tarball, asset manifest, benchmarks  

## Full sandbox (verified)

```
CRT heap allocations after boot: 0
Total entities: 132
Streaming cells loaded: 9
Traffic vehicles simulated: 3
Pedestrians with fear > 0.5: 1
Windows shattered: 1
Street lights outaged: 17
Network packets sent: 1
Audio events processed: 600
Weather intensity: 1.000
Road friction multiplier: 0.600
Pedestrian fear_level: 1.000
Street light voltage: 0.000
Doppler observed Hz: 1235.67
FINAL STATUS: FULL INTEGRATION SANDBOX PASSED
```

Shaders: `shaders/MasterPBR.hlsl`, `SkinSSS.hlsl`, `CarPaintClearcoat.hlsl`, `TireRubber.hlsl`.
