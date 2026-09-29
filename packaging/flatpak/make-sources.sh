#!/usr/bin/env bash
# Makes what the Flatpak build needs besides the tagged source, which Flathub
# fetches before it builds without network access:
#   dist/Kvit_Notes-<version>-flatpak-sources.tar.gz, one top folder holding
#     vendor/     every Go module the program is built from, as `go mod vendor`
#                 writes it, including kvit-ui and the patched typesetting that
#                 go.mod takes from ../kvit-ui
#     licenses/   the licence folder of the Linux packages
#   dist/flatpak/org.kvit.Notes.yaml, the manifest with this version and the
#     archive's digest filled in (the git commit is set when the tag exists)
#
#   packaging/flatpak/make-sources.sh
#
# Run packaging/linux/build-linux.sh first; its licence folder is reused.
# `go mod vendor` runs in a copy of the checkout, so the repository does not
# change. The copy is then built the way the Flatpak builds it: from vendor/,
# with module downloads off (GOPROXY=off) and without ../kvit-ui.
set -euo pipefail
source "$(dirname "$0")/../lib.sh"
release_version

NAME=Kvit_Notes-$VERSION-flatpak-sources
LICENSES=$WORK/linux/tar/Kvit_Notes-$VERSION-linux-x86_64/share/licenses/kvit-notes
[ -d "$LICENSES" ] || die "run packaging/linux/build-linux.sh first"

FP=$WORK/flatpak
SRC=$FP/src/kvit-notes
rm -rf "$FP"
mkdir -p "$SRC" "$DIST/flatpak"

echo "== copy of the checkout"
# The files git would commit: tracked ones, and new ones not ignored.
git ls-files -z --cached --others --exclude-standard |
    tar --null --ignore-failed-read -T - -cf - 2> /dev/null | tar -xf - -C "$SRC"

echo "== go mod vendor"
# go.mod's replacements by relative path (../kvit-ui and a folder inside
# it) must resolve from the copy while vendoring. The first folder of each
# gets one link beside the copy, to the real folder beside the checkout, and
# the links are removed afterwards.
links=()
for top in $(sed -n 's#^replace .* => \.\./\([^/ ]*\).*$#\1#p' go.mod | sort -u); do
    ln -s "$(cd "$REPO/../$top" && pwd)" "$FP/src/$top"
    links+=("$FP/src/$top")
done
(cd "$SRC" && GOFLAGS=-mod=mod go mod vendor)
for l in "${links[@]}"; do
    [ -L "$l" ] && rm "$l"
done
echo "  $(grep -c '^# ' "$SRC/vendor/modules.txt") modules in vendor/"

echo "== offline build from vendor/"
(cd "$SRC" && GOPROXY=off GOFLAGS=-mod=vendor go build -trimpath -o "$FP/kvit-notes" ./cmd/kvit-notes)
echo "  built $(go version "$FP/kvit-notes" | cut -d' ' -f2-)"

echo "== archive"
mkdir -p "$FP/out/$NAME"
mv "$SRC/vendor" "$FP/out/$NAME/vendor"
cp -R "$LICENSES" "$FP/out/$NAME/licenses"
tar_tree "$FP/out" "$NAME" "$DIST/$NAME.tar.gz"
sum=$(sha256sum "$DIST/$NAME.tar.gz" | cut -d' ' -f1)
echo "  dist/$NAME.tar.gz  $sum"

# The manifest for this version: the tag, the archive's URL and digest, the
# version the program reports, and the tag's commit when the tag exists here.
commit=$(git rev-parse -q --verify "refs/tags/v$VERSION^{commit}" || true)
sed -e "s#^\(tag: \)v.*#\1v$VERSION#" \
    -e "s#releases/download/v[^/]*/Kvit_Notes-[^/]*-flatpak-sources.tar.gz#releases/download/v$VERSION/$NAME.tar.gz#" \
    -e "/flatpak-sources.tar.gz/{n;s#sha256: .*#sha256: $sum#;}" \
    -e "s#^\(KVIT_VERSION: \).*#\1$VERSION#" \
    ${commit:+-e "s#^\(commit: \).*#\1$commit#"} \
    packaging/flatpak/org.kvit.Notes.yaml > "$DIST/flatpak/org.kvit.Notes.yaml"
echo "  dist/flatpak/org.kvit.Notes.yaml${commit:+ (commit $commit)}"
[ -n "$commit" ] || echo "  NOTE: no tag v$VERSION here; set the commit in dist/flatpak/org.kvit.Notes.yaml after tagging"
