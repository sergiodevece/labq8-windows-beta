#!/bin/sh
# E0 <-> E1a audio compatibility check.
# Builds the E0 plugin code (exported from commit dfcf25b) and the current
# code into build/compat (git-ignored), renders every E0 session through both,
# and compares them sample by sample on each architecture.
#
# Usage: tests/compat/run_compat.sh            (from the repository root)
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK="$ROOT/build/compat"
E0_COMMIT=dfcf25b

rm -rf "$WORK/e0src" "$WORK/out"
mkdir -p "$WORK/e0src" "$WORK/out"
git -C "$ROOT" archive "$E0_COMMIT" src | tar -x -C "$WORK/e0src"

cmake -S "$ROOT/tests/compat" -B "$WORK/cmake" -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
      -DE0_SRC_DIR="$WORK/e0src/src" > /dev/null
cmake --build "$WORK/cmake" -j8 > /dev/null

E0="$WORK/cmake/compat_e0_artefacts/Release/compat_e0"
E1A="$WORK/cmake/compat_e1a_artefacts/Release/compat_e1a"

status=0
for ARCH in arm64 x86_64; do
    OUT="$WORK/out/$ARCH"
    arch -$ARCH "$E0" render "$OUT/e0" "$OUT/e0" "$ROOT/tests/fixtures"
    arch -$ARCH "$E1A" render "$OUT/e0" "$OUT/e1a"
    echo ""
    echo "=== $ARCH: E0 ($E0_COMMIT) vs E1a, 12 configs per case (44.1/48/96 kHz x mono/stereo x 64/512 blocks), 2 s each"
    arch -$ARCH "$E1A" compare "$OUT/e0" "$OUT/e1a" || status=1
done
exit $status
