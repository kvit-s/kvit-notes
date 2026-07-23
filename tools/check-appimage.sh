#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Run the finished AppImage and prove the packaged runtime works.
#
#   tools/check-appimage.sh dist/Kvit_Notes-1.0.0-x86_64.AppImage
#
# tools/check-appdir-runtime.sh already probes the AppDir before it is
# packed. This runs the artifact users actually download, after packing, so
# anything the squashfs image, AppRun or the relocation step breaks is caught
# here rather than in a draft release. The script that builds the AppImage
# used to print `--math-selftest` as a suggestion and never run it, and CI
# uploaded the artifact without executing it once.
#
# What is verified, and how strongly:
#
#   math resources   `--math-selftest` renders a formula from the packaged
#                    math-res tree and exits. Fully behavioural.
#   QML imports      the shell loads and the event loop runs, with no QML
#                    error on stderr. Fully behavioural.
#   SQLite FTS5      the app indexes a seeded vault, and the resulting
#                    database is inspected for the FTS5 virtual tables and
#                    indexed rows. Fully behavioural: an FTS5-less SQLite
#                    driver cannot create these tables at all.
#   SVG support      the qsvg image plugin is found inside the AppImage,
#                    loads, and advertises the "svg" key. Behavioural as far
#                    as plugin loading goes; it does not assert that a
#                    particular image rasterized.
#   multimedia       the QtMultimedia QML module loads, and the ffmpeg
#                    backend plugin is present. The backend itself is loaded
#                    lazily on first playback, so its presence is checked
#                    structurally rather than by loading it.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ $# -lt 1 ]; then
    echo "usage: $0 <appimage>" >&2
    exit 2
fi
APPIMAGE=$(realpath -e "$1") || exit 2
[ -x "$APPIMAGE" ] || chmod +x "$APPIMAGE"

command -v sqlite3 > /dev/null || {
    echo "sqlite3 is required to inspect the search index" >&2
    exit 2
}

# Extract rather than mount: works without FUSE in containers and WSL, and
# keeps the run deterministic.
export APPIMAGE_EXTRACT_AND_RUN=1

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

# ── 1. Math resources, from the packed artifact.
echo "[1/4] --math-selftest"
SELFTEST=$WORK/selftest.log
if ! QT_QPA_PLATFORM=offscreen timeout 120 "$APPIMAGE" --math-selftest \
        > "$SELFTEST" 2>&1; then
    cat "$SELFTEST" >&2
    fail "--math-selftest exited non-zero"
fi
grep -q 'selftest: OK' "$SELFTEST" || { cat "$SELFTEST" >&2
    fail "--math-selftest did not report OK"; }
# APPIMAGE_EXTRACT_AND_RUN prints a line per cached file; only the app's own
# output is interesting, and the whole log is dumped anyway when a check fails.
grep -vF 'File exists and file size matches' "$SELFTEST" | sed 's/^/      /'

# ── 2. Start the shell against a seeded vault.
#
# The vault defaults to ~/Documents/Kvit, so pointing HOME at a scratch
# directory both isolates the run from the developer's real notes and gives
# the collection something to index. The note and the SVG beside it are what
# make the run exercise indexing and image handling rather than just opening
# an empty window.
echo "[2/4] launch and index a seeded vault"
VAULT=$WORK/home/Documents/Kvit
mkdir -p "$VAULT"
cat > "$VAULT/probe.svg" <<'SVG'
<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40"><rect width="40" height="40" fill="#33aa77"/></svg>
SVG
cat > "$VAULT/probe.md" <<'MD'
# Packaging probe

Indexable prose for the search index to tokenize.

![vector](probe.svg)

$E = mc^2$
MD

RUN=$WORK/run.log
set +e
HOME="$WORK/home" \
XDG_CONFIG_HOME="$WORK/home/.config" \
XDG_CACHE_HOME="$WORK/home/.cache" \
QT_QPA_PLATFORM=offscreen \
QT_QUICK_BACKEND=software \
QT_DEBUG_PLUGINS=1 \
timeout 40 "$APPIMAGE" > "$RUN" 2>&1
STATUS=$?
set -e

# 124 is `timeout` killing a process that was still running, which is the
# success case: the app started and stayed up. Exiting on its own means it
# failed to start, because nothing here asks it to quit.
if [ "$STATUS" -ne 124 ]; then
    echo "--- last 40 lines of output" >&2
    tail -40 "$RUN" >&2
    fail "the packaged app exited on its own (status $STATUS) instead of running"
fi

grep -qE 'failed to load component|is not a type|module .* not installed|plugin .* not found' "$RUN" \
    && { grep -nE 'failed to load component|is not a type|module .* not installed|plugin .* not found' "$RUN" >&2
         fail "QML load errors from the packaged app"; }

# The app warns explicitly when its SQLite driver cannot do FTS5 with the
# trigram tokenizer (AppContext::wire). Treat that warning as fatal here:
# a packaged build without it ships global search that silently does nothing.
grep -q 'Global search disabled' "$RUN" \
    && fail "the packaged SQLite driver lacks FTS5 with the trigram tokenizer"
echo "      shell started, no QML or search-capability errors"

# ── 3. The search index the run just built.
echo "[3/4] SQLite FTS5 in the packaged runtime"
DB=$(find "$WORK/home/.cache" -name '*.sqlite' -type f | head -1)
[ -n "$DB" ] || fail "no search index database was created under the scratch cache"

SCHEMA=$(sqlite3 "$DB" \
    "SELECT group_concat(name) FROM sqlite_master WHERE sql LIKE '%USING fts5%';")
case "$SCHEMA" in
    *search_words*) ;;
    *) fail "search_words FTS5 table missing from $DB (found: ${SCHEMA:-none})" ;;
esac
case "$SCHEMA" in
    *search_trigrams*) ;;
    *) fail "search_trigrams FTS5 table missing from $DB (found: $SCHEMA)" ;;
esac

ROWS=$(sqlite3 "$DB" "SELECT count(*) FROM search_words;")
[ "${ROWS:-0}" -gt 0 ] || fail "the seeded note produced no indexed rows"
echo "      FTS5 tables present, $ROWS indexed rows"

# ── 4. Image and multimedia plugins, from the same run's plugin log.
echo "[4/4] image and multimedia plugins"
grep -q 'libqsvg.so.*loaded library' "$RUN" \
    || fail "the qsvg image plugin did not load from the AppImage"
grep -qE '"svg"' "$RUN" \
    || fail "the loaded qsvg plugin does not advertise the svg key"
echo "      qsvg loaded and advertises svg"

grep -q 'libquickmultimediaplugin.so' "$RUN" \
    || fail "the QtMultimedia QML plugin was not loaded"
echo "      QtMultimedia QML plugin loaded"

# The ffmpeg backend is loaded on first playback rather than at startup, so
# this is a presence check on the extracted image.
EXTRACTED=$(grep -oE '/tmp/appimage_extracted_[a-f0-9]+' "$RUN" | head -1)
if [ -n "$EXTRACTED" ] && [ -d "$EXTRACTED" ]; then
    find "$EXTRACTED" -name 'libffmpegmediaplugin.so' | grep -q . \
        || fail "the ffmpeg multimedia backend is missing from the AppImage"
    echo "      ffmpeg multimedia backend present"
else
    echo "      note: could not locate the extracted image; ffmpeg backend" \
         "presence was checked in the AppDir before packing instead"
fi

echo "AppImage runtime checks passed: $(basename "$APPIMAGE")"
