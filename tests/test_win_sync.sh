#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
# Guard rails on tools/win-sync.sh. The sync runs rsync --delete against a
# directory named by the caller, so the cases that matter are the refusals:
# every path that could point at something other than the disposable
# Windows mirror must stop before a single file is removed.
#
#   tests/test_win_sync.sh        # standalone
#   ctest -R WinSyncGuards        # in the normal gate
set -uo pipefail

SYNC="$(cd "$(dirname "${BASH_SOURCE[0]}")/../tools" && pwd)/win-sync.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

failures=0
pass() { echo "ok   - $1"; }
fail() { echo "FAIL - $1"; failures=$((failures + 1)); }

# A minimal stand-in for the checkout, so the test never rsyncs the real
# tree. win-sync.sh identifies its source by the project() line.
SRC="$WORK/src"
mkdir -p "$SRC/src"
echo 'project(kvit-notes VERSION 1.0.0 LANGUAGES CXX)' > "$SRC/CMakeLists.txt"
echo 'int main() { return 0; }' > "$SRC/src/main.cpp"

# Runs win-sync.sh with the fake source and no inherited destination.
run_sync() {
    env -u KVIT_WIN_ROOT KVIT_WSL_ROOT="$SRC" bash "$SYNC" "$@" >"$WORK/out" 2>&1
}

refuses() {
    local desc="$1"; shift
    if run_sync "$@"; then
        fail "$desc (sync succeeded)"
        return
    fi
    pass "$desc"
}

refuses "missing destination is refused"
refuses "filesystem root is refused" /
refuses "broad mount path is refused" /mnt/z/projects
refuses "source as its own destination is refused" "$SRC"
refuses "destination containing the source is refused" "$WORK"
refuses "destination inside the source is refused" "$SRC/mirror"

# The C1 case: an unrelated project directory at a mistyped path.
OTHER="$WORK/mnt/d/projects/other-project"
mkdir -p "$OTHER/src"
echo 'uncommitted work' > "$OTHER/src/notes.txt"
refuses "uninitialized destination is refused" "$OTHER"
refuses "--init over a non-empty directory is refused" --init "$OTHER"
if [[ -f $OTHER/src/notes.txt ]]; then
    pass "refused sync left the unrelated directory untouched"
else
    fail "refused sync deleted files in the unrelated directory"
fi

CHECKOUT="$WORK/mnt/d/projects/some-checkout"
mkdir -p "$CHECKOUT/.git"
refuses "--init over a git checkout is refused" --init "$CHECKOUT"

MISSING="$WORK/mnt/d/projects/never-created"
refuses "destination that does not exist is refused without --init" "$MISSING"
if [[ -e $MISSING ]]; then
    fail "refused sync created the destination directory"
else
    pass "refused sync created nothing"
fi

# The legitimate workflow: initialize once, then sync in one command.
MIRROR="$WORK/mnt/d/projects/kvit-notes"
mkdir -p "$MIRROR"
if run_sync --init "$MIRROR" \
    && [[ $(head -n 1 "$MIRROR/.kvit-notes-mirror") == "kvit-notes-mirror" ]] \
    && [[ -f $MIRROR/src/main.cpp ]]; then
    pass "--init on an empty directory initializes and mirrors"
else
    fail "--init on an empty directory: $(cat "$WORK/out")"
fi

echo 'stale' > "$MIRROR/src/removed.cpp"
if run_sync "$MIRROR" \
    && [[ ! -f $MIRROR/src/removed.cpp ]] \
    && [[ -f $MIRROR/.kvit-notes-mirror ]]; then
    pass "initialized mirror syncs, propagates deletion, keeps its marker"
else
    fail "second sync of an initialized mirror: $(cat "$WORK/out")"
fi

if env -u KVIT_WIN_ROOT KVIT_WSL_ROOT="$SRC" \
    KVIT_WIN_ROOT="$MIRROR" bash "$SYNC" >/dev/null 2>&1; then
    pass "KVIT_WIN_ROOT names the destination when no argument is given"
else
    fail "KVIT_WIN_ROOT was not honoured"
fi

if run_sync "$MIRROR" && grep -q 'deletions propagate' "$WORK/out" \
    && grep -q "destination $MIRROR" "$WORK/out"; then
    pass "the resolved destination and the deletion warning are printed"
else
    fail "sync did not report its resolved destination"
fi

if [[ $failures -eq 0 ]]; then
    echo "test_win_sync: all checks passed"
    exit 0
fi
echo "test_win_sync: $failures check(s) failed" >&2
exit 1
