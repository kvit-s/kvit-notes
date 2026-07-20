#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
# One-command Windows build/test from a WSL shell. The bat side always
# mirrors this tree first (win-build.bat -> tools/win-sync.sh), so there
# is no stale-code failure mode from either entry point.
#
#   tools/win.sh build      configure if needed + build
#   tools/win.sh test       full unit suite (reads win-test-result.txt)
#   tools/win.sh deploy     windeployqt staging next to the exe
#   tools/win.sh selftest   deployed --math-selftest probe
#   tools/win.sh sync       mirror only
#
# The mirror directory is named explicitly, either as the argument after
# the verb or in KVIT_WIN_ROOT; there is no default, because the sync that
# feeds it deletes whatever it finds there. Create it once with
# `tools/win-sync.sh --init DESTINATION`.
set -euo pipefail

MARKER_NAME=".kvit-notes-mirror"
MARKER_MAGIC="kvit-notes-mirror"

CMD=/mnt/c/Windows/System32/cmd.exe
verb="${1:-build}"
shift || true

case "$verb" in
sync)
    exec bash "$(dirname "$0")/win-sync.sh" "$@"
    ;;
build|configure|test|deploy|selftest) ;;
*)
    echo "usage: tools/win.sh [sync|configure|build|test|deploy|selftest] [MIRROR]" >&2
    exit 2
    ;;
esac

WIN_ROOT="${1:-${KVIT_WIN_ROOT:-}}"
if [[ -z $WIN_ROOT ]]; then
    echo "tools/win.sh: no mirror directory given." >&2
    echo "  pass it after the verb or set KVIT_WIN_ROOT; initialize a new one" >&2
    echo "  with: tools/win-sync.sh --init DESTINATION" >&2
    exit 2
fi
WIN_ROOT="$(realpath -e "$WIN_ROOT" 2>/dev/null)" \
    || { echo "tools/win.sh: mirror does not exist: ${1:-$KVIT_WIN_ROOT}" >&2; exit 1; }
if [[ ! -f $WIN_ROOT/$MARKER_NAME ]] \
    || [[ $(head -n 1 "$WIN_ROOT/$MARKER_NAME") != "$MARKER_MAGIC" ]]; then
    echo "tools/win.sh: not an initialized Kvit Notes mirror: $WIN_ROOT" >&2
    echo "  (no $MARKER_NAME marker; tools/win-sync.sh --init writes it)" >&2
    exit 1
fi

case "$verb" in
build|configure)
    cd "$WIN_ROOT" && exec "$CMD" /c win-build.bat "$verb"
    ;;
test)
    cd "$WIN_ROOT" && "$CMD" /c win-build.bat build && "$CMD" /c win-test.bat
    tail -n 3 "$WIN_ROOT/win-test-result.txt"
    ;;
deploy)
    cd "$WIN_ROOT" && "$CMD" /c win-deploy.bat
    cp /mnt/c/Qt/6.10.1/msvc2022_64/plugins/platforms/qoffscreen.dll \
       "$WIN_ROOT/build-windows-msvc-release/Release/platforms/"
    echo "deployed (incl. qoffscreen.dll for --math-selftest)"
    ;;
selftest)
    cd "$WIN_ROOT" && "$CMD" /c win-selftest.bat
    cat "$WIN_ROOT/selftest.log"
    ;;
esac
