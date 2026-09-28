#!/usr/bin/env bash
# Builds every release artifact of Kvit Notes into dist/, from this WSL
# machine, and with --test tries them here. packaging/README.md describes
# each artifact and the tools it needs.
#
#   packaging/build-all.sh [--test] [windows] [macos] [linux] [flatpak]
#
# With no platform named, all four are built:
#   windows   Kvit_Notes-<version>-windows-x64.zip, Kvit_Notes-<version>-setup.exe
#   macos     Kvit_Notes-<version>-macos-universal.zip (the app, to be signed
#             and put in a disk image on a Mac: packaging/macos/build-macos.sh)
#   linux     Kvit_Notes-<version>-linux-x86_64.tar.gz, Kvit_Notes-<version>-x86_64.AppImage,
#             aur/PKGBUILD
#   flatpak   Kvit_Notes-<version>-flatpak-sources.tar.gz, flatpak/org.kvit.Notes.yaml
#             (needs linux)
# then SHA256SUMS.txt over every artifact. --test runs
# packaging/windows/test-windows.sh and packaging/linux/test-linux.sh after
# the builds. dist/ is emptied first.
#
# Version: KVIT_VERSION_FULL, else the tag, else the base version
# (packaging/lib.sh, release_version). KVIT_REQUIRE_MATH=1 fails the build
# when the math library cannot be built, instead of packaging without it.
set -euo pipefail
source "$(dirname "$0")/lib.sh"

test=0
platforms=()
for a in "$@"; do
    case $a in
        --test) test=1 ;;
        windows|macos|linux|flatpak) platforms+=("$a") ;;
        -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown argument $a" ;;
    esac
done
[ ${#platforms[@]} -gt 0 ] || platforms=(windows macos linux flatpak)
wants() { [[ " ${platforms[*]} " == *" $1 "* ]]; }
wants flatpak && ! wants linux && die "flatpak needs linux in the same run"

release_version
rm -rf "$DIST"
mkdir -p "$DIST"

wants windows && packaging/windows/build-windows.sh
wants macos && packaging/macos/build-macos.sh
wants linux && packaging/linux/build-linux.sh
wants flatpak && packaging/flatpak/make-sources.sh

# One checksum file over every artifact (the per-platform SHA256SUMS-*.txt
# files stay beside it), the file the AUR and release notes refer to.
(
    cd "$DIST"
    rm -f SHA256SUMS.txt
    find . -type f ! -name 'SHA256SUMS*' | sed 's#^\./##' | LC_ALL=C sort |
        while read -r f; do echo "$(sha256sum "$f" | cut -d' ' -f1)  $f"; done > SHA256SUMS.txt
)

if [ "$test" = 1 ]; then
    wants windows && packaging/windows/test-windows.sh
    wants linux && packaging/linux/test-linux.sh
fi

echo
echo "== dist/ (version $VERSION)"
(cd "$DIST" && find . -type f | sed 's#^\./##' | LC_ALL=C sort | while read -r f; do
    printf '  %-56s %10s  %s\n' "$f" "$(du -h "$f" | cut -f1)" "$(sha256sum "$f" | cut -d' ' -f1)"
done)
