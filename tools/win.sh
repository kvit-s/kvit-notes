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
set -euo pipefail

WIN_ROOT="${KVIT_WIN_ROOT:-/mnt/d/projects/kvit-chat}"
CMD=/mnt/c/Windows/System32/cmd.exe
verb="${1:-build}"

case "$verb" in
sync)
    exec bash "$(dirname "$0")/win-sync.sh"
    ;;
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
*)
    echo "usage: tools/win.sh [sync|configure|build|test|deploy|selftest]" >&2
    exit 2
    ;;
esac
