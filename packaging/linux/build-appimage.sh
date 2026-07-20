#!/bin/bash
# Build the Linux AppImage (launch-plan.md D3.1): install the linux-release
# preset into an AppDir, bundle Qt with linuxdeploy-plugin-qt, and emit
# dist/Kvit_Editor-<version>-x86_64.AppImage.
#
# Run from the repo root (CI does; locally too). Requires the linux-release
# preset to be configured/built, network access to fetch the pinned
# linuxdeploy tools on first run, and FUSE or APPIMAGE_EXTRACT_AND_RUN=1.
set -euo pipefail
cd "$(dirname "$0")/../.."

BUILD_DIR=build-linux-release
DIST=dist
TOOLS=packaging/linux/.tools
APPDIR=$BUILD_DIR/AppDir

# Pinned tool releases (SHA-pinned downloads are not offered upstream; the
# continuous tag is explicitly avoided in favor of the last stable ones).
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20240109-1/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20240109-1/linuxdeploy-plugin-qt-x86_64.AppImage"

mkdir -p "$TOOLS" "$DIST"
for url in "$LINUXDEPLOY_URL" "$LINUXDEPLOY_QT_URL"; do
    f="$TOOLS/$(basename "$url")"
    [ -f "$f" ] || curl -fsSL "$url" -o "$f"
    chmod +x "$f"
done

cmake --build --preset linux-release -j "$(nproc)" --target kvit-notes
rm -rf "$APPDIR"
cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr"

VERSION=$(grep -oP 'project\(kvit-notes VERSION \K[0-9.]+' CMakeLists.txt)
export APPIMAGE_EXTRACT_AND_RUN=1   # works without FUSE (containers, WSL)
# linuxdeploy resolves dependencies through the loader, so the Qt kit's lib
# dir must be visible to it.
[ -n "${QT_ROOT_DIR:-}" ] && \
    export LD_LIBRARY_PATH="$QT_ROOT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export EXTRA_QT_MODULES="multimedia"
export EXTRA_QT_PLUGINS="imageformats;multimedia"

# Because the app links Qt6::Sql, linuxdeploy-plugin-qt deploys EVERY
# sqldrivers/*.so from the kit, and the server-database drivers (Firebird,
# MySQL, ODBC, PostgreSQL) hard-fail on their absent client libraries. The
# app needs only qsqlite (FTS5 global search), so the plugin gets a staged
# plugins directory without them, via a qmake wrapper that rewrites
# QT_INSTALL_PLUGINS and forwards every other query to the real qmake.
STAGED="$BUILD_DIR/qt-plugins-staged"
rm -rf "$STAGED"
mkdir -p "$STAGED"
cp -al "$QT_ROOT_DIR/plugins/." "$STAGED/" 2>/dev/null \
    || cp -a "$QT_ROOT_DIR/plugins/." "$STAGED/"
find "$STAGED/sqldrivers" -name '*.so' ! -name 'libqsqlite.so' -delete
REAL_QMAKE="$QT_ROOT_DIR/bin/qmake"
STAGED_ABS="$(cd "$STAGED" && pwd)"
cat > "$BUILD_DIR/qmake-staged" <<WRAP
#!/bin/bash
if [ "\$1" = "-query" ] && [ "\$2" = "QT_INSTALL_PLUGINS" ]; then
    echo "$STAGED_ABS"
    exit 0
fi
if [ "\$1" = "-query" ] && [ \$# -eq 1 ]; then
    "$REAL_QMAKE" -query | sed "s|^QT_INSTALL_PLUGINS:.*|QT_INSTALL_PLUGINS:$STAGED_ABS|"
    exit 0
fi
exec "$REAL_QMAKE" "\$@"
WRAP
chmod +x "$BUILD_DIR/qmake-staged"
export QMAKE="$(pwd)/$BUILD_DIR/qmake-staged"
# No embedded AppImageUpdate info (LDAI_UPDATE_INFORMATION deliberately
# unset - an empty string is rejected as malformed); the in-app check
# covers release discovery.
export LDAI_OUTPUT="$DIST/Kvit_Editor-$VERSION-x86_64.AppImage"

# Pass 1: deploy Qt into the AppDir (no packing yet).
"$TOOLS/$(basename "$LINUXDEPLOY_URL")" \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/kvit-notes.desktop" \
    --icon-file packaging/icons/hicolor/256x256/apps/kvit-notes.png \
    --plugin qt

# The offscreen platform plugin rides along so `--math-selftest` (the
# packaged relocatability probe, docs/qa-checklist.md) runs headless from
# the AppImage itself; its Qt dependencies are already deployed.
install -m644 "$QT_ROOT_DIR/plugins/platforms/libqoffscreen.so" \
    "$APPDIR/usr/plugins/platforms/"

# Qt LGPL obligations (packaging/qt-lgpl-checklist.md): the license texts
# and the notices file ride inside the AppDir, and the bundled-library list
# becomes the deploy-time enumeration manifest.
mkdir -p "$APPDIR/usr/share/licenses/qt"
if [ -n "${QT_ROOT_DIR:-}" ] && [ -d "$QT_ROOT_DIR/../../Licenses" ]; then
    cp "$QT_ROOT_DIR/../../Licenses"/LICENSE.LGPL* \
       "$APPDIR/usr/share/licenses/qt/" 2>/dev/null || true
fi
cp THIRD-PARTY-NOTICES.md "$APPDIR/usr/share/licenses/"
mkdir -p packaging/manifests
find "$APPDIR" \( -name '*.so*' -o -name 'kvit-notes' \) -type f | sort \
    > "packaging/manifests/linux-$VERSION.txt"

# Pass 2: pack the finished AppDir.
"$TOOLS/$(basename "$LINUXDEPLOY_URL")" \
    --appdir "$APPDIR" \
    --output appimage

echo "AppImage: $LDAI_OUTPUT"
echo "Bundled-library manifest: packaging/manifests/linux-$VERSION.txt"
echo "Smoke check: $LDAI_OUTPUT --math-selftest"
