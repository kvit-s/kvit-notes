#!/bin/bash
# Build the Linux AppImage: configure and install the linux-release preset
# into an AppDir, bundle Qt with linuxdeploy-plugin-qt, and emit
# dist/Kvit_Editor-<version>-x86_64.AppImage.
#
# Run from anywhere; the script works from the repo root. It is self-contained:
# a clean checkout needs only Qt on QT_ROOT_DIR, network access to fetch the
# pinned linuxdeploy tools on first run, and FUSE or APPIMAGE_EXTRACT_AND_RUN=1.
# It configures the preset itself rather than assuming a build tree exists,
# because the tag-triggered packaging job in CI runs on a fresh checkout.
#
# Version: pass KVIT_VERSION_FULL, or let the script derive it from the tag
# being built (GITHUB_REF_NAME, or `git describe`). See "Release version"
# below.
set -euo pipefail
cd "$(dirname "$0")/../.."

BUILD_DIR=build-linux-release
DIST=dist
TOOLS=packaging/linux/.tools
APPDIR=$BUILD_DIR/AppDir

# ── Release version
#
# PROJECT_VERSION in CMakeLists.txt carries the three numeric components,
# which is all CMake's project(VERSION) accepts. The version the binary
# reports and the artifact is named for is the full SemVer string including
# any prerelease suffix, so that a v1.0.0-rc1 build calls itself 1.0.0-rc1
# rather than claiming to be the final 1.0.0 release.
#
# Precedence: an explicit KVIT_VERSION_FULL wins; otherwise the tag being
# built supplies it; otherwise this is a local build of the plain base
# version. A tag whose base version disagrees with PROJECT_VERSION is an
# error — that mismatch means the tag and the source have diverged, and
# publishing either interpretation of it would be wrong.
BASE_VERSION=$(grep -oP 'project\(kvit-notes VERSION \K[0-9]+\.[0-9]+\.[0-9]+' \
                    CMakeLists.txt)
if [ -z "$BASE_VERSION" ]; then
    echo "Could not read PROJECT_VERSION from CMakeLists.txt" >&2
    exit 1
fi

if [ -n "${KVIT_VERSION_FULL:-}" ]; then
    VERSION=$KVIT_VERSION_FULL
    VERSION_SOURCE="KVIT_VERSION_FULL"
else
    TAG=${GITHUB_REF_NAME:-}
    if [ -z "$TAG" ]; then
        TAG=$(git describe --tags --exact-match 2>/dev/null || true)
    fi
    if [ -n "$TAG" ]; then
        VERSION=${TAG#v}
        VERSION_SOURCE="tag $TAG"
    else
        VERSION=$BASE_VERSION
        VERSION_SOURCE="CMakeLists.txt (untagged build)"
    fi
fi

# SemVer shape: numeric core, optional dot-separated prerelease identifiers,
# optional build metadata.
SEMVER='^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$'
if ! [[ $VERSION =~ $SEMVER ]]; then
    echo "Version '$VERSION' (from $VERSION_SOURCE) is not a valid SemVer" \
         "version. Tags must look like v1.0.0 or v1.0.0-rc1." >&2
    exit 1
fi

# The numeric core must be exactly PROJECT_VERSION. This is what stops an
# arbitrary v* tag from producing an artifact whose name and reported version
# have nothing to do with the source it was built from.
if [[ $VERSION != "$BASE_VERSION" && $VERSION != "$BASE_VERSION"[-+]* ]]; then
    echo "Version mismatch: '$VERSION' (from $VERSION_SOURCE) does not have" \
         "base version '$BASE_VERSION' from CMakeLists.txt." >&2
    echo "Bump project(kvit-notes VERSION ...) or retag." >&2
    exit 1
fi
echo "Building version $VERSION (from $VERSION_SOURCE)"

# ── Build tools: pinned by content, not by URL
#
# A version-shaped URL is not a content pin. GitHub release assets can be
# replaced in place, and this script runs in the tag job, which holds
# `contents: write` and can therefore modify the published release — so an
# unverified download here is remote code execution with the power to alter
# what users are given. Each tool is verified against the digest below before
# it is made executable, and a mismatch stops the build.
#
# Digests recorded 2026-07-20 by downloading each asset and running sha256sum.
# To move to a newer linuxdeploy release, update both the URL and its digest;
# a URL change without a digest change fails closed.
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20240109-1/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_SHA256="c86d6540f1df31061f02f539a2d3445f8d7f85cc3994eee1e74cd1ac97b76df0"
LINUXDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20240109-1/linuxdeploy-plugin-qt-x86_64.AppImage"
LINUXDEPLOY_QT_SHA256="f53349093d333a6558c560844c1a0f64a3b6bd077bf02740af3ad3dbb8827433"

mkdir -p "$TOOLS" "$DIST"

fetch_verified() {
    local url=$1 want=$2
    local f="$TOOLS/$(basename "$url")"

    if [ -z "$want" ]; then
        echo "No SHA-256 pinned for $url - refusing to run an unverified" \
             "build tool." >&2
        exit 1
    fi

    [ -f "$f" ] || curl -fsSL "$url" -o "$f"

    local got
    got=$(sha256sum "$f" | cut -d' ' -f1)
    if [ "$got" != "$want" ]; then
        echo "Digest mismatch for $(basename "$url")" >&2
        echo "  expected $want" >&2
        echo "  actual   $got" >&2
        # A cached file from an earlier run could be the stale one, so it is
        # removed; a genuinely changed upstream asset then fails again on the
        # next run rather than appearing to be fixed by a retry.
        rm -f "$f"
        exit 1
    fi
    chmod +x "$f"
    echo "verified $(basename "$url") ($want)"
}

fetch_verified "$LINUXDEPLOY_URL" "$LINUXDEPLOY_SHA256"
fetch_verified "$LINUXDEPLOY_QT_URL" "$LINUXDEPLOY_QT_SHA256"

# ── Configure, build, install
#
# Configuring here rather than expecting a build tree is what makes the tag
# job work: it checks out fresh and calls this script directly. Re-running
# cmake on an existing tree is cheap and also re-applies KVIT_VERSION_FULL,
# so a build directory left over from a different version cannot leak into
# the artifact.
cmake --preset linux-release -DKVIT_VERSION_FULL="$VERSION"
cmake --build --preset linux-release -j "$(nproc)" --target kvit-notes
rm -rf "$APPDIR"
cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr"

export APPIMAGE_EXTRACT_AND_RUN=1   # works without FUSE (containers, WSL)
# linuxdeploy resolves dependencies through the loader, so the Qt kit's lib
# dir must be visible to it.
[ -n "${QT_ROOT_DIR:-}" ] && \
    export LD_LIBRARY_PATH="$QT_ROOT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export EXTRA_QT_MODULES="multimedia"
export EXTRA_QT_PLUGINS="imageformats;multimedia"

# linuxdeploy-plugin-qt works out which QML modules to deploy by scanning QML
# source files. This app compiles its QML into resources.qrc, so there is no
# QML tree for the plugin to find by itself, and without this it deployed no
# QML modules whatsoever: the AppImage then held the executable, the Qt
# libraries and the math resources, passed --math-selftest (which never builds
# a QML engine), and died at launch on `module "QtQuick.Controls" plugin
# "qtquickcontrols2plugin" not found`. Pointing the plugin at the QML sources
# that go into the qrc gives it the same import graph the binary will have.
export QML_SOURCES_PATHS="$(pwd)/qml"

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

# ── Qt LGPL obligations (packaging/qt-lgpl-checklist.md)
#
# The project's own licenses arrive through `cmake --install` above; only
# Qt's are added here, because they are a property of the Qt kit rather than
# of this source tree.
#
# The canonical LGPL-3 and GPL-3 texts are vendored in packaging/licenses/qt
# so the artifact is correctly licensed regardless of how the CI Qt kit lays
# its own documents out. The kit's licensing document is copied alongside
# when present, since it states Qt's own terms and module-by-module
# licensing, but it is not what the obligation rests on: the previous
# `cp .../LICENSE.LGPL* || true` matched nothing at all in a Qt 6.10.1 kit
# (whose Licenses/ holds LICENSE, LICENSE.FDL, COPYING.txt, Copyright.txt)
# and suppressed its own failure, so it produced an empty licenses/qt
# directory and an artifact shipping Qt with no LGPL text.
QT_LICENSE_DIR="$APPDIR/usr/share/licenses/kvit-notes/qt"
mkdir -p "$QT_LICENSE_DIR"
cp packaging/licenses/qt/LICENSE.LGPL3 "$QT_LICENSE_DIR/"
cp packaging/licenses/qt/LICENSE.GPL3 "$QT_LICENSE_DIR/"
if [ -n "${QT_ROOT_DIR:-}" ] && [ -f "$QT_ROOT_DIR/../../Licenses/LICENSE" ]; then
    cp "$QT_ROOT_DIR/../../Licenses/LICENSE" "$QT_LICENSE_DIR/LICENSE.qt-kit"
    echo "Included the Qt kit's own licensing document."
else
    echo "Note: the Qt kit ships no Licenses/LICENSE; the vendored LGPL-3" \
         "and GPL-3 texts are the artifact's Qt license payload."
fi

# Fail closed. Neither an artifact missing a required notice nor one that
# cannot start its interface is published: both checks run against the
# finished AppDir, before it is packed.
tools/check-license-payload.sh "$APPDIR/usr" --qt
tools/check-appdir-runtime.sh "$APPDIR"

mkdir -p packaging/manifests
find "$APPDIR" \( -name '*.so*' -o -name 'kvit-notes' \) -type f | sort \
    > "packaging/manifests/linux-$VERSION.txt"

# Pass 2: pack the finished AppDir.
"$TOOLS/$(basename "$LINUXDEPLOY_URL")" \
    --appdir "$APPDIR" \
    --output appimage

# The packed artifact is the thing users download, so it is the thing that
# gets run. The AppDir checks above cannot see anything the squashfs image,
# AppRun or the relocation step breaks, and this suggestion used to be
# printed rather than executed.
tools/check-appimage.sh "$LDAI_OUTPUT"

echo "AppImage: $LDAI_OUTPUT"
echo "Bundled-library manifest: packaging/manifests/linux-$VERSION.txt"
