#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Build the macOS artifact: configure and install the macos-release preset into
# a staging prefix, bundle Qt into the .app with macdeployqt, then emit into
# dist/:
#   Kvit_Notes-<version>-macos-<arch>.dmg   compressed disk image
#   SHA256SUMS-macos.txt                    checksum of the image
#
# Run from anywhere; it works from the repo root. A clean checkout needs only
# the Qt clang_64 kit (QT_ROOT_DIR on the environment, or macdeployqt on PATH)
# and the Xcode command line tools (codesign, hdiutil, xcrun, install_name_tool,
# shasum). It configures the preset itself, because the tag-triggered packaging
# job in CI runs on a fresh checkout. This mirrors
# packaging/linux/build-appimage.sh and packaging/windows/build-windows.ps1;
# keep the three in step.
#
# Signing and notarization are OPTIONAL and run only when their credentials are
# present in the environment (see "Code signing and notarization" below). With
# no credentials the script still produces a complete, self-contained,
# ad-hoc-signed .dmg — Gatekeeper will warn on first open, but every packaging
# mechanism the launch build needs is exercised. The Developer ID identity and
# the notarization profile plug in unchanged once the Apple Developer account
# is active.
#
# Version: pass KVIT_VERSION_FULL, or let the script derive it from the tag
# being built (GITHUB_REF_NAME, or `git describe`). See "Release version".
set -euo pipefail
cd "$(dirname "$0")/../.."

BUILD_DIR=build-macos-release
DIST=dist
STAGE="$BUILD_DIR/stage"
APP="$STAGE/kvit-notes.app"

# ── Release version
#
# The rules, precedence and validation are identical to build-appimage.sh and
# build-windows.ps1: PROJECT_VERSION in CMakeLists.txt carries the three numeric
# components, the version the binary reports and the artifact is named for is
# the full SemVer string including any prerelease suffix, an explicit
# KVIT_VERSION_FULL wins, otherwise the tag supplies it, otherwise this is a
# local build of the base version, and a tag whose base version disagrees with
# PROJECT_VERSION is an error.
BASE_VERSION=$(grep -oE 'project\(kvit-notes VERSION [0-9]+\.[0-9]+\.[0-9]+' \
                    CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
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

SEMVER='^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$'
if ! [[ $VERSION =~ $SEMVER ]]; then
    echo "Version '$VERSION' (from $VERSION_SOURCE) is not a valid SemVer" \
         "version. Tags must look like v1.0.0 or v1.0.0-rc1." >&2
    exit 1
fi
if [[ $VERSION != "$BASE_VERSION" && $VERSION != "$BASE_VERSION"[-+]* ]]; then
    echo "Version mismatch: '$VERSION' (from $VERSION_SOURCE) does not have" \
         "base version '$BASE_VERSION' from CMakeLists.txt." >&2
    echo "Bump project(kvit-notes VERSION ...) or retag." >&2
    exit 1
fi
echo "Building version $VERSION (from $VERSION_SOURCE)"

# ── Architecture
#
# The official Qt macOS kit is a universal (arm64 + x86_64) binary, so a
# universal build needs only CMAKE_OSX_ARCHITECTURES set and the whole tree to
# compile for both slices. That is opt-in through KVIT_MACOS_ARCHS ("arm64",
# "x86_64", or "arm64;x86_64"); the default is the host slice, which is what a
# single macos-14 (Apple Silicon) runner produces. The arch label in the
# artifact name records which it was.
ARCH_ARGS=()
if [ -n "${KVIT_MACOS_ARCHS:-}" ]; then
    ARCH_ARGS=(-DCMAKE_OSX_ARCHITECTURES="$KVIT_MACOS_ARCHS")
    case "$KVIT_MACOS_ARCHS" in
        *";"*) ARCH_LABEL=universal ;;
        *)     ARCH_LABEL=$KVIT_MACOS_ARCHS ;;
    esac
else
    ARCH_LABEL=$(uname -m)   # arm64 or x86_64
fi

# ── Locate the Qt kit tools
#
# QT_ROOT_DIR is what install-qt-action exports and what the offscreen plugin
# and the kit's own license document are found relative to. macdeployqt is
# taken from the kit when QT_ROOT_DIR is set, else from PATH.
if [ -n "${QT_ROOT_DIR:-}" ]; then
    MACDEPLOYQT="$QT_ROOT_DIR/bin/macdeployqt"
else
    MACDEPLOYQT=$(command -v macdeployqt || true)
fi
if [ -z "$MACDEPLOYQT" ] || [ ! -x "$MACDEPLOYQT" ]; then
    echo "macdeployqt not found (set QT_ROOT_DIR or put it on PATH)." >&2
    exit 1
fi

# ── Configure, build, install into the staging prefix
#
# Configuring here rather than expecting a build tree is what makes the tag job
# work: it checks out fresh and calls this script directly. The BUNDLE install
# rule (CMakeLists.txt) drops kvit-notes.app at the prefix root with math-res
# and the project's own license texts already inside Contents/Resources.
# ${ARCH_ARGS[@]+...} expands to nothing when the array is empty. macOS runs
# bash 3.2, where a bare "${ARR[@]}" over an empty array trips `set -u`.
cmake --preset macos-release -DKVIT_VERSION_FULL="$VERSION" \
    ${ARCH_ARGS[@]+"${ARCH_ARGS[@]}"}
cmake --build --preset macos-release -j "$(sysctl -n hw.ncpu)" --target kvit-notes
rm -rf "$STAGE"
cmake --install "$BUILD_DIR" --prefix "$STAGE"

if [ ! -d "$APP" ]; then
    echo "Install did not produce $APP." >&2
    exit 1
fi

# ── The offscreen platform plugin, deployed with the bundle
#
# macdeployqt deploys qcocoa, not qoffscreen, and the packaged --math-selftest
# below runs headless. Copying the plugin into the bundle BEFORE macdeployqt and
# naming it to -executable lets the same pass rewrite its Qt references to the
# bundled frameworks and give it the @executable_path/../Frameworks rpath, so it
# resolves them exactly as qcocoa does. (@executable_path is the main binary in
# Contents/MacOS, so ../Frameworks is the bundled Contents/Frameworks.)
mkdir -p "$APP/Contents/PlugIns/platforms"
OFFSCREEN_SRC="${QT_ROOT_DIR:-}/plugins/platforms/libqoffscreen.dylib"
if [ ! -f "$OFFSCREEN_SRC" ]; then
    echo "Offscreen platform plugin not found at $OFFSCREEN_SRC" \
         "(QT_ROOT_DIR must point at the Qt kit)." >&2
    exit 1
fi
cp "$OFFSCREEN_SRC" "$APP/Contents/PlugIns/platforms/"

# ── Deploy the Qt runtime
#
# -qmldir gives macdeployqt the import graph: the app compiles its QML into
# resources.qrc, so there is no QML tree to scan otherwise — the same failure
# that once shipped a Linux AppImage unable to load its own UI.
"$MACDEPLOYQT" "$APP" \
    -qmldir="$PWD/qml" \
    -executable="$APP/Contents/PlugIns/platforms/libqoffscreen.dylib"

# Because the app links Qt6::Sql, macdeployqt stages the kit's SQL drivers. The
# app needs only qsqlite (the FTS5 global search index); the server-database
# drivers (Firebird, MySQL, ODBC, PostgreSQL) reference client libraries that
# are not present, so any that were copied are removed. This mirrors the pruning
# the AppImage does through its staged-plugins qmake wrapper.
SQLDRIVERS="$APP/Contents/PlugIns/sqldrivers"
if [ -d "$SQLDRIVERS" ]; then
    find "$SQLDRIVERS" -name '*.dylib' ! -name 'libqsqlite.dylib' -delete
fi

# ── Qt LGPL obligations (packaging/qt-lgpl-checklist.md)
#
# The project's own licenses and the math-res font licenses arrive through
# `cmake --install`; only Qt's are added here, since they belong to the Qt kit
# rather than this source tree. The canonical LGPL-3 and GPL-3 texts are
# vendored in packaging/licenses/qt so the artifact is correctly licensed
# regardless of how the CI Qt kit lays its own documents out; the kit's own
# licensing document is copied alongside when present.
QT_LICENSE_DIR="$APP/Contents/Resources/licenses/qt"
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

# ── Code signing and notarization (optional; gated on credentials)
#
# codesign runs inside-out — every nested framework and dylib first, then the
# app bundle last — because a hardened-runtime signature over the outer bundle
# is only valid if everything it contains is already signed. Without an
# identity the bundle is ad-hoc signed ("-"), which keeps the load commands
# consistent after macdeployqt rewrote them but does not satisfy Gatekeeper.
#
# KVIT_CODESIGN_IDENTITY  a "Developer ID Application: …(TEAMID)" identity that
#                         is present in the keychain. Unset → ad-hoc.
sign_one() {
    local target=$1
    if [ -n "${KVIT_CODESIGN_IDENTITY:-}" ]; then
        codesign --force --timestamp --options runtime \
            --sign "$KVIT_CODESIGN_IDENTITY" "$target"
    else
        codesign --force --sign - "$target"
    fi
}

echo "Signing nested code (${KVIT_CODESIGN_IDENTITY:+identity: $KVIT_CODESIGN_IDENTITY}${KVIT_CODESIGN_IDENTITY:-ad-hoc})"
# Frameworks and loadable dylibs first, then the app. Process substitution
# rather than a pipe keeps the loop in this shell, so `set -e` aborts on a
# codesign failure instead of the pipeline swallowing it. -depth signs nested
# items before the framework directory that contains them.
while IFS= read -r fw; do
    sign_one "$fw"
done < <(find "$APP/Contents/Frameworks" -depth -name '*.framework' -type d 2>/dev/null)
while IFS= read -r dylib; do
    sign_one "$dylib"
done < <(find "$APP/Contents/PlugIns" "$APP/Contents/Frameworks" \
             -name '*.dylib' -type f 2>/dev/null)
# The bundle last.
sign_one "$APP"

# ── Fail closed: a complete license payload and a runnable interface
#
# Neither an artifact missing a required notice nor one that cannot start is
# published. check-license-payload.sh searches Contents/Resources, which holds
# the project licenses, the math-res font licenses and the Qt texts just added.
tools/check-license-payload.sh "$APP/Contents/Resources" --qt

# Prove the bundle is self-contained: run the math self-test with the Qt kit
# removed from the dynamic-loader environment, so only the bundled frameworks
# and the bundled offscreen plugin can satisfy it. On macOS the frameworks are
# found through the bundle's baked-in rpath rather than the environment, so
# clearing DYLD_* and QT_PLUGIN_PATH leaves nothing but the bundle to resolve.
echo "Packaged --math-selftest (headless, bundle-only):"
env -u DYLD_FRAMEWORK_PATH -u DYLD_LIBRARY_PATH -u QT_PLUGIN_PATH \
    QT_QPA_PLATFORM=offscreen \
    "$APP/Contents/MacOS/kvit-notes" --math-selftest

# Bundled-runtime manifest, mirroring packaging/manifests/{linux,windows}-*.
mkdir -p packaging/manifests
{
    find "$APP/Contents/Frameworks" -name '*.framework' -type d 2>/dev/null \
        | sed "s#^$APP/##"
    find "$APP/Contents" \( -name '*.dylib' -o -name 'kvit-notes' \) -type f \
        | sed "s#^$APP/##"
} | sort > "packaging/manifests/macos-$VERSION.txt"

# ── Disk image
#
# A read-only, compressed (UDZO) image whose single window holds the .app and a
# symlink to /Applications, the familiar drag-to-install layout. hdiutil is part
# of macOS, so no extra tool is fetched.
mkdir -p "$DIST"
DMG_ROOT="$BUILD_DIR/dmg-root"
rm -rf "$DMG_ROOT"
mkdir -p "$DMG_ROOT"
cp -R "$APP" "$DMG_ROOT/"
ln -s /Applications "$DMG_ROOT/Applications"

DMG="$DIST/Kvit_Notes-$VERSION-macos-$ARCH_LABEL.dmg"
rm -f "$DMG"
hdiutil create \
    -volname "Kvit Notes $VERSION" \
    -srcfolder "$DMG_ROOT" \
    -fs HFS+ \
    -format UDZO \
    -ov \
    "$DMG"

# Sign the image itself when there is a real identity (an ad-hoc-signed image
# would only warn), so its signature travels with it for stapling.
if [ -n "${KVIT_CODESIGN_IDENTITY:-}" ]; then
    codesign --force --timestamp --sign "$KVIT_CODESIGN_IDENTITY" "$DMG"
fi

# ── Notarization (optional; gated on credentials)
#
# Submit the image to Apple's notary service and staple the ticket so the app
# validates offline. Two ways to supply credentials; the keychain profile is
# preferred because no secret sits in the environment.
#
#   KVIT_NOTARY_KEYCHAIN_PROFILE   a profile stored by `xcrun notarytool
#                                  store-credentials`, or
#   KVIT_NOTARY_APPLE_ID + KVIT_NOTARY_TEAM_ID + KVIT_NOTARY_PASSWORD
#                                  an app-specific password triple.
#
# Notarization requires a Developer ID signature, so it is skipped (not failed)
# whenever the bundle was only ad-hoc signed.
if [ -z "${KVIT_CODESIGN_IDENTITY:-}" ]; then
    echo "Notarization skipped: no Developer ID identity, so the image is" \
         "ad-hoc signed. Gatekeeper will warn on first open."
elif [ -n "${KVIT_NOTARY_KEYCHAIN_PROFILE:-}" ]; then
    echo "Notarizing via keychain profile $KVIT_NOTARY_KEYCHAIN_PROFILE"
    xcrun notarytool submit "$DMG" \
        --keychain-profile "$KVIT_NOTARY_KEYCHAIN_PROFILE" --wait
    xcrun stapler staple "$DMG"
elif [ -n "${KVIT_NOTARY_APPLE_ID:-}" ] && [ -n "${KVIT_NOTARY_TEAM_ID:-}" ] \
        && [ -n "${KVIT_NOTARY_PASSWORD:-}" ]; then
    echo "Notarizing via Apple ID $KVIT_NOTARY_APPLE_ID"
    xcrun notarytool submit "$DMG" \
        --apple-id "$KVIT_NOTARY_APPLE_ID" \
        --team-id "$KVIT_NOTARY_TEAM_ID" \
        --password "$KVIT_NOTARY_PASSWORD" --wait
    xcrun stapler staple "$DMG"
else
    echo "Notarization skipped: signed with a Developer ID identity but no" \
         "notary credentials supplied (KVIT_NOTARY_KEYCHAIN_PROFILE, or the" \
         "KVIT_NOTARY_APPLE_ID/TEAM_ID/PASSWORD triple)." >&2
fi

# ── Checksum
( cd "$DIST" && shasum -a 256 "$(basename "$DMG")" > SHA256SUMS-macos.txt )

echo ""
echo "macOS artifact: $DMG"
echo "Bundled-runtime manifest: packaging/manifests/macos-$VERSION.txt"
