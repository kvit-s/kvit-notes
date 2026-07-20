#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
# Mirror the canonical WSL tree to a Windows build tree. One-way and
# delete-propagating: the mirror is disposable and must never be edited
# directly. Called by win-build.bat before every Windows build (via
# wsl.exe) and usable standalone.
#
#   tools/win-sync.sh --init /mnt/d/projects/kvit-notes   # first time
#   tools/win-sync.sh /mnt/d/projects/kvit-notes          # every time after
#   KVIT_WIN_ROOT=/mnt/d/projects/kvit-notes tools/win-sync.sh
#
# Because rsync --delete removes everything in the destination that is not
# in this tree, the destination is never assumed. It must be named on the
# command line or in KVIT_WIN_ROOT, it must survive the safety checks
# below, and it must carry the .kvit-notes-mirror marker that --init
# writes. --init refuses any directory that already holds unrelated files,
# so a mistyped path cannot turn into a recursive delete of someone's
# checkout.
set -euo pipefail

MARKER_NAME=".kvit-notes-mirror"
MARKER_MAGIC="kvit-notes-mirror"

die() { echo "win-sync: $*" >&2; exit 1; }

usage() {
    cat >&2 <<'EOF'
usage: tools/win-sync.sh [--init] DESTINATION
       KVIT_WIN_ROOT=DESTINATION tools/win-sync.sh [--init]

  --init   create/adopt DESTINATION as a Kvit Notes mirror, then sync.
           Refused if DESTINATION already contains unrelated files.

There is no default destination: the sync propagates deletions.
EOF
    exit 2
}

init=0
dst_arg=""
while [[ $# -gt 0 ]]; do
    case "$1" in
    --init) init=1 ;;
    -h|--help) usage ;;
    -*) die "unknown option: $1" ;;
    *)
        [[ -n $dst_arg ]] && die "more than one destination given"
        dst_arg="$1"
        ;;
    esac
    shift
done

[[ -z $dst_arg ]] && dst_arg="${KVIT_WIN_ROOT:-}"
[[ -z $dst_arg ]] && usage

src="${KVIT_WSL_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
src="$(realpath -e "$src" 2>/dev/null)" || die "source does not exist: $src"
grep -q '^project(kvit-notes' "$src/CMakeLists.txt" 2>/dev/null \
    || die "source is not a Kvit Notes checkout: $src"

# The destination need not exist yet under --init, so resolve without
# requiring it; -m still collapses .., symlinked parents and trailing slashes,
# which is what the containment checks below have to compare.
dst="$(realpath -m "$dst_arg")" || die "cannot resolve destination: $dst_arg"

# Refuse paths broad enough that a delete-propagating sync would take out
# more than a project directory. Under a mount point (/mnt/d, /media/usb)
# the mount itself is not a directory anyone chose, so require two real
# components below it rather than counting from the filesystem root.
depth=0
while IFS= read -r component; do
    [[ -n $component ]] && depth=$((depth + 1))
done < <(tr '/' '\n' <<<"${dst#/}")
min_depth=3
[[ $dst =~ ^/(mnt|media)/ ]] && min_depth=4
[[ $dst == / ]] && die "refusing to sync to the filesystem root"
[[ $depth -lt $min_depth ]] && die \
    "destination is too broad for a delete-propagating sync: $dst"

[[ $dst == "$src" ]] && die "destination is the source tree: $dst"
[[ $src == "$dst"/* ]] && die "destination contains the source tree: $dst"
[[ $dst == "$src"/* ]] && die "destination is inside the source tree: $dst"

marker="$dst/$MARKER_NAME"
marker_ok=0
if [[ -f $marker ]] && [[ $(head -n 1 "$marker") == "$MARKER_MAGIC" ]]; then
    marker_ok=1
fi

if [[ $marker_ok -eq 0 ]]; then
    [[ $init -eq 0 ]] && die "$(cat <<EOF
destination is not an initialized Kvit Notes mirror: $dst
  (no $MARKER_NAME marker). If this really is the disposable Windows
  mirror, run: tools/win-sync.sh --init "$dst"
EOF
)"
    # --init adopts an empty or new directory only. Anything already living
    # there is the case this guard exists for: an unrelated checkout that
    # --delete would erase.
    if [[ -e $dst ]]; then
        [[ -d $dst ]] || die "destination exists and is not a directory: $dst"
        [[ -e $dst/.git ]] && die "destination is a git checkout, not a mirror: $dst"
        if [[ -n $(ls -A "$dst") ]]; then
            die "$(cat <<EOF
refusing to initialize a mirror over a non-empty directory: $dst
  Contents would be deleted by the first delete-propagating sync.
  Point --init at a new or empty directory.
EOF
)"
        fi
    fi
    mkdir -p "$dst"
    cat > "$marker" <<EOF
$MARKER_MAGIC
# Disposable one-way mirror of a Kvit Notes WSL checkout. Never edit files
# here: tools/win-sync.sh overwrites and deletes them on every sync.
source=$src
initialized=$(date -Iseconds)
EOF
    echo "win-sync: initialized mirror $dst"
else
    recorded="$(sed -n 's/^source=//p' "$marker" | head -n 1)"
    if [[ -n $recorded && $recorded != "$src" ]]; then
        echo "win-sync: warning: mirror was initialized from $recorded" >&2
    fi
fi

echo "win-sync: source      $src"
echo "win-sync: destination $dst"
echo "win-sync: deletions propagate - files in the destination that are not"
echo "win-sync: in the source tree will be removed"

rsync -a --delete \
    --exclude '.git/' \
    --exclude 'build*/' \
    --exclude 'tests/screenshots/' \
    --exclude 'win-test-result.txt' \
    --exclude 'ctest-one.txt' \
    --exclude 'selftest.log' \
    --exclude 'windeploy.log' \
    --exclude "/$MARKER_NAME" \
    "$src/" "$dst/"
echo "win-sync: mirrored $src -> $dst"
