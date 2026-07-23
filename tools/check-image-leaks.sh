#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Scan shipped raster images for text leaked into their pixels: an absolute
# home path, the harness scratch directory or a session id, the pre-rename
# product name, or the owner's personal address. The secret scan and every
# text-based check look at file contents but never inside an image — a press
# still once shipped with a scratch-directory path (a session id) rendered
# into its status bar, invisible to those checks.
#
# The press stills are regenerated from a checked-in demo vault at a fixed,
# non-identifying path (tools/capture-press-stills.sh), so a clean run stays
# clean; this is the backstop for a hand-staged or stale capture. Requires
# tesseract-ocr.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v tesseract >/dev/null; then
    echo "error: tesseract (OCR) not installed; cannot scan images for leaks." >&2
    echo "install tesseract-ocr and re-run — this check must not be skipped." >&2
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

bad=0 scanned=0
while IFS= read -r img; do
    [ -f "$img" ] || continue
    scanned=$((scanned + 1))
    txt=$(tesseract "$img" - 2>/dev/null) || continue
    leak=$(printf '%s' "$txt" | grep -inE "$pat") || true
    if [ -n "$leak" ]; then
        echo "leaked text rendered into $img:"
        printf '%s\n' "$leak" | sed 's/^/    /'
        bad=1
    fi
done < <(git ls-files '*.png' '*.jpg' '*.jpeg' ':(exclude)third_party/**')

if [ "$bad" -eq 0 ]; then
    echo "image leak scan: clean ($scanned image(s))"
else
    echo "image leak scan: FAILED"
fi
exit "$bad"
