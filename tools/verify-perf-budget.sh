#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Prove that the CPU-time performance budgets still catch a real regression.
#
#   tools/verify-perf-budget.sh [BUILD_DIR]
#
# A budget that has been made robust against machine load has to be shown to
# still fail on genuinely slower code, otherwise "robust" just means
# "loosened". This injects a regression into a hot path, runs the budgeted
# test, and requires it to fail; then reverts and requires it to pass. Both
# directions have to hold or the script exits non-zero.
#
# The regression is a doubled per-note parse in NoteCollection::indexNote -
# the shape of a refactor that re-derives fields by indexing a second time.
# It roughly doubles the CPU cost of opening a collection, which is the kind
# of change these budgets exist to catch and the kind that a wall-clock
# assertion on a busy machine would have lost in the noise.
#
# Run it on any machine, busy or idle: the point is that the answer does not
# depend on that. The script prints the load average it ran at.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=${1:-build-linux-release}
TARGET=src/notecollection.cpp
TEST=$BUILD_DIR/tests/test_notecollection
CASE=testBenchmark500NoteOpen

if ! git diff --quiet -- "$TARGET"; then
    echo "$TARGET has uncommitted changes; refusing to patch it." >&2
    exit 2
fi
[ -d "$BUILD_DIR" ] || { echo "no build directory: $BUILD_DIR" >&2; exit 2; }

# Whatever happens, put the source back and rebuild it clean.
restore() {
    git checkout -- "$TARGET" 2>/dev/null || true
}
trap restore EXIT

build() {
    cmake --build --preset linux-release -j "$(nproc)" --target test_notecollection \
        > /tmp/perf-budget-build.log 2>&1 \
        || { echo "build failed; see /tmp/perf-budget-build.log" >&2; exit 1; }
}

# Runs the budgeted case and echoes pass/fail plus the measurement line.
run_case() {
    local out
    if out=$(QT_QPA_PLATFORM=offscreen KVIT_ENFORCE_TIMING_BUDGETS=1 \
                "$TEST" "$CASE" 2>&1); then
        echo "PASS"
    else
        echo "FAIL"
    fi
    printf '%s\n' "$out" | grep -oE 'cpu [0-9.]+ ms \(wall [0-9.]+ ms, contention [0-9.]+x\)' \
        | sed 's/^/      /' || true
}

echo "Machine load at start: $(cut -d' ' -f1-3 /proc/loadavg)"
echo

echo "[1/2] unchanged code must pass its budget"
build
verdict_clean=$(run_case)
printf '%s\n' "$verdict_clean" | sed 's/^/      /'

echo
echo "[2/2] with a doubled per-note parse, the budget must fail"
python3 - "$TARGET" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
t = p.read_text()
old = """    indexNoteFromText(relPath, fileText, info);
    const NoteEntry *entry = note(relPath);"""
new = """    indexNoteFromText(relPath, fileText, info);
    indexNoteFromText(relPath, fileText, info);   // injected regression
    const NoteEntry *entry = note(relPath);"""
if old not in t:
    sys.exit("could not find the injection point in " + str(p))
p.write_text(t.replace(old, new, 1))
PY
build
verdict_regressed=$(run_case)
printf '%s\n' "$verdict_regressed" | sed 's/^/      /'

restore
build

echo
if [[ $verdict_clean == PASS* && $verdict_regressed == FAIL* ]]; then
    echo "The budget passes unchanged code and fails a doubled parse."
    exit 0
fi
echo "VERIFICATION FAILED: expected unchanged=PASS regressed=FAIL," >&2
echo "  got unchanged=${verdict_clean%%$'\n'*} regressed=${verdict_regressed%%$'\n'*}" >&2
exit 1
