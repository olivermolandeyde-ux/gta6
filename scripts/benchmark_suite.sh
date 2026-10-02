#!/bin/bash
# Comprehensive benchmarking across all 16 phases

echo "=== LEONIDA ENGINE BENCHMARK SUITE ==="

BINARY="./build_release/leonida_engine"
if [ ! -x "$BINARY" ]; then
    BINARY="./build/leonida_engine"
fi

echo "[1/16] Phase 1: Memory & ECS..."
$BINARY --benchmark-phase 1 --frames 10000

echo "[2/16] Phase 2: Rendering Pipeline..."
$BINARY --benchmark-phase 2 --frames 10000

echo "[3/16] Phase 3: Granular Objects..."
$BINARY --benchmark-phase 3 --frames 10000

echo "[4/16] Phase 4: Vehicle Physics..."
$BINARY --benchmark-phase 4 --frames 10000

echo "[5/16] Phase 5: AI & IK..."
$BINARY --benchmark-phase 5 --frames 10000

echo "[6/16] Phase 6: World Streaming..."
$BINARY --benchmark-phase 6 --frames 10000

echo "[7/16] Phase 7: Networking..."
$BINARY --benchmark-phase 7 --frames 10000

echo "[8/16] Phase 8: Audio & Weather..."
$BINARY --benchmark-phase 8 --frames 10000

echo "[9/16] Phase 9: City Generation..."
python3 tools/citygen/master_generate.py --benchmark

echo "[10/16] Phase 10: Full Integration..."
$BINARY --benchmark-phase 10 --frames 10000

echo "[11/16] Phase 11: Gameplay Systems..."
$BINARY --benchmark-phase 11 --frames 10000

echo "[12/16] Phase 12: Mission System..."
$BINARY --benchmark-phase 12 --frames 10000

echo "[13/16] Phase 13: Advanced AI..."
$BINARY --benchmark-phase 13 --frames 10000

echo "[14/16] Phase 14: Save/Load..."
$BINARY --benchmark-phase 14 --iterations 100

echo "[15/16] Phase 15: Multiplayer..."
$BINARY --benchmark-phase 15 --frames 10000

echo "[16/16] Phase 16: Full Stress Test..."
$BINARY --full-stress-test --duration 60

echo "=== BENCHMARK SUITE COMPLETE ==="
