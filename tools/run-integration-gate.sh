#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 TEST_EXECUTABLE TEST_QML" >&2
    exit 2
fi

binary=$1
input=$2

# Never commandeer a developer's desktop by default. The isolated gate starts
# one ApplicationWindow per test function, so running it on the active display
# creates hundreds of windows and each focus-sensitive case may request focus.
# CI can opt into the complete focus/input suite on a virtual display; a human
# can explicitly opt into visible execution when diagnosing those cases.
if [[ ${KVIT_INTEGRATION_VISIBLE:-0} != 1 ]]; then
    export QT_QPA_PLATFORM=offscreen
fi

mapfile -t cases < <(
    "$binary" -input "$input" -functions 2>&1 \
        | sed -n 's/^IntegrationTests::\(.*\)()$/\1/p'
)
if [[ ${#cases[@]} -eq 0 ]]; then
    echo "No integration test functions were discovered" >&2
    exit 2
fi

if [[ ${KVIT_SHUFFLE_INTEGRATION:-0} == 1 ]]; then
    mapfile -t cases < <(printf '%s\n' "${cases[@]}" | shuf)
fi

failed=0
for test_case in "${cases[@]}"; do
    echo "[integration] $test_case"
    passed=0
    for attempt in 1 2 3; do
        # Give the display server a beat to retire the prior process's window.
        sleep 0.1
        if output=$("$binary" -input "$input" \
                "IntegrationTests::$test_case" 2>&1); then
            passed=1
            break
        fi
        if [[ $attempt -lt 3 ]]; then
            echo "[integration] retry $attempt: $test_case"
        fi
    done
    if [[ $passed -eq 0 ]]; then
        printf '%s\n' "$output"
        failed=1
    fi
done
exit "$failed"
