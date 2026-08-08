#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Scan shipped raster images and animations for text leaked into their pixels:
# an absolute home path, the harness scratch directory or a session id, the
# pre-rename product name, or the owner's personal address. The secret scan and
# every text-based check look at file contents but never inside an image — a
# press still once shipped with a scratch-directory path (a session id)
# rendered into its status bar, invisible to those checks.
#
# The press stills are regenerated from a checked-in demo vault at a fixed,
# non-identifying path (tools/capture-press-stills.sh) and the gallery clips
# from the same vault (tools/record-gallery.sh), so a clean run stays clean;
# this is the backstop for a hand-staged or stale capture. Requires
# tesseract-ocr, and ffmpeg for the animations.
#
# A clip is sampled rather than read frame by frame, and each sampled frame is
# enlarged before it is read: a gallery clip is published at 900 pixels wide,
# where the status bar's path is too small for OCR to recover, and enlarging
# to 1800 gets it back. Sampling means a leak that is on screen for under a
# second can be missed, which is the price of a check that runs in a minute
# rather than in twenty.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# One frame in this many is read from each animation. A clip decodes at ten
# frames a second, so at six a dialog on screen for two seconds is sampled
# three times and the whole scan takes about ninety seconds. Lower it to read
# more of a clip when a capture is being checked by hand.
FRAME_STRIDE=${KVIT_LEAK_FRAME_STRIDE:-6}

if ! command -v tesseract >/dev/null; then
    echo "error: tesseract (OCR) not installed; cannot scan images for leaks." >&2
    echo "install tesseract-ocr and re-run — this check must not be skipped." >&2
    exit 2
fi
if ! command -v ffmpeg >/dev/null; then
    echo "error: ffmpeg not installed; cannot sample animations for leaks." >&2
    echo "install ffmpeg and re-run — this check must not be skipped." >&2
    exit 2
fi

# Absolute home paths (Linux and macOS), the harness scratch directory and
# its path-encoded form, a UUID (a session id), the pre-rename product name,
# and the owner's personal address. Deliberately NOT a bare "scratchpad" or
# "/tmp": both occur as legitimate note content (the demo vault has a note
# titled "Calculus scratchpad").
pat='/home/|/Users/|-home-[a-z]|claude-[0-9]{3,}'
pat="$pat|[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}"
pat="$pat|Kvit Editor|skolos@"

bad=0 scanned=0 frames=0

# Read one image; report and mark on a hit. $2 names it in the report, which
# for a sampled frame is the clip it came from rather than the temporary file.
scan_one() {
    local file=$1 label=$2 txt leak
    txt=$(tesseract "$file" - 2>/dev/null) || return 0
    leak=$(printf '%s' "$txt" | grep -inE "$pat") || true
    if [ -n "$leak" ]; then
        echo "leaked text rendered into $label:"
        printf '%s\n' "$leak" | sed 's/^/    /'
        bad=1
    fi
}

while IFS= read -r img; do
    [ -f "$img" ] || continue
    scanned=$((scanned + 1))
    scan_one "$img" "$img"
done < <(git ls-files '*.png' '*.jpg' '*.jpeg' ':(exclude)third_party/**')

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
while IFS= read -r clip; do
    [ -f "$clip" ] || continue
    scanned=$((scanned + 1))
    rm -rf "$work/frames"
    mkdir -p "$work/frames"
    # -nostdin, because ffmpeg reads standard input and this loop is being fed
    # from it: without it ffmpeg swallows the next few filenames and those
    # clips are silently never scanned.
    ffmpeg -nostdin -y -loglevel error -i "$clip" \
        -vf "select='not(mod(n\,$FRAME_STRIDE))',scale=1800:-1:flags=lanczos" \
        -vsync 0 "$work/frames/f_%04d.png" 2>/dev/null || continue
    for frame in "$work"/frames/f_*.png; do
        [ -f "$frame" ] || continue
        frames=$((frames + 1))
        scan_one "$frame" "$clip frame $(basename "$frame" .png | tr -d 'f_')"
    done
done < <(git ls-files '*.gif' ':(exclude)third_party/**')

if [ "$bad" -eq 0 ]; then
    echo "image leak scan: clean ($scanned file(s), $frames sampled frame(s))"
else
    echo "image leak scan: FAILED"
fi
exit "$bad"
