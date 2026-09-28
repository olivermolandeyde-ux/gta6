#!/usr/bin/env bash
# Master Linux/macOS build: C++20 engine + HLSL stage + citygen + full sandbox.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "==> Leonida Engine MICRO-PHASE 10 master build"
mkdir -p build output/city

if command -v cmake >/dev/null 2>&1; then
    echo "==> cmake configure + build"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j"$(nproc 2>/dev/null || echo 4)"
    SANDBOX=./build/leonida_full_sandbox
else
    echo "==> cmake not found; g++ fallback"
    g++ -std=c++20 -O2 -Iengine \
        engine/core/Assert.cpp \
        engine/memory/PageAllocator.cpp \
        engine/memory/LinearAllocator.cpp \
        engine/memory/PoolAllocator.cpp \
        engine/memory/StackAllocator.cpp \
        engine/memory/FrameAllocator.cpp \
        engine/memory/MemorySystem.cpp \
        engine/ecs/ComponentRegistry.cpp \
        engine/ecs/Chunk.cpp \
        engine/ecs/Archetype.cpp \
        engine/ecs/CommandBuffer.cpp \
        engine/ecs/Query.cpp \
        engine/ecs/World.cpp \
        engine/ecs/SystemScheduler.cpp \
        engine/render/RenderPipeline.cpp \
        engine/objects/VehicleEngine.cpp \
        engine/objects/BreakableWindow.cpp \
        engine/objects/StreetLight.cpp \
        engine/physics/VehicleDynamics.cpp \
        engine/ai/PedestrianBehavior.cpp \
        engine/animation/TwoBoneIK.cpp \
        engine/animation/AnimationStateMachine.cpp \
        engine/world/WorldStreamer.cpp \
        engine/eco/TrafficSystem.cpp \
        engine/eco/PowerGridSystem.cpp \
        engine/network/NetworkCore.cpp \
        engine/network/InterestManagement.cpp \
        engine/network/StateReplication.cpp \
        engine/audio/AudioEngine.cpp \
        engine/eco/WeatherSystem.cpp \
        engine/vfx/ParticleSystem.cpp \
        sandbox/FullIntegrationSandbox.cpp \
        -o build/leonida_full_sandbox -pthread \
        -DLEONIDA_SOURCE_DIR=\"$ROOT\"
    SANDBOX=./build/leonida_full_sandbox
    mkdir -p build/shaders
    cp -f shaders/*.hlsl build/shaders/ 2>/dev/null || true
fi

PY=""
if [[ -x "$ROOT/tools/.venv/bin/python" ]]; then
    PY="$ROOT/tools/.venv/bin/python"
elif command -v python3 >/dev/null 2>&1; then
    PY=python3
fi

if [[ -n "$PY" ]]; then
    echo "==> citygen 2 km"
    (cd "$ROOT/tools/citygen" && "$PY" master_generate.py --size-km 2 --seed 42 --coast west --out "$ROOT/output/city")
else
    echo "==> python not found; sandbox will use inline scene"
fi

echo "==> full integration sandbox"
"$SANDBOX"
echo "==> MASTER BUILD COMPLETE"
