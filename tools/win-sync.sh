#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
# Mirror the canonical WSL tree to the Windows build tree
# (D:\projects\kvit-chat). One-way, delete-propagating: the mirror is
# disposable and must never be edited directly. Called by win-build.bat
# before every Windows build (via wsl.exe) and usable standalone.
set -euo pipefail

SRC="${KVIT_WSL_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
DST="${KVIT_WIN_ROOT:-/mnt/d/projects/kvit-chat}"

mkdir -p "$DST"
rsync -a --delete \
    --exclude '.git/' \
    --exclude 'build*/' \
    --exclude 'tests/screenshots/' \
    --exclude 'win-test-result.txt' \
    --exclude 'ctest-one.txt' \
    --exclude 'selftest.log' \
    --exclude 'windeploy.log' \
    "$SRC/" "$DST/"
echo "win-sync: mirrored $SRC -> $DST"
