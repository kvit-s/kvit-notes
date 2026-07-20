#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Pin the Flatpak manifest to the exact commit a release tag points at.
#
#   tools/update-flatpak-commit.sh <version>          # set tag + commit
#   tools/update-flatpak-commit.sh --check <version>  # verify, do not edit
#
# <version> is the release version without the leading v (1.0.0, 1.0.0-rc1).
#
# A git tag does not pin anything: it is a mutable ref that can be moved to a
# different commit after the manifest is submitted, and Flathub would then
# build source nobody reviewed. The manifest therefore carries both, with the
# commit as the real pin, and ships with a placeholder that is not a real
# object so an unpinned manifest fails to fetch instead of building whatever
# the tag currently names.
#
# The commit is read from the local repository, so run this on the checkout
# the tag was made from, after the tag exists.
set -euo pipefail
cd "$(dirname "$0")/.."

CHECK=0
if [ "${1:-}" = "--check" ]; then
    CHECK=1
    shift
fi

if [ $# -lt 1 ]; then
    echo "usage: $0 [--check] <version>" >&2
    exit 2
fi

VERSION=${1#v}
TAG="v${VERSION}"
MANIFEST=packaging/flatpak/org.kvit.Notes.yaml
PLACEHOLDER=0000000000000000000000000000000000000000

[ -f "$MANIFEST" ] || { echo "not found: $MANIFEST" >&2; exit 2; }

COMMIT=$(git rev-parse --verify "${TAG}^{commit}" 2>/dev/null) || {
    echo "tag $TAG does not exist in this repository." >&2
    echo "Create the release tag first; the manifest pins what it points at." >&2
    exit 1
}

CUR_TAG=$(grep -oP '^\s*tag:\s*\K\S+' "$MANIFEST" || true)
CUR_COMMIT=$(grep -oP '^\s*commit:\s*"?\K[0-9a-f]{40}' "$MANIFEST" || true)

if [ "$CHECK" -eq 1 ]; then
    fail=0
    if [ "$CUR_TAG" != "$TAG" ]; then
        echo "manifest tag is '$CUR_TAG', expected '$TAG'" >&2
        fail=1
    fi
    if [ "$CUR_COMMIT" = "$PLACEHOLDER" ]; then
        echo "manifest still carries the placeholder commit - it is not pinned." >&2
        fail=1
    elif [ "$CUR_COMMIT" != "$COMMIT" ]; then
        echo "manifest commit does not match $TAG:" >&2
        echo "  manifest $CUR_COMMIT" >&2
        echo "  $TAG     $COMMIT" >&2
        fail=1
    fi
    if [ "$fail" -ne 0 ]; then
        echo "Run tools/update-flatpak-commit.sh $VERSION to fix." >&2
        exit 1
    fi
    echo "manifest pins $TAG at $COMMIT"
    exit 0
fi

sed -i \
    -e "s|^\(\s*\)tag:.*|\1tag: ${TAG}|" \
    -e "s|^\(\s*\)commit:.*|\1commit: \"${COMMIT}\"|" \
    "$MANIFEST"

echo "Pinned $MANIFEST"
echo "  tag    $TAG"
echo "  commit $COMMIT"
echo
echo "Verify with: tools/update-flatpak-commit.sh --check $VERSION"
