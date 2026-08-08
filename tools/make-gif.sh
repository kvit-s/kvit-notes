#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Assemble the frames kvit-uidriver --record wrote into a GIF.
#
#   tools/make-gif.sh FRAME_DIR OUTPUT.gif [WIDTH] [SPEED]
#
# SPEED divides every frame's duration, so 2 plays the clip at twice the speed
# it was captured at. Pacing that reads well while watching the application
# work reads as slow in a short loop beside a paragraph of prose, and speeding
# up at assembly time avoids retuning the scenario for it.
#
# The driver names each frame with the milliseconds elapsed since capture
# began, because grabbing a frame is not free and the interval between frames
# is therefore not the interval that was asked for. This script reads those
# numbers and gives every frame the duration it actually occupied, so the GIF
# plays at the speed the scenario ran at rather than at a nominal frame rate.
# Assembling with a fixed -framerate instead silently speeds the result up
# wherever capture fell behind, which is exactly where something slow and
# interesting was happening.
#
# WIDTH defaults to 900, which keeps a 1280-wide capture legible at a few
# hundred KB for ten seconds.
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "usage: $0 FRAME_DIR OUTPUT.gif [WIDTH] [SPEED]" >&2
    exit 2
fi

frames=$1
out=$2
width=${3:-900}
speed=${4:-1}

command -v ffmpeg >/dev/null || { echo "make-gif: ffmpeg is not installed" >&2; exit 1; }
[[ -d $frames ]] || { echo "make-gif: no such directory: $frames" >&2; exit 1; }

count=$(find "$frames" -maxdepth 1 -name 'f_*.png' | wc -l)
if [[ $count -lt 2 ]]; then
    echo "make-gif: $frames holds $count frames; nothing to assemble" >&2
    echo "make-gif: did the run pass --record=$frames ?" >&2
    exit 1
fi

concat=$(mktemp)
trap 'rm -f "$concat"' EXIT

python3 - "$frames" "$concat" "$speed" <<'PY'
import os, re, sys
frames, concat = sys.argv[1], sys.argv[2]
speed = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
names = sorted(f for f in os.listdir(frames) if re.fullmatch(r'f_\d+\.png', f))
stamps = [int(re.findall(r'f_(\d+)\.png', f)[0]) for f in names]
lines = []
for i, (name, at) in enumerate(zip(names, stamps)):
    nxt = stamps[i + 1] if i + 1 < len(stamps) else at + 100
    # A floor keeps a frame pair captured back to back from asking for a
    # zero-length display, which some GIF viewers render as a stall.
    lines.append("file '%s'" % os.path.join(frames, name))
    lines.append("duration %.3f" % max(0.02, (nxt - at) / 1000.0 / speed))
lines.append("file '%s'" % os.path.join(frames, names[-1]))
open(concat, "w").write("\n".join(lines) + "\n")
span = (stamps[-1] - stamps[0]) / 1000.0
print("make-gif: %d frames spanning %.1f s, played at %gx (%.1f s)"
      % (len(names), span, speed, span / speed))
PY

# One palette for the whole clip rather than per frame: a per-frame palette
# makes flat interface colours shimmer between frames.
ffmpeg -y -loglevel error -f concat -safe 0 -i "$concat" \
    -vf "fps=10,scale=${width}:-1:flags=lanczos,split[a][b];\
[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=bayer:bayer_scale=3" \
    -loop 0 "$out"

echo "make-gif: wrote $out ($(du -h "$out" | cut -f1))"
