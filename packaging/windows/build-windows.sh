#!/usr/bin/env bash
# Builds the Windows artifacts into dist/:
#   Kvit_Notes-<version>-windows-x64.zip   portable zip, one top folder
#   Kvit_Notes-<version>-setup.exe         Inno Setup per-user installer
#   SHA256SUMS-windows.txt                 checksums of both
#
# Runs in WSL: everything is built on Linux except the installer, which the
# Windows program ISCC.exe (Inno Setup 6) compiles; WSL starts it directly.
# packaging/windows/wsl.sh says where it is looked for.
#
# The installed and unzipped layout is the app's, without :
#   kvit-notes.exe      the program, with its icon, version information and
#                       manifest (packaging/windows/make-syso.sh)
#   kvitmath.dll        the math library, which the program looks for beside
#                       itself
#   math-res\           the math library's resources
#   licenses\           the notices and every licence text
#
# The installer keeps the installer's AppId, product name, per-user folder
# (%LOCALAPPDATA%\Programs\Kvit Notes), Start-menu group and .md
# association, so it upgrades an installed  Kvit Notes in place as the same
# product; kvit-notes.iss says how.
#
# Version: KVIT_VERSION_FULL, else the tag, else the base version
# (packaging/lib.sh, release_version).
set -euo pipefail
source "$(dirname "$0")/../lib.sh"
source packaging/windows/wsl.sh
release_version

STAGE_NAME=Kvit_Notes-$VERSION-windows-x64
STAGE=$WORK/windows/$STAGE_NAME
SETUP=Kvit_Notes-$VERSION-setup.exe
rm -rf "$WORK/windows"
mkdir -p "$STAGE" "$DIST"

# ── The executable, with its resources for this version
#
# The resource file in cmd/kvit-notes carries the base version between
# releases; a pre-release (2.0.0-rc1) writes its own version into it for this
# build and puts the base version back afterwards, so the working tree is left
# as it was.
if [ "$VERSION" != "$BASE_VERSION" ]; then
    trap 'packaging/windows/make-syso.sh "$BASE_VERSION" > /dev/null' EXIT
fi
packaging/windows/make-syso.sh "$VERSION"
echo "== kvit-notes.exe"
go_build windows amd64 "$STAGE/kvit-notes.exe"

# The resource table must hold the icon group, the version information and
# the manifest, whatever changed in the resource file or the build.
resources=$(llvm-readobj-18 --coff-resources "$STAGE/kvit-notes.exe")
for type in GROUP_ICON VERSIONINFO MANIFEST; do
    grep -q "^  Type: $type " <<< "$resources" ||
        die "kvit-notes.exe has no $type resource; is cmd/kvit-notes/rsrc_windows_amd64.syso there?"
done
echo "  resources: icon, version information, manifest"

# ── The math library and its resources
echo "== math library"
MATH=1
math_library windows/amd64 "$STAGE/kvitmath.dll" || MATH=0
if [ "$MATH" = 1 ]; then
    stage_math_res "$STAGE/math-res"
fi

# ── Licences, and what the executable contains
echo "== licences"
python3 packaging/sbom.py check windows/amd64="$STAGE/kvit-notes.exe"
stage_licenses "$STAGE/licenses" windows "$MATH" "$STAGE/kvit-notes.exe"

echo "== manifest"
check_manifest "$STAGE" packaging/manifests/windows.txt

# ── Portable zip
echo "== zip"
zip_tree "$WORK/windows" "$STAGE_NAME" "$DIST/$STAGE_NAME.zip"
echo "  dist/$STAGE_NAME.zip"

# ── Installer
#
# ISCC reads the staged tree and writes the installer through WSL's
# \\wsl.localhost share, so nothing is copied to the Windows drive.
echo "== installer"
ISCC=$(find_iscc)
echo "  Inno Setup: $(win_path "$ISCC")"
"$ISCC" /Q \
    "/DKvitVersion=$VERSION" \
    "/DKvitVersionNumeric=$BASE_VERSION" \
    "/DStageDir=$(win_path "$STAGE")" \
    "/DOutputDir=$(win_path "$DIST")" \
    "$(win_path packaging/windows/kvit-notes.iss)" | tr -d '\r'
[ -f "$DIST/$SETUP" ] || die "Inno Setup did not write dist/$SETUP"
echo "  dist/$SETUP"

# ── Checksums
(cd "$DIST" && sha256_lines "$STAGE_NAME.zip" "$SETUP" > SHA256SUMS-windows.txt)
echo "== dist/SHA256SUMS-windows.txt"
cat "$DIST/SHA256SUMS-windows.txt"
[ "$MATH" = 1 ] || echo "NOTE: built without the math library and math-res"
