# Leonida Engine — MICRO-PHASE 2

Custom C++20 core for a 1:1 open-world simulation. **No CRT heap on the simulation path. No garbage collector. No generic `Prop` base class.**

This repository contains **MICRO-PHASE 1–6**: memory, ECS, render, unique objects, vehicle dynamics, pedestrian AI/IK, world-cell streaming (per-cell LinearAllocator), traffic ACC, and a municipal power grid.

## Memory domains

| Domain | Allocator | Lifetime | Legal contents |
|---|---|---|---|
| OS pages | `PageAllocator` (`mmap` / `VirtualAlloc`) | process | backing for every other domain |
| World arena | `LinearAllocator` | world load → tear-down | archetypes, entity table, prefab blobs |
| Streaming arena | `LinearAllocator` | per world-cell | cell-local scratch, not entity bytes |
| Chunk pool | lock-free `PoolAllocator` 16 KiB | churn | SoA component slabs |
| Record pool | lock-free `PoolAllocator` | churn | reserved for overflow records |
| Stack | LIFO `StackAllocator` | nested scope | physics island scratch |
| Frame | double-buffered `FrameAllocator` | one frame | query scratch, debug draw. **NEVER entities.** |

## ECS

- **Entity** = 64-bit handle (`index | generation | world_id`). Stale handles fail `is_alive()`.
- **Archetype** = unique component bitmask. Entities with the same mask share 16 KiB SoA chunks.
- **Structural changes** (add/remove component) move the entity to a new archetype via column memcpy. No allocator traffic besides a possible new chunk.
- **Destroy** is swap-remove. Iteration is dense `0..count` with no hole branches.
- **CommandBuffer** defers structural changes until after system iteration.
- **Systems** are function pointers staged by `PipelineStage`. No virtual `System` base.

Instantiation laws live in `engine/ecs/InstantiationRules.h` (L1–L8).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/leonida_phase1_sandbox
./build/leonida_phase2_sandbox
./build/leonida_phase3_sandbox
./build/leonida_phase4_sandbox
./build/leonida_phase5_sandbox
./build/leonida_phase6_sandbox
```

Shaders live in `shaders/MasterPBR.hlsl` (GGX + Smith + Schlick, gamma 2.2) and `shaders/SkinSSS.hlsl` (wrapped-Lambert + pre-integrated LUT). CMake stages them into the build directory and, if `dxc` is on PATH, compiles `PS_Main` to `MasterPBR.ps.cso`.

`RenderGeometryPass` snapshots `TransformComponent` + `RenderableComponent` handles into the **frame arena** (L2/L6), then records GPU commands. Component pointers are not stored across frames.

## Roadmap

1. **MICRO-PHASE 1** — Core engine & custom ECS
2. **MICRO-PHASE 2** — Render passes + master PBR HLSL + skin SSS
3. **MICRO-PHASE 3** — Unique object classes (`VehicleEngine`, `BreakableWindow`, `StreetLight`)
4. **MICRO-PHASE 4** — Vehicle suspension / Ackermann + clearcoat & tire shaders
5. **MICRO-PHASE 5** — Pedestrian flee AI, two-bone IK, animation state machine
6. **MICRO-PHASE 6** — World streaming, traffic, power grid *(this tree)*
