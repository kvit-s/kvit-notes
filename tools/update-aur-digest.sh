#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Pin the AUR -bin package to the exact bytes of a release AppImage.
#
#   tools/update-aur-digest.sh <version> [SHA256SUMS.txt]
#
# <version> is the release version without the leading v (1.0.0, 1.0.0-rc1).
# The second argument is the release's SHA256SUMS.txt; when omitted the script
# downloads it from the GitHub release for that version.
#
# The PKGBUILD shipped `sha256sums=('SKIP')`, which tells makepkg to accept
# whatever bytes the download URL returns. A GitHub release asset can be
# replaced after publication, so SKIP means every AUR user installs whatever
# is at that URL at install time, with no way to notice a substitution. This
# script replaces SKIP with the digest recorded at release time, and running
# it is a step of the release procedure (docs/release-rollback.md).
#
# --check verifies the PKGBUILD already matches without editing it, which is
# what CI or a pre-publish review should run.
set -euo pipefail
cd "$(dirname "$0")/.."

CHECK=0
if [ "${1:-}" = "--check" ]; then
    CHECK=1
    shift
fi

if [ $# -lt 1 ]; then
    echo "usage: $0 [--check] <version> [SHA256SUMS.txt]" >&2
    exit 2
fi

VERSION=${1#v}
SUMS=${2:-}
PKGBUILD=packaging/aur/kvit-notes-bin/PKGBUILD
ASSET="Kvit_Editor-${VERSION}-x86_64.AppImage"

if [ ! -f "$PKGBUILD" ]; then
    echo "not found: $PKGBUILD" >&2
    exit 2
fi

TMP=""
if [ -z "$SUMS" ]; then
    TMP=$(mktemp)
    trap 'rm -f "$TMP"' EXIT
    URL="https://github.com/kvit-s/kvit-notes/releases/download/v${VERSION}/SHA256SUMS.txt"
    echo "Fetching $URL"
    if ! curl -fsSL "$URL" -o "$TMP"; then
        echo "Could not download SHA256SUMS.txt for v${VERSION}." >&2
        echo "Pass the file explicitly once the release is drafted." >&2
        exit 1
    fi
    SUMS=$TMP
fi

# The sums file lists every artifact; take the line for this AppImage only.
DIGEST=$(awk -v want="$ASSET" '$2 == want || $2 == "*" want { print $1 }' "$SUMS")

if [ -z "$DIGEST" ]; then
    echo "No entry for $ASSET in $SUMS. Contents:" >&2
    cat "$SUMS" >&2
    exit 1
fi
if ! [[ $DIGEST =~ ^[0-9a-f]{64}$ ]]; then
    echo "Digest for $ASSET is not a SHA-256 hex string: '$DIGEST'" >&2
    exit 1
fi

CURRENT=$(grep -oP "^sha256sums=\('\K[^']*" "$PKGBUILD" || true)
CURRENT_VER=$(grep -oP '^pkgver=\K.*' "$PKGBUILD" || true)

if [ "$CHECK" -eq 1 ]; then
    fail=0
    if [ "$CURRENT_VER" != "$VERSION" ]; then
        echo "PKGBUILD pkgver is '$CURRENT_VER', expected '$VERSION'" >&2
        fail=1
    fi
    if [ "$CURRENT" = "SKIP" ]; then
        echo "PKGBUILD still has sha256sums=('SKIP') - the package would" \
             "accept any bytes from the release URL." >&2
        fail=1
    elif [ "$CURRENT" != "$DIGEST" ]; then
        echo "PKGBUILD digest does not match the release artifact:" >&2
        echo "  PKGBUILD $CURRENT" >&2
        echo "  release  $DIGEST" >&2
        fail=1
    fi
    if [ "$fail" -ne 0 ]; then
        echo "Run tools/update-aur-digest.sh $VERSION to fix." >&2
        exit 1
    fi
    echo "PKGBUILD pins v$VERSION at $DIGEST"
    exit 0
fi

sed -i \
    -e "s|^pkgver=.*|pkgver=${VERSION}|" \
    -e "s|^sha256sums=(.*|sha256sums=('${DIGEST}')|" \
    "$PKGBUILD"

echo "Pinned $PKGBUILD to v$VERSION"
echo "  $ASSET"
echo "  $DIGEST"
echo
echo "Verify with: tools/update-aur-digest.sh --check $VERSION"
