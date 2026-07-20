#!/bin/bash
# Regenerate every icon artifact from kvit.svg. The generated files are
# committed (CI must not need rsvg-convert); rerun this after editing the SVG.
#   requires: rsvg-convert, python3
set -e
cd "$(dirname "$0")"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

for size in 16 24 32 48 64 128 256 512 1024; do
    rsvg-convert -w "$size" -h "$size" kvit.svg -o "$TMP/kvit-$size.png"
done

# Linux hicolor theme set (installed by CMake).
for size in 16 24 32 48 64 128 256 512; do
    mkdir -p "hicolor/${size}x${size}/apps"
    cp "$TMP/kvit-$size.png" "hicolor/${size}x${size}/apps/kvit-notes.png"
done

# Windows .ico and macOS .icns containers.
python3 pack_icons.py "$TMP" .

echo "icons regenerated"
