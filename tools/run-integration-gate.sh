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
#
# The default is Qt's VNC platform plugin rather than the offscreen one. Both
# stay off the desktop, but offscreen is not a display: tst_integration.qml
# treats `Qt.platform.pluginName === "offscreen"` as headless and skips 252 of
# its 351 cases, so this gate used to exercise under a third of what it named.
# The VNC plugin is a real windowing surface with working focus and input, it
# skips nothing, and nothing has to connect to the port for the run to
# proceed. A human diagnosing a focus case can still opt into the desktop.
use_vnc=0
if [[ -n ${KVIT_QML_TEST_PLATFORM:-} ]]; then
    export QT_QPA_PLATFORM=$KVIT_QML_TEST_PLATFORM
elif [[ ${KVIT_INTEGRATION_VISIBLE:-0} != 1 ]]; then
    use_vnc=1
    export QT_QPA_PLATFORM="vnc:size=1600x1200"
fi

# read -r in a loop rather than mapfile, which macOS's bash 3.2 does not have.
cases=()
if ! discovery_output=$("$binary" -input "$input" -functions 2>&1); then
    printf '%s\n' "$discovery_output" >&2
    exit 2
fi
while IFS= read -r case_name; do cases+=("$case_name"); done < <(
    printf '%s\n' "$discovery_output" \
        | sed -n 's/^\([^ ][^ ]*::.*\)()$/\1/p'
)
if [[ ${#cases[@]} -eq 0 ]]; then
    echo "No integration test functions were discovered" >&2
    exit 2
fi

if [[ ${KVIT_SHUFFLE_INTEGRATION:-0} == 1 ]]; then
    mapfile -t cases < <(printf '%s\n' "${cases[@]}" | shuf)
fi

# Each case gets its own VNC port. The processes are sequential, so one port
# would nearly always do, but a socket still in TIME_WAIT from the previous
# case refuses the bind and fails a test that has nothing wrong with it.
vnc_port=5900
next_platform() {
    if [[ $use_vnc != 1 ]]; then
        return
    fi
    vnc_port=$((vnc_port + 1))
    [[ $vnc_port -gt 5999 ]] && vnc_port=5900
    export QT_QPA_PLATFORM="vnc:size=1600x1200:port=$vnc_port"
}

# One case must not be able to stall the whole gate. A case that takes more
# than a few seconds has hung rather than slowed down: the whole file runs in
# about seven minutes, and the slowest single case is under two seconds. The
# hang seen in practice is a VNC port that a previous process has not let go
# of, where the server neither binds nor gives up. Without a bound the run
# stops at that case and CTest eventually kills it with nothing to read.
# `timeout` is coreutils and is not on a stock macOS, so its absence degrades
# to the old behaviour rather than failing the run.
run_case() {
    if command -v timeout >/dev/null 2>&1; then
        timeout "${KVIT_INTEGRATION_CASE_TIMEOUT:-60}" \
            "$binary" -input "$input" "$1" 2>&1
    else
        "$binary" -input "$input" "$1" 2>&1
    fi
}

failed=0
for test_case in "${cases[@]}"; do
    echo "[integration] ${test_case#*::}"
    passed=0
    for attempt in 1 2 3; do
        # Give the display server a beat to retire the prior process's window.
        sleep 0.1
        next_platform
        if output=$(run_case "$test_case"); then
            passed=1
            break
        fi
        if [[ $attempt -lt 3 ]]; then
            echo "[integration] retry $attempt: ${test_case#*::}"
        fi
    done
    if [[ $passed -eq 0 ]]; then
        printf '%s\n' "$output"
        failed=1
    fi
done
exit "$failed"
