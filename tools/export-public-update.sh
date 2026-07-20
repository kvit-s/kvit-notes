#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
# Update an existing public checkout from HEAD of this private tree, as a
# normal commit on top of its history.
#
# tools/export-public-tree.sh builds the FIRST public history (a fresh
# single commit) and stays the right tool until that history is published.
# Afterwards the public repository is forkable and its history must not be
# rewritten, so this script replays the allowlist onto the existing clone
# instead: same manifest, same HEAD-only content rule, but the result is
# one ordinary commit.
#
#   tools/export-public-update.sh <public-clone> [commit message]
#
# It stages an export with the audited tooling, mirrors it into the clone
# with delete propagation (so a file dropped from the allowlist disappears
# publicly), and commits. It never pushes: review the diff, then push.
set -euo pipefail
cd "$(dirname "$0")/.."
PRIVATE_ROOT=$(pwd)

CLONE="${1:?usage: export-public-update.sh <public-clone> [message]}"
MESSAGE="${2:-Sync from development tree}"
CLONE=$(cd "$CLONE" && pwd)

if [ ! -d "$CLONE/.git" ]; then
    echo "not a git checkout: $CLONE" >&2
    exit 1
fi
if ! git -C "$CLONE" diff --quiet || ! git -C "$CLONE" diff --cached --quiet; then
    echo "refusing to update: $CLONE has uncommitted changes" >&2
    exit 1
fi

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
"$PRIVATE_ROOT/tools/export-public-tree.sh" "$STAGE/tree" > /dev/null

# Mirror content only: the clone keeps its own history, so .git is exempt.
rsync -a --delete --exclude '.git/' "$STAGE/tree/" "$CLONE/"

cd "$CLONE"
git add -A
if git diff --cached --quiet; then
    echo "public tree already matches HEAD of $PRIVATE_ROOT; nothing to commit"
    exit 0
fi
git commit -q -m "$MESSAGE"
echo "committed to $CLONE:"
git show --stat --oneline HEAD | head -20
echo ""
echo "review, then: git -C $CLONE push"
