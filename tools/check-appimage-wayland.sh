#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Start the finished AppImage as a native Wayland client under a headless
# Weston compositor. This proves more than file presence: Qt must load the
# bundled Wayland QPA plugin and its dependencies, create its shell, and stay
# in the event loop. XWayland is unavailable in this isolated compositor, so
# it cannot mask a missing Wayland deployment.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ $# -lt 1 ]; then
    echo "usage: $0 <appimage>" >&2
    exit 2
fi
APPIMAGE=$(realpath -e "$1") || exit 2
[ -x "$APPIMAGE" ] || chmod +x "$APPIMAGE"

command -v weston > /dev/null || {
    echo "weston is required for the native Wayland package check" >&2
    exit 2
}

fail() { echo "FAIL: $*" >&2; exit 1; }

WORK=$(mktemp -d)
RUNTIME_DIR=$WORK/runtime
HOME_DIR=$WORK/home
mkdir -p "$RUNTIME_DIR" "$HOME_DIR"
chmod 700 "$RUNTIME_DIR"
WESTON_LOG=$WORK/weston.log
RUN_LOG=$WORK/app.log
SOCKET=kvit-wayland
WESTON_PID=

cleanup() {
    if [ -n "$WESTON_PID" ] && kill -0 "$WESTON_PID" 2>/dev/null; then
        kill "$WESTON_PID" 2>/dev/null || true
        wait "$WESTON_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

XDG_RUNTIME_DIR="$RUNTIME_DIR" \
    weston --backend=headless-backend.so --socket="$SOCKET" \
           --idle-time=0 --log="$WESTON_LOG" &
WESTON_PID=$!

for _ in $(seq 1 100); do
    [ -S "$RUNTIME_DIR/$SOCKET" ] && break
    kill -0 "$WESTON_PID" 2>/dev/null || {
        cat "$WESTON_LOG" >&2
        fail "Weston exited before creating its Wayland socket"
    }
    sleep 0.1
done
[ -S "$RUNTIME_DIR/$SOCKET" ] || {
    cat "$WESTON_LOG" >&2
    fail "Weston did not create its Wayland socket"
}

set +e
env -u APPIMAGE_EXTRACT_AND_RUN \
    HOME="$HOME_DIR" \
    XDG_CONFIG_HOME="$HOME_DIR/.config" \
    XDG_CACHE_HOME="$HOME_DIR/.cache" \
    XDG_RUNTIME_DIR="$RUNTIME_DIR" \
    WAYLAND_DISPLAY="$SOCKET" \
    QT_QPA_PLATFORM=wayland \
    QT_QUICK_BACKEND=software \
    QT_DEBUG_PLUGINS=1 \
    timeout 40 "$APPIMAGE" > "$RUN_LOG" 2>&1
STATUS=$?
set -e

if [ "$STATUS" -ne 124 ]; then
    echo "--- application output" >&2
    tail -80 "$RUN_LOG" >&2
    echo "--- Weston output" >&2
    tail -80 "$WESTON_LOG" >&2
    fail "the native Wayland launch exited early (status $STATUS)"
fi

grep -qE 'failed to load component|is not a type|module .* not installed|plugin .* not found|Could not (find|load) the Qt platform plugin' "$RUN_LOG" \
    && { grep -nE 'failed to load component|is not a type|module .* not installed|plugin .* not found|Could not (find|load) the Qt platform plugin' "$RUN_LOG" >&2
         fail "QML or Wayland plugin errors from the packaged app"; }

grep -qE 'libqwayland-(egl|generic)\.so.*loaded library' "$RUN_LOG" \
    || { tail -80 "$RUN_LOG" >&2
         fail "Qt did not report loading a bundled Wayland QPA plugin"; }

echo "Native Wayland AppImage check passed: $(basename "$APPIMAGE")"
