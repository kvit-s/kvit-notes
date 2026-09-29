#!/usr/bin/env bash
# Builds the macOS app, kvit-notes.app, for both Apple silicon and Intel Macs
# (one universal executable and library), and packages it into dist/:
#
#   on Linux (WSL):  Kvit_Notes-<version>-macos-universal.zip   the app, unsigned
#                    SHA256SUMS-macos.txt
#   on a Mac:        Kvit_Notes-<version>-macos-universal.dmg   the signed app
#                    SHA256SUMS-macos.txt
#
#   packaging/macos/build-macos.sh                  build the app, then zip it
#                                                   (Linux) or sign it and make
#                                                   the disk image (Mac)
#   packaging/macos/build-macos.sh --from-zip ZIP   on a Mac: sign and package
#                                                   an app built on Linux
#
# The bundle:
#   Contents/Info.plist                 packaging/macos/Info.plist, with the version
#   Contents/MacOS/kvit-notes           the program: arm64 and x86_64 joined with lipo
#   Contents/Frameworks/libkvitmath.dylib   the math library, arm64 and x86_64
#   Contents/Resources/kvit.icns        the icon
#   Contents/Resources/math-res/        the math library's resources
#   Contents/Resources/licenses/        the notices and every licence text
# which is where the Go package mathtex looks for the library and resources
# in a bundle, and the app's name, identifier (org.kvit.Notes) and icon.
#
# Building needs Go and zig (for the math library); joining the two
# architectures needs llvm-lipo (llvm-lipo-18 on this Linux machine) or the
# Xcode lipo. Signing, the disk image and notarisation need a Mac with the
# Xcode command line tools (codesign, hdiutil, xcrun), as the app's
# script did. No Mac was available when this was written, so the Mac-only
# part below has not been run.
#
# Signing and notarisation are optional for a local build and required when
# KVIT_REQUIRE_SIGNING=1. Without an identity the app is ad-hoc signed, which
# macOS accepts on the machine that made it but Gatekeeper rejects elsewhere.
#   KVIT_CODESIGN_IDENTITY        a "Developer ID Application: ... (TEAMID)"
#                                 identity in the keychain
#   KVIT_NOTARY_KEYCHAIN_PROFILE  a profile stored by `xcrun notarytool
#                                 store-credentials`, or
#   KVIT_NOTARY_KEY_PATH + KVIT_NOTARY_KEY_ID + KVIT_NOTARY_ISSUER_ID
#                                 an App Store Connect API key, or
#   KVIT_NOTARY_APPLE_ID + KVIT_NOTARY_TEAM_ID + KVIT_NOTARY_PASSWORD
#                                 an Apple ID with an app-specific password.
#
# Version: KVIT_VERSION_FULL, else the tag, else the base version
# (packaging/lib.sh, release_version).
set -euo pipefail
source "$(dirname "$0")/../lib.sh"

FROM_ZIP=
if [ "${1:-}" = --from-zip ]; then
    zip=${2:?--from-zip needs the zip of an app}
    FROM_ZIP=$(cd "$(dirname "$zip")" && pwd)/$(basename "$zip")
fi

ON_MAC=0
[ "$(uname -s)" = Darwin ] && ON_MAC=1
[ -n "$FROM_ZIP" ] && [ "$ON_MAC" = 0 ] && die "--from-zip is for signing on a Mac"

SIGNING_REQUIRED=${KVIT_REQUIRE_SIGNING:-0}
case "$SIGNING_REQUIRED" in
    0|1) ;;
    *) die "KVIT_REQUIRE_SIGNING must be 0 or 1, not '$SIGNING_REQUIRED'" ;;
esac
if [ "$SIGNING_REQUIRED" = 1 ]; then
    [ "$ON_MAC" = 1 ] || die "KVIT_REQUIRE_SIGNING=1 needs a Mac"
    [ -n "${KVIT_CODESIGN_IDENTITY:-}" ] ||
        die "KVIT_REQUIRE_SIGNING=1 but KVIT_CODESIGN_IDENTITY names no Developer ID Application identity"
    if [ -z "${KVIT_NOTARY_KEYCHAIN_PROFILE:-}" ] \
            && ! { [ -n "${KVIT_NOTARY_KEY_PATH:-}" ] && [ -n "${KVIT_NOTARY_KEY_ID:-}" ] \
                   && [ -n "${KVIT_NOTARY_ISSUER_ID:-}" ]; } \
            && ! { [ -n "${KVIT_NOTARY_APPLE_ID:-}" ] && [ -n "${KVIT_NOTARY_TEAM_ID:-}" ] \
                   && [ -n "${KVIT_NOTARY_PASSWORD:-}" ]; }; then
        die "KVIT_REQUIRE_SIGNING=1 but no complete set of notarisation credentials was given"
    fi
fi

release_version
STAGE=$WORK/macos
APP=$STAGE/kvit-notes.app
NAME=Kvit_Notes-$VERSION-macos-universal
mkdir -p "$DIST"

# The Xcode tools on a Mac, the LLVM ones elsewhere.
LIPO=$(command -v lipo || command -v llvm-lipo-18 || command -v llvm-lipo || true)
OTOOL=$(command -v otool || command -v llvm-otool-18 || command -v llvm-otool || true)
[ -n "$LIPO" ] || die "neither lipo nor llvm-lipo is installed"
[ -n "$OTOOL" ] || die "neither otool nor llvm-otool is installed"

# The year of a Unix time, with GNU date (Linux) or BSD date (macOS).
year_of() {
    date -u -d "@$1" +%Y 2> /dev/null || date -u -r "$1" +%Y
}

# The macOS version a thin (one-architecture) Mach-O file needs, from
# LC_BUILD_VERSION (minos) or the older LC_VERSION_MIN_MACOSX (version).
# (The output is read whole before awk sees it: with pipefail, awk leaving
# early would make otool, and so the pipeline, fail.)
macho_minimum() {
    local commands
    commands=$("$OTOOL" -l "$1")
    awk '/LC_BUILD_VERSION|LC_VERSION_MIN_MACOSX/ { want = 1 }
         want && ($1 == "minos" || $1 == "version") { print $2; exit }' <<< "$commands"
}

build_app() {
    rm -rf "$STAGE"
    mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

    echo "== kvit-notes (arm64 and x86_64)"
    go_build darwin arm64 "$STAGE/kvit-notes-arm64"
    go_build darwin amd64 "$STAGE/kvit-notes-amd64"
    "$LIPO" -create -output "$APP/Contents/MacOS/kvit-notes" "$STAGE/kvit-notes-arm64" "$STAGE/kvit-notes-amd64"
    chmod 755 "$APP/Contents/MacOS/kvit-notes"

    echo "== math library"
    MATH=1
    if math_library darwin/arm64 "$STAGE/libkvitmath-arm64.dylib" &&
       math_library darwin/amd64 "$STAGE/libkvitmath-amd64.dylib"; then
        mkdir -p "$APP/Contents/Frameworks"
        "$LIPO" -create -output "$APP/Contents/Frameworks/libkvitmath.dylib" \
            "$STAGE/libkvitmath-arm64.dylib" "$STAGE/libkvitmath-amd64.dylib"
        stage_math_res "$APP/Contents/Resources/math-res"
    else
        MATH=0
    fi

    echo "== bundle"
    cp packaging/icons/kvit.icns "$APP/Contents/Resources/kvit.icns"
    sed -e "s/@VERSION@/$BASE_VERSION/g" \
        -e "s/@COPYRIGHT_YEAR@/$(year_of "$SOURCE_DATE_EPOCH")/g" \
        packaging/macos/Info.plist > "$APP/Contents/Info.plist"
    printf 'APPL????' > "$APP/Contents/PkgInfo"

    echo "== licences"
    python3 packaging/sbom.py check darwin/arm64="$STAGE/kvit-notes-arm64" darwin/amd64="$STAGE/kvit-notes-amd64"
    stage_licenses "$APP/Contents/Resources/licenses" macos "$MATH" \
        "$STAGE/kvit-notes-arm64" "$STAGE/kvit-notes-amd64"
}

# Checks of the finished bundle, on Linux and on a Mac alike.
check_app() {
    echo "== checks"
    local plist_min f archs arch min signed
    plist_min=$(sed -n '/LSMinimumSystemVersion/{n;s/.*<string>\(.*\)<\/string>.*/\1/p;}' "$APP/Contents/Info.plist")
    # Each file must hold both architectures. Each architecture is taken out
    # on its own and checked: the macOS version it needs must not be later
    # than LSMinimumSystemVersion, and the arm64 code must carry a signature
    # (Go's linker and zig sign it ad hoc), because Apple silicon Macs refuse
    # to run arm64 code that has none. (llvm-otool's -arch option does not
    # select the architecture of a universal file, hence lipo -thin.)
    local thin=$STAGE/thin
    for f in "$APP/Contents/MacOS/kvit-notes" "$APP"/Contents/Frameworks/*.dylib; do
        [ -f "$f" ] || continue
        archs=$("$LIPO" -archs "$f" | xargs)
        [[ " $archs " == *" arm64 "* && " $archs " == *" x86_64 "* ]] ||
            die "${f#"$APP"/} has architectures '$archs', not both arm64 and x86_64"
        for arch in $archs; do
            "$LIPO" -thin "$arch" -output "$thin" "$f"
            min=$(macho_minimum "$thin")
            [ -n "$min" ] || die "${f#"$APP"/} ($arch) states no minimum macOS version"
            [ "$(printf '%s\n%s\n' "$min" "$plist_min" | sort -V | tail -1)" = "$plist_min" ] ||
                die "${f#"$APP"/} ($arch) needs macOS $min, later than LSMinimumSystemVersion $plist_min"
            signed=no
            grep -q LC_CODE_SIGNATURE <<< "$("$OTOOL" -l "$thin")" && signed=yes
            [ "$arch" != arm64 ] || [ "$signed" = yes ] || die "${f#"$APP"/} (arm64) has no code signature"
            echo "  ${f#"$APP"/} ($arch): needs macOS $min, signature: $signed"
        done
    done
    rm -f "$thin"
    MATH=0
    [ -f "$APP/Contents/Frameworks/libkvitmath.dylib" ] && MATH=1
    check_manifest "$APP" packaging/manifests/macos.txt
}

if [ -n "$FROM_ZIP" ]; then
    rm -rf "$STAGE"
    mkdir -p "$STAGE"
    ditto -x -k "$FROM_ZIP" "$STAGE"
    [ -d "$APP" ] || die "$FROM_ZIP has no kvit-notes.app at its top"
else
    build_app
fi
check_app

if [ "$ON_MAC" = 0 ]; then
    zip_tree "$STAGE" kvit-notes.app "$DIST/$NAME.zip"
    (cd "$DIST" && sha256_lines "$NAME.zip" > SHA256SUMS-macos.txt)
    echo "== dist/$NAME.zip (unsigned; sign it on a Mac with: $0 --from-zip dist/$NAME.zip)"
    cat "$DIST/SHA256SUMS-macos.txt"
    [ "$MATH" = 1 ] || echo "NOTE: built without the math library and math-res"
    exit 0
fi

# ── On a Mac: sign, try the program, make the disk image, notarise
#
# codesign works from the inside out: the library first, then the bundle,
# whose signature covers everything in it. With an identity the signature uses
# the hardened runtime and a secure timestamp, which notarisation requires;
# the library is signed with the same identity, so the hardened runtime's
# library validation accepts it when the program loads it.
sign_one() {
    if [ -n "${KVIT_CODESIGN_IDENTITY:-}" ]; then
        codesign --force --timestamp --options runtime --sign "$KVIT_CODESIGN_IDENTITY" "$1"
    else
        codesign --force --sign - "$1"
    fi
}
echo "== signing (${KVIT_CODESIGN_IDENTITY:-ad-hoc})"
xattr -cr "$APP"
for f in "$APP"/Contents/Frameworks/*.dylib; do
    [ -f "$f" ] && sign_one "$f"
done
sign_one "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
if [ -n "${KVIT_CODESIGN_IDENTITY:-}" ]; then
    details=$(codesign --display --verbose=4 "$APP" 2>&1)
    grep -q '^Authority=Developer ID Application:' <<< "$details" ||
        die "the app is not signed by a Developer ID Application certificate: $details"
    grep -Eq '^TeamIdentifier=[A-Z0-9]{10}$' <<< "$details" ||
        die "the app's signature has no team identifier: $details"
fi

# The program starts on this Mac, in both architectures when Rosetta is
# there: --help parses the options and exits without opening a window.
echo "== the signed program starts"
"$APP/Contents/MacOS/kvit-notes" --help > /dev/null 2>&1 || die "kvit-notes --help failed"
if [ "$(uname -m)" = arm64 ] && arch -x86_64 /usr/bin/true 2> /dev/null; then
    arch -x86_64 "$APP/Contents/MacOS/kvit-notes" --help > /dev/null 2>&1 ||
        die "kvit-notes --help failed as x86_64"
fi

# A read-only compressed image (UDZO) holding the app and a link to
# /Applications, the usual drag-to-install window. hdiutil is part of macOS.
echo "== disk image"
DMG=$DIST/$NAME.dmg
DMG_ROOT=$WORK/macos-dmg
rm -rf "$DMG_ROOT" "$DMG"
mkdir -p "$DMG_ROOT"
ditto "$APP" "$DMG_ROOT/kvit-notes.app"
ln -s /Applications "$DMG_ROOT/Applications"
hdiutil create -volname "Kvit Notes $VERSION" -srcfolder "$DMG_ROOT" -fs HFS+ -format UDZO -ov "$DMG"
if [ -n "${KVIT_CODESIGN_IDENTITY:-}" ]; then
    codesign --force --timestamp --sign "$KVIT_CODESIGN_IDENTITY" "$DMG"
fi

# Notarisation needs a Developer ID signature, so an ad-hoc signed image is
# not submitted. The ticket is stapled to the image so the app checks out
# without a network connection.
NOTARIZED=0
if [ -z "${KVIT_CODESIGN_IDENTITY:-}" ]; then
    echo "Notarisation skipped: the app is ad-hoc signed; Gatekeeper will refuse it on other Macs."
elif [ -n "${KVIT_NOTARY_KEYCHAIN_PROFILE:-}" ]; then
    xcrun notarytool submit "$DMG" --keychain-profile "$KVIT_NOTARY_KEYCHAIN_PROFILE" --wait
    NOTARIZED=1
elif [ -n "${KVIT_NOTARY_KEY_PATH:-}" ] && [ -n "${KVIT_NOTARY_KEY_ID:-}" ] && [ -n "${KVIT_NOTARY_ISSUER_ID:-}" ]; then
    xcrun notarytool submit "$DMG" --key "$KVIT_NOTARY_KEY_PATH" --key-id "$KVIT_NOTARY_KEY_ID" \
        --issuer "$KVIT_NOTARY_ISSUER_ID" --wait
    NOTARIZED=1
elif [ -n "${KVIT_NOTARY_APPLE_ID:-}" ] && [ -n "${KVIT_NOTARY_TEAM_ID:-}" ] && [ -n "${KVIT_NOTARY_PASSWORD:-}" ]; then
    xcrun notarytool submit "$DMG" --apple-id "$KVIT_NOTARY_APPLE_ID" --team-id "$KVIT_NOTARY_TEAM_ID" \
        --password "$KVIT_NOTARY_PASSWORD" --wait
    NOTARIZED=1
else
    echo "Notarisation skipped: signed with a Developer ID identity, but no notarisation credentials were given." >&2
fi
if [ "$NOTARIZED" = 1 ]; then
    xcrun stapler staple "$DMG"
    xcrun stapler validate "$DMG"
    spctl --assess --type open --context context:primary-signature --verbose=4 "$DMG"
elif [ "$SIGNING_REQUIRED" = 1 ]; then
    die "KVIT_REQUIRE_SIGNING=1 but the disk image was not notarised"
fi

(cd "$DIST" && shasum -a 256 "$NAME.dmg" > SHA256SUMS-macos.txt)
echo "== dist/$NAME.dmg"
cat "$DIST/SHA256SUMS-macos.txt"
[ "$MATH" = 1 ] || echo "NOTE: built without the math library and math-res"
