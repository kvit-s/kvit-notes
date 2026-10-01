#!/usr/bin/env bash
# Builds the Linux artifacts into dist/:
#   Kvit_Notes-<version>-linux-x86_64.tar.gz   the program in a plain folder tree
#   Kvit_Notes-<version>-x86_64.AppImage       the same tree as one runnable file
#   SHA256SUMS-linux.txt                       checksums of both
#   aur/PKGBUILD                               the AUR -bin package for this version,
#                                              pinned to the tar.gz's digest
#
# The tree (the tar.gz's top folder, and usr/ in the AppImage):
#   bin/kvit-notes                             the program
#   lib/kvit-notes/libkvitmath.so              the math library
#   share/kvit-notes/math-res/                 the math library's resources
#   share/applications/kvit-notes.desktop      the launcher (MimeType=text/markdown)
#   share/icons/hicolor/*/apps/kvit-notes.*    the icon
#   share/metainfo/org.kvit.Notes.metainfo.xml the AppStream description
#   share/licenses/kvit-notes/                 the notices and every licence text
# The Go package mathtex finds the library and resources from bin/ through
# ../lib/kvit-notes and ../share/kvit-notes, so the tree works wherever it is
# unpacked (~/.local, /usr/local, /opt/...). The program needs libX11 and libGL
# from the system, as any desktop has, and nothing else.
#
# The AppImage is made by appimagetool with a separately pinned type-2
# runtime, both downloaded once into packaging/.tools and checked against the
# digests below. appimagetool runs with --appimage-extract-and-run so the
# build machine needs no FUSE. linuxdeploy is not needed: the program has no
# libraries to collect.
#
# packaging/linux/test-linux.sh runs the results.
#
# Version: KVIT_VERSION_FULL, else the tag, else the base version
# (packaging/lib.sh, release_version).
set -euo pipefail
source "$(dirname "$0")/../lib.sh"
release_version

# ── Pinned tools
#
# The digests were recorded on 2026-08-02 and match what these URLs served
# on 2026-09-27. The runtime is pinned apart
# from appimagetool so that an older runtime inside the tool cannot bring back
# the libfuse.so.2 dependency on FUSE 3 systems. Its source is
# AppImage/type2-runtime at commit 75849dce7cc37e4319b633df1f116ca895c71a12
# (packaging/sbom.yaml); keep the URL, digest, commit and that entry together.
APPIMAGETOOL_URL=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
APPIMAGETOOL_SHA256=a6d71e2b6cd66f8e8d16c37ad164658985e0cf5fcaa950c90a482890cb9d13e0
APPIMAGE_RUNTIME_URL=https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
APPIMAGE_RUNTIME_SHA256=1cc49bcf1e2ccd593c379adb17c9f85a36d619088296504de95b1d06215aebbf

TREE_NAME=Kvit_Notes-$VERSION-linux-x86_64
TARBALL=$TREE_NAME.tar.gz
APPIMAGE=Kvit_Notes-$VERSION-x86_64.AppImage
APPDIR=$WORK/linux/AppDir
USR=$APPDIR/usr
rm -rf "$WORK/linux"
mkdir -p "$USR/bin" "$DIST"

# ── The tree
echo "== kvit-notes"
go_build linux amd64 "$USR/bin/kvit-notes"

echo "== math library"
MATH=1
math_library linux/amd64 "$USR/lib/kvit-notes/libkvitmath.so" || MATH=0
if [ "$MATH" = 1 ]; then
    chmod 755 "$USR/lib/kvit-notes/libkvitmath.so"
    stage_math_res "$USR/share/kvit-notes/math-res"
fi

echo "== launcher, icons, metadata"
install -Dm644 packaging/linux/kvit-notes.desktop "$USR/share/applications/kvit-notes.desktop"
for size in 16 24 32 48 64 128 256 512; do
    install -Dm644 "packaging/icons/hicolor/${size}x${size}/apps/kvit-notes.png" \
        "$USR/share/icons/hicolor/${size}x${size}/apps/kvit-notes.png"
done
install -Dm644 packaging/icons/kvit.svg "$USR/share/icons/hicolor/scalable/apps/kvit-notes.svg"
# The metadata gets a <release> entry for this version when the file has
# none yet (a pre-release, or a release whose entry was not written), dated
# with the packaged commit, so software centres show the right version.
METAINFO=$USR/share/metainfo/org.kvit.Notes.metainfo.xml
install -Dm644 packaging/flatpak/org.kvit.Notes.metainfo.xml "$METAINFO"
if ! grep -q "<release version=\"$VERSION\"" "$METAINFO"; then
    date=$(date -u -d "@$SOURCE_DATE_EPOCH" +%Y-%m-%d)
    type=stable
    [[ $VERSION == *-* ]] && type=development
    sed -i "s|  <releases>|  <releases>\n    <release version=\"$VERSION\" date=\"$date\" type=\"$type\"/>|" "$METAINFO"
fi
if command -v appstreamcli > /dev/null; then
    appstreamcli validate --no-net --explain "$METAINFO" | sed 's/^/  /'
fi

echo "== licences"
python3 packaging/sbom.py check linux/amd64="$USR/bin/kvit-notes"
stage_licenses "$USR/share/licenses/kvit-notes" linux "$MATH" "$USR/bin/kvit-notes"

# ── The AppDir: the tree as usr/, and what the AppImage needs around it
#
# The tar.gz's folder is copied from usr/ before the AppImage's own files are
# added. The AppImage runtime starts AppRun at the top of the image; here it
# is a link to the program, which finds everything else from its own path.
# The runtime's licence texts go into the AppImage only.
echo "== AppDir"
rm -rf "$WORK/linux/tar"
mkdir -p "$WORK/linux/tar"
cp -a "$USR" "$WORK/linux/tar/$TREE_NAME"
ln -s usr/bin/kvit-notes "$APPDIR/AppRun"
cp packaging/linux/kvit-notes.desktop "$APPDIR/kvit-notes.desktop"
cp packaging/icons/hicolor/256x256/apps/kvit-notes.png "$APPDIR/kvit-notes.png"
ln -s kvit-notes.png "$APPDIR/.DirIcon"
stage_licenses "$USR/share/licenses/kvit-notes" appimage "$MATH" "$USR/bin/kvit-notes"
check_manifest "$APPDIR" packaging/manifests/linux.txt

# ── tar.gz
echo "== tar.gz"
check_manifest "$WORK/linux/tar/$TREE_NAME" packaging/manifests/linux.txt usr appimage-runtime/
tar_tree "$WORK/linux/tar" "$TREE_NAME" "$DIST/$TARBALL"
echo "  dist/$TARBALL"

# ── AppImage
echo "== AppImage"
fetch_verified "$APPIMAGETOOL_URL" "$APPIMAGETOOL_SHA256"
fetch_verified "$APPIMAGE_RUNTIME_URL" "$APPIMAGE_RUNTIME_SHA256"
rm -f "$DIST/$APPIMAGE"
# No update information is embedded (the app checks for releases itself).
# SOURCE_DATE_EPOCH, exported by lib.sh, makes mksquashfs use the commit's
# time for every file.
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$TOOLS/$(basename "$APPIMAGETOOL_URL")" \
    --runtime-file "$TOOLS/$(basename "$APPIMAGE_RUNTIME_URL")" \
    "$APPDIR" "$DIST/$APPIMAGE" 2>&1 | sed 's/^/  /'
[ -x "$DIST/$APPIMAGE" ] || die "appimagetool did not write dist/$APPIMAGE"

# What users download is the packed file, so its contents are checked, not
# only the tree it was made from: unpack it and compare with the manifest.
rm -rf "$WORK/linux/unpacked"
mkdir -p "$WORK/linux/unpacked"
(cd "$WORK/linux/unpacked" && "$DIST/$APPIMAGE" --appimage-extract > /dev/null)
echo "  unpacked dist/$APPIMAGE:"
check_manifest "$WORK/linux/unpacked/squashfs-root" packaging/manifests/linux.txt

# ── AUR package for this version
#
# packaging/aur/kvit-notes-bin/PKGBUILD has a placeholder version and digest;
# dist/aur/PKGBUILD is the copy to publish, with this version and the digest
# of the tar.gz it downloads (the release asset of the same name).
mkdir -p "$DIST/aur"
sum=$(sha256sum "$DIST/$TARBALL" | cut -d' ' -f1)
pkgver=${VERSION//-/_}
sed -e "s/^pkgver=.*/pkgver=$pkgver/" \
    -e "s/^_version=.*/_version=$VERSION/" \
    -e "s/^sha256sums=.*/sha256sums=('$sum')/" \
    packaging/aur/kvit-notes-bin/PKGBUILD > "$DIST/aur/PKGBUILD"

# ── Checksums
(cd "$DIST" && sha256_lines "$TARBALL" "$APPIMAGE" > SHA256SUMS-linux.txt)
echo "== dist/SHA256SUMS-linux.txt"
cat "$DIST/SHA256SUMS-linux.txt"
[ "$MATH" = 1 ] || echo "NOTE: built without the math library and math-res"
