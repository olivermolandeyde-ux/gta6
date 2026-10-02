#!/bin/bash
# Final Gold Master verification

echo "=== LEONIDA ENGINE GOLD MASTER VERIFICATION ==="

PASS=0
FAIL=0

# Test 1: Binary exists and is executable
if [ -f "./build_release/leonida_engine" ]; then
    echo "✓ Binary exists"
    PASS=$((PASS + 1))
else
    echo "✗ Binary missing"
    FAIL=$((FAIL + 1))
fi

# Test 2: Data files generated
if [ -f "./release/data/city.cell" ] && [ -f "./release/data/missions/test_mission.mission" ]; then
    echo "✓ Data files present"
    PASS=$((PASS + 1))
else
    echo "✗ Data files missing"
    FAIL=$((FAIL + 1))
fi

# Test 3: Manifest generated
if [ -f "./release/manifest.json" ]; then
    echo "✓ Manifest present"
    PASS=$((PASS + 1))
else
    echo "✗ Manifest missing"
    FAIL=$((FAIL + 1))
fi

# Test 4: Checksums match
if sha256sum -c leonida_engine_v1.0.tar.gz.sha256; then
    echo "✓ Checksums valid"
    PASS=$((PASS + 1))
else
    echo "✗ Checksums invalid"
    FAIL=$((FAIL + 1))
fi

# Test 5: Run full integration test
if ./build_release/leonida_engine --full-verification; then
    echo "✓ Full integration test passed"
    PASS=$((PASS + 1))
else
    echo "✗ Full integration test failed"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "=== VERIFICATION RESULTS ==="
echo "Passed: $PASS"
echo "Failed: $FAIL"

if [ $FAIL -eq 0 ]; then
    echo "✓ GOLD MASTER CERTIFIED"
    exit 0
else
    echo "✗ VERIFICATION FAILED"
    exit 1
fi
