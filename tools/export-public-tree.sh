#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
# Build the public kvit-notes tree (launch-plan.md A1): the files named by
# packaging/export-allowlist.txt, taken from HEAD, committed as a fresh
# single-commit history. The result is the private mirror of the future
# public repo; tools/export-gates.sh gates it before anything goes public.
#
#   tools/export-public-tree.sh <target-dir>
set -euo pipefail
cd "$(dirname "$0")/.."

TARGET="${1:?usage: export-public-tree.sh <target-dir>}"

if ! git diff --quiet HEAD || ! git diff --cached --quiet; then
    echo "refusing to export: uncommitted changes (the export is HEAD)" >&2
    exit 1
fi
if [ -e "$TARGET" ] && [ -n "$(ls -A "$TARGET" 2>/dev/null)" ]; then
    echo "refusing to export into non-empty $TARGET" >&2
    exit 1
fi

# Pathspecs from the manifest (comments stripped).
mapfile -t SPECS < <(grep -v '^\s*#' packaging/export-allowlist.txt | grep -v '^\s*$')

mkdir -p "$TARGET"
git ls-files -z -- "${SPECS[@]}" | while IFS= read -r -d '' f; do
    mkdir -p "$TARGET/$(dirname "$f")"
    git show "HEAD:$f" > "$TARGET/$f"
    chmod --reference="$f" "$TARGET/$f"
done

COUNT=$(git ls-files -- "${SPECS[@]}" | wc -l)
(
    cd "$TARGET"
    git init -q -b main
    git add -A
    git -c user.name="Kvit Notes" -c user.email="info@kvit.app" \
        commit -q -m "Initial public release"
)
echo "exported $COUNT files to $TARGET (fresh single-commit history)"
echo "next: tools/export-gates.sh $TARGET"
