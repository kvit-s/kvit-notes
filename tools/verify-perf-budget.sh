#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Prove that the CPU-time performance budgets still catch a real regression.
#
#   tools/verify-perf-budget.sh [BUILD_DIR]
#
# A budget made robust against machine load has to be shown to still fail on
# genuinely slower code, otherwise "robust" just means "loosened". For each
# budget below this injects a plausible slowdown into the code it covers,
# requires the test to fail, reverts, and requires it to pass. Every case must
# behave that way or the script exits non-zero.
#
# The two cases are the two CPU budgets sitting in the blocking `unit` gate:
# a doubled per-note parse in the collection open, and extra work on the
# search query path. Both are the shape of an ordinary refactor mistake
# rather than an obvious sleep.
#
# Budgets are forced with KVIT_ENFORCE_TIMING_BUDGETS=1 so the answer does not
# depend on how busy the machine running this happens to be. That is the point
# of the exercise: the verdict should be a property of the code.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=${1:-build-linux-release}

# file|test binary|test function|description
CASES=(
    "src/notecollection.cpp|test_notecollection|testBenchmark500NoteOpen|collection open, doubled per-note parse"
    "src/searchindexdb.cpp|test_searchindexdb|testQueryPerformanceGate|search query, verification in a nested loop"
    "src/querydata.cpp|test_querydata|testEvaluate1000NoteBudget|query evaluate, sort keys recomputed in the comparator"
)

if ! git diff --quiet -- src/; then
    echo "src/ has uncommitted changes; refusing to patch it." >&2
    exit 2
fi
[ -d "$BUILD_DIR" ] || { echo "no build directory: $BUILD_DIR" >&2; exit 2; }

restore() { git checkout -- src/ 2>/dev/null || true; }
trap restore EXIT

build() {
    cmake --build --preset linux-release -j "$(nproc)" --target "$1" \
        > /tmp/perf-budget-build.log 2>&1 \
        || { echo "build of $1 failed; see /tmp/perf-budget-build.log" >&2
             exit 1; }
}

# Echoes PASS or FAIL, then the measurement line.
run_case() {
    local binary=$1 fn=$2 out
    if out=$(QT_QPA_PLATFORM=offscreen KVIT_ENFORCE_TIMING_BUDGETS=1 \
                "$BUILD_DIR/tests/$binary" "$fn" 2>&1); then
        echo "PASS"
    else
        echo "FAIL"
    fi
    printf '%s\n' "$out" | grep -oE 'PERF .*cpu [0-9.]+ ms' | tail -1
}

echo "Machine load at start: $(cut -d' ' -f1-3 /proc/loadavg)"
echo

failures=0
for spec in "${CASES[@]}"; do
    IFS='|' read -r file binary fn description <<< "$spec"
    echo "-- $description"

    build "$binary"
    mapfile -t clean < <(run_case "$binary" "$fn")
    echo "   unchanged:  ${clean[0]}   ${clean[1]:-}"

    python3 tools/inject-perf-regression.py "$file"
    build "$binary"
    mapfile -t regressed < <(run_case "$binary" "$fn")
    echo "   regressed:  ${regressed[0]}   ${regressed[1]:-}"

    git checkout -- "$file"

    if [[ ${clean[0]} == PASS && ${regressed[0]} == FAIL ]]; then
        echo "   ok: passes unchanged code, fails the regression"
    else
        echo "   WRONG: expected unchanged=PASS regressed=FAIL" >&2
        failures=$((failures + 1))
    fi
    echo
done

# Leave the tree built from unmodified sources.
restore
for spec in "${CASES[@]}"; do
    IFS='|' read -r _ binary _ _ <<< "$spec"
    build "$binary"
done

if [ "$failures" -eq 0 ]; then
    echo "Every budget under test still catches its regression."
    exit 0
fi
echo "$failures budget(s) did not behave as required." >&2
exit 1
