#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Verify that a packaged AppDir can actually start its user interface.
#
#   tools/check-appdir-runtime.sh <appdir>
#
# Why this exists: linuxdeploy-plugin-qt deploys QML modules only for QML it
# is pointed at. This app compiles its QML into resources.qrc, so there is no
# QML directory for the plugin to discover on its own, and without
# QML_SOURCES_PATHS it deployed no QML modules at all. The resulting AppImage
# contained the executable, the Qt libraries and the math resources, passed
# `--math-selftest` — which never constructs a QML engine — and then failed at
# launch with:
#
#   qrc:/qml/main.qml:5:1: module "QtQuick.Controls" plugin
#   "qtquickcontrols2plugin" not found
#
# Every check that existed looked at files rather than behaviour, so nothing
# caught it. This script closes that gap in two ways: it asserts that each QML
# module the shell imports is present, and it starts the packaged binary
# headless to confirm the engine loads main.qml.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ $# -lt 1 ]; then
    echo "usage: $0 <appdir>" >&2
    exit 2
fi
APPDIR=$1

if [ ! -d "$APPDIR" ]; then
    echo "not a directory: $APPDIR" >&2
    exit 2
fi

# ── QML modules the shell imports.
#
# Derived from the `import` lines in qml/*.qml; Kvit is the app's own C++
# module, registered at runtime rather than deployed, so it is not listed.
MODULES=(
    QtQuick
    QtQuick/Controls
    QtQuick/Dialogs
    QtQuick/Effects
    QtQuick/Layouts
    QtQuick/Window
    QtMultimedia
    Qt/labs/qmlmodels
)

echo "QML module check: $APPDIR/usr/qml"
MISSING=0
for m in "${MODULES[@]}"; do
    if [ -d "$APPDIR/usr/qml/$m" ]; then
        echo "  ok  $m"
    else
        echo "MISSING QML module: $m" >&2
        MISSING=1
    fi
done
if [ "$MISSING" -ne 0 ]; then
    echo "The AppDir cannot load its shell. Is QML_SOURCES_PATHS set before" \
         "linuxdeploy runs?" >&2
    exit 1
fi

# ── Launch check.
#
# The binary has no "load the shell and exit" mode, so it is started headless
# and killed after a few seconds. A QML failure is fast and loud: the engine
# reports it and the process exits immediately. Surviving the timeout with
# nothing on stderr therefore means main.qml loaded and the event loop ran.
echo "Launch check (offscreen)"
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

set +e
(
    cd "$APPDIR"
    QT_QPA_PLATFORM=offscreen \
    QT_QUICK_BACKEND=software \
    HOME=$(mktemp -d) \
    timeout 25 ./AppRun
) > "$LOG" 2>&1
STATUS=$?
set -e

# 124 is `timeout` killing a process that was still running - the success
# case here. Anything else means the app exited on its own, which at this
# point can only be a failure to start.
if [ "$STATUS" -ne 124 ]; then
    echo "The packaged app exited on its own (status $STATUS) instead of" \
         "running. Output:" >&2
    cat "$LOG" >&2
    exit 1
fi

if grep -qE 'failed to load component|is not a type|not installed|plugin .* not found' "$LOG"; then
    echo "QML load errors from the packaged app:" >&2
    cat "$LOG" >&2
    exit 1
fi

echo "Launch check passed: the shell loaded and the event loop ran."
