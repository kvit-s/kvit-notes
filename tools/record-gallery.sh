#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Record the feature-gallery clips: one GIF per feature, from the real
# application driven by tools/uidriver.cpp against the checked-in demo vault.
#
#   tools/record-gallery.sh [SCENARIO ...]
#
# With no arguments it records every clip the gallery uses. Naming scenarios
# re-records only those, which is what iterating on one of them looks like.
#
# Everything runs unattended: frames come from the window itself through
# QQuickWindow::grabWindow rather than from a screen recorder, so there is
# nobody at a keyboard and no desktop to keep clear. The VNC platform plugin
# gives a real windowing surface with working focus that never touches the
# display, which is what the integration gate uses for the same reason.
#
# Requires a build with -DKVIT_UI_DRIVER=ON (build/kvit-uidriver) and ffmpeg.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRIVER="${KVIT_UIDRIVER:-$ROOT/build/kvit-uidriver}"
FIXTURE="$ROOT/screenshots/demo-vault"
# Notes only the gallery needs, staged on top of the demo vault. They are kept
# apart from it so that adding a clip cannot change the press stills, which are
# captured from the vault alone and are committed images.
EXTRA="$ROOT/screenshots/demo-vault-gallery"
OUT="${KVIT_GALLERY_OUT:-$ROOT/screenshots/gallery}"

# The staged vault lives at a fixed, neutral path because the status bar
# renders the open note's absolute path: a run from a home or scratch directory
# bakes a username into the pixels of every frame.
VAULT=/tmp/kvit-notes-demo/vault
SCRATCH=/tmp/kvit-gallery

# Published width and the speed every clip is assembled at. Pacing that reads
# well while watching the application work reads as slow in a short loop beside
# a paragraph of prose, and this is an assembly-time multiplier, so changing it
# costs one re-assembly rather than a retune of every dwell in every scenario.
WIDTH=900
SPEED=2

ALL=(mermaid livepreview math astext query palette tables kanban wikilinks
     search theme export singlefile)

if [ ! -x "$DRIVER" ]; then
    echo "error: $DRIVER not found; configure with -DKVIT_UI_DRIVER=ON and" \
         "build the kvit-uidriver target first." >&2
    exit 1
fi
command -v ffmpeg >/dev/null || { echo "error: ffmpeg is not installed" >&2; exit 1; }

wanted=("$@")
[ ${#wanted[@]} -eq 0 ] && wanted=("${ALL[@]}")

mkdir -p "$OUT"

# A fresh vault for every clip. The scenarios type into notes and save them,
# and tour-query rewrites a project's front matter from another process, so a
# second clip run over the leavings of the first is not the note the scenario
# was written against.
stage_vault() {
    rm -rf /tmp/kvit-notes-demo
    mkdir -p "$VAULT"
    cp -a "$FIXTURE/." "$VAULT/"
    cp -a "$EXTRA/." "$VAULT/"
}

record() { # <scenario-suffix>
    local name=$1
    local frames="$SCRATCH/frames-$name"
    local home="$SCRATCH/home-$name"

    stage_vault
    rm -rf "$frames" "$home"
    mkdir -p "$frames" "$home/.config" "$home/.cache" "$home/.local/share"

    # tour-singlefile is started on a loose file rather than on a vault: that
    # is the mode it exists to show, and it is chosen by the startup argument.
    local target="$VAULT"
    [ "$name" = singlefile ] && target="$VAULT/Welcome.md"

    # Run from the staged vault's parent. A file chooser opens on the working
    # directory, so a run started in a checkout puts that checkout's path —
    # and the username in it — into the frames of the export clip.
    echo "== recording $name"
    ( cd "$(dirname "$VAULT")" &&
      HOME="$home" XDG_CONFIG_HOME="$home/.config" \
      XDG_CACHE_HOME="$home/.cache" XDG_DATA_HOME="$home/.local/share" \
      QT_QPA_PLATFORM=vnc \
        "$DRIVER" --scenario="tour-$name" --vault="$target" \
                  --size=1280x800 --out="$SCRATCH" \
                  --record="$frames" --fps=10 )

    "$ROOT/tools/make-gif.sh" "$frames" "$OUT/$name.gif" "$WIDTH" "$SPEED"
}

for name in "${wanted[@]}"; do
    record "$name"
done

echo
echo "clips in $OUT:"
ls -lh "$OUT"/*.gif | awk '{print "  " $9 "  " $5}'
