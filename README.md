# Leonida Engine

**PROJECT COMPLETE.** A C++20 open-world simulation core built from scratch across 10 micro-phases. No generic `Prop` / `Actor` / `Vehicle` bases. No CRT heap on the simulation path after `MemorySystem::boot()`.

```
  ██████╗ ██████╗  ██████╗      ██╗███████╗ ██████╗████████╗
  ██╔══██╗██╔══██╗██╔═══██╗     ██║██╔════╝██╔════╝╚══██╔══╝
  ██████╔╝██████╔╝██║   ██║     ██║█████╗  ██║        ██║
  ██╔═══╝ ██╔══██╗██║   ██║██   ██║██╔══╝  ██║        ██║
  ██║     ██║  ██║╚██████╔╝╚█████╔╝███████╗╚██████╗   ██║
  ╚═╝     ╚═╝  ╚═╝ ╚═════╝  ╚════╝ ╚══════╝ ╚═════╝   ╚═╝
  COMPLETE — 10 phases · 8 systems · 36,864 windows · 0 CRT mallocs
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
