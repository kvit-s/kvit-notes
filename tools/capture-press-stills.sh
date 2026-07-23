#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Regenerate the four press/README stills under screenshots/press/ from the
# checked-in demo vault (screenshots/demo-vault/), driving the real shell
# through tools/uidriver.cpp.
#
# Why this exists: the original stills were captured from a demo vault that
# lived in a throwaway scratch directory and was never checked in, so they
# could not be reproduced, and one of them leaked that directory's absolute
# path (a session id) in the status bar. Everything the captures depend on is
# now in the tree, and the vault is staged at a fixed, non-identifying path so
# the status-bar path carries no username or session id.
#
# Requires a build with -DKVIT_UI_DRIVER=ON (build/kvit-uidriver). The driver
# pins GALLIUM_DRIVER=llvmpipe under WSL itself (the WSLg GPU-GL path corrupts
# Qt text rendering); grabbing the real shell window is the only way to see
# the true text rendering, since every headless test renders on CPU.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRIVER="${KVIT_UIDRIVER:-$ROOT/build/kvit-uidriver}"
FIXTURE="$ROOT/screenshots/demo-vault"
OUT="$ROOT/screenshots/press"

if [ ! -x "$DRIVER" ]; then
    echo "error: $DRIVER not found; configure with -DKVIT_UI_DRIVER=ON and" \
         "build the kvit-uidriver target first." >&2
    exit 1
fi

# Stage the vault at a clean, fixed path so the footer path in the captures
# reads plainly and reproducibly, and isolate HOME/XDG so the run never
# touches the user's real notes, settings, or search-index cache. The staging
# copy also absorbs any sidecar/index files the app writes, keeping the
# committed fixture pristine.
VAULT="/tmp/kvit-notes-demo"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE" "$VAULT"' EXIT
rm -rf "$VAULT"
mkdir -p "$VAULT"
cp -a "$FIXTURE/." "$VAULT/"

export HOME="$STAGE/home"
export XDG_CONFIG_HOME="$STAGE/config"
export XDG_CACHE_HOME="$STAGE/cache"
export XDG_DATA_HOME="$STAGE/data"
mkdir -p "$HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_DATA_HOME"

# Give the notes staggered modification times so the "Modified" sidebar order
# is deterministic (last touched sorts to the top) rather than dependent on
# copy order.
for n in "Calculus.md" "Welcome.md" "projects/Alpha.md" "projects/Beta.md" \
         "projects/Delta.md" "projects/Gamma.md" "Project board.md" \
         "Release pipeline.md"; do
    touch "$VAULT/$n"
    sleep 0.05
done

mkdir -p "$OUT"

shot() { # <output-name> <note-path-relative-to-vault>
    echo "capturing $1 ($2)"
    "$DRIVER" --scenario=still --vault="$VAULT" --note="$VAULT/$2" \
              --name="$1" --size=1280x800 --out="$OUT"
}

shot hero-shell        "Welcome.md"
shot math              "Calculus.md"
shot mermaid-flowchart "Release pipeline.md"
shot query-block       "Project board.md"

echo "wrote stills to $OUT"
