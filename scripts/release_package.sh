#!/bin/bash
# Master release packaging script
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "=== LEONIDA ENGINE RELEASE PACKAGING ==="
echo "Building Gold Master..."

# 1. Clean build
rm -rf build_release/
mkdir build_release/

# 2. Build with maximum optimization
if command -v cmake >/dev/null 2>&1; then
    cmake -S . -B build_release/ -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -DNDEBUG -ffast-math -march=native"
    cmake --build build_release/ --parallel "$(nproc 2>/dev/null || echo 4)" --target leonida_engine
else
    echo "==> cmake not found; g++ Gold Master fallback"
    mkdir -p build_release
    g++ -std=c++20 -O3 -DNDEBUG -ffast-math -march=native -Iengine \
        -DLEONIDA_SOURCE_DIR=\"$ROOT\" \
        sandbox/LeonidaEngineMain.cpp \
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
        engine/gameplay/PlayerController.cpp \
        engine/mission/RewardSystem.cpp \
        engine/save/WorldSerializer.cpp \
        engine/save/WorldDeserializer.cpp \
        engine/save/SaveSlotManager.cpp \
        -o build_release/leonida_engine -pthread
fi

# 3. Strip debug symbols (reduce binary size)
if [ -f build_release/leonida_engine ]; then
    strip --strip-debug build_release/leonida_engine || true
fi

# 4. Generate all data assets
mkdir -p release/data
PY=python3
if [ -x tools/.venv/bin/python ]; then
    PY=tools/.venv/bin/python
fi
"$PY" tools/citygen/master_generate.py --ai-only --output release/data/
if [ -d output/city/cells ]; then
    mkdir -p release/data/cells
    CELL0="$(find output/city/cells -name '*.cell' | head -n 1)"
    if [ -n "$CELL0" ]; then
        cp -f "$CELL0" release/data/cells/
        cp -f "$CELL0" release/data/city.cell
    fi
else
    "$PY" tools/citygen/master_generate.py --size-km 0.128 --output release/data/
fi

# 5. Create release directory structure
mkdir -p release/bin
mkdir -p release/data/missions
mkdir -p release/data/ai
mkdir -p release/shaders
mkdir -p release/docs

cp -f build_release/leonida_engine release/bin/
cp -f shaders/*.hlsl release/shaders/ 2>/dev/null || true
cp -f README.md release/docs/
cp -f CHANGELOG.md release/docs/
cp -f data/missions/test_mission.mission release/data/missions/ 2>/dev/null || true
cp -f data/ai/test_city.ai release/data/ai/ 2>/dev/null || true

# Flatten a Gold Master city.cell for verification (first generated cell or staged copy)
CELL="$(find release/data -name '*.cell' | head -n 1 || true)"
if [ -n "$CELL" ]; then
    cp -f "$CELL" release/data/city.cell
elif [ -d output/city/cells ]; then
    CELL="$(find output/city/cells -name '*.cell' | head -n 1 || true)"
    if [ -n "$CELL" ]; then
        cp -f "$CELL" release/data/city.cell
    fi
fi

# 6. Create asset manifest (for patching/delta updates)
python3 scripts/generate_manifest.py release/ > release/manifest.json

# 7. Create installer package
rm -f leonida_engine_v1.0.tar.gz
(cd release/ && tar -czf ../leonida_engine_v1.0.tar.gz *)

# 8. Calculate checksums
sha256sum leonida_engine_v1.0.tar.gz > leonida_engine_v1.0.tar.gz.sha256

echo "=== RELEASE PACKAGING COMPLETE ==="
echo "Package: leonida_engine_v1.0.tar.gz"
echo "Checksum: leonida_engine_v1.0.tar.gz.sha256"
ls -lh leonida_engine_v1.0.tar.gz
