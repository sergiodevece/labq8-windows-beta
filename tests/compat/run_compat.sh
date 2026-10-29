#!/bin/sh
# Audio compatibility check: sessions of an older version, rendered by that
# version and by the current sources, compared sample by sample on arm64 and
# x86_64. Everything goes to build/compat/<base>/ (git-ignored).
#
# Usage (from the repository root):
#   tests/compat/run_compat.sh e0    # E0  (dfcf25b): differences allowed only at nominal 0 dB
#   tests/compat/run_compat.sh e1a   # E1a (9095401): strict, any difference fails
#   tests/compat/run_compat.sh e1b   # E1b (1e8db0b): strict except where a shelf/cut is above its Q limit (E1b.1)
#   tests/compat/run_compat.sh e1b1  # E1b.1 (5e889ec): strict, any difference fails
#   tests/compat/run_compat.sh e2a   # E2a (111acab): strict, any difference fails
#   tests/compat/run_compat.sh       # all five
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)

run_base() {
    NAME=$1 COMMIT=$2 STRICT=$3
    WORK="$ROOT/build/compat/$NAME"
    rm -rf "$WORK/src" "$WORK/out"
    mkdir -p "$WORK/src" "$WORK/out"
    git -C "$ROOT" archive "$COMMIT" src | tar -x -C "$WORK/src"

    cmake -S "$ROOT/tests/compat" -B "$WORK/cmake" -G "Unix Makefiles" \
          -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
          -DBASE_SRC_DIR="$WORK/src/src" > /dev/null
    cmake --build "$WORK/cmake" -j8 > /dev/null

    BASE="$WORK/cmake/compat_base_artefacts/Release/compat_base"
    NEW="$WORK/cmake/compat_new_artefacts/Release/compat_new"

    for ARCH in arm64 x86_64; do
        OUT="$WORK/out/$ARCH"
        arch -$ARCH "$BASE" render "$OUT/base" "$OUT/base" "$ROOT/tests/fixtures" > /dev/null
        arch -$ARCH "$NEW" render "$OUT/base" "$OUT/new" > /dev/null
        echo ""
        echo "=== $ARCH: $NAME ($COMMIT) sessions, $NAME vs current; 12 configs per case (44.1/48/96 kHz x mono/stereo x 64/512 blocks), 2 s each"
        if ! arch -$ARCH "$NEW" compare "$OUT/base" "$OUT/new" $STRICT > "$OUT/report.txt"; then
            status=1
        fi
        grep -v "^JUCE v" "$OUT/report.txt"
    done
}

status=0
case "${1:-all}" in
    e0)  run_base e0 dfcf25b "" ;;
    e1a) run_base e1a 9095401 --strict ;;
    e1b) run_base e1b 1e8db0b "--strict --qlimit" ;;
    e1b1) run_base e1b1 5e889ec --strict ;;
    e2a) run_base e2a 111acab --strict ;;
    all) run_base e0 dfcf25b ""; run_base e1a 9095401 --strict; run_base e1b 1e8db0b "--strict --qlimit"; run_base e1b1 5e889ec --strict; run_base e2a 111acab --strict ;;
    *)   echo "usage: $0 [e0|e1a|e1b|e1b1|e2a|all]"; exit 2 ;;
esac
exit $status
