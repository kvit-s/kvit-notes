#!/usr/bin/env bash
# Runs the Linux artifacts on this machine:
#   - the AppImage, as users start it (mounted through FUSE), opening a
#     scratch vault for a few seconds (--close-after), and whether the math
#     library and resources are in the mounted image;
#   - the math self-test inside the AppImage's tree and inside the unpacked
#     tar.gz (packaging/lib.sh, math_probe, says how);
#   - the unpacked tar.gz's program, the same way as the AppImage;
#   - dist/aur/PKGBUILD: its digest is the tar.gz's, and its package() step,
#     run on the unpacked tar.gz, gives a /usr tree in which the self-test
#     passes too.
#
#   packaging/linux/test-linux.sh
#
# Needs a display (DISPLAY); this machine's WSLg provides one. Run
# packaging/linux/build-linux.sh first. Everything runs in a scratch folder
# outside the repository, with HOME and the XDG folders pointed into it, so
# the program never reads or writes the user's settings or vaults; it is
# removed afterwards, and every process started is stopped.
set -euo pipefail
source "$(dirname "$0")/../lib.sh"
release_version

TREE_NAME=Kvit_Notes-$VERSION-linux-x86_64
TARBALL=$DIST/$TREE_NAME.tar.gz
APPIMAGE=$DIST/Kvit_Notes-$VERSION-x86_64.AppImage
[ -f "$TARBALL" ] && [ -x "$APPIMAGE" ] || die "run packaging/linux/build-linux.sh first"
[ -n "${DISPLAY:-}" ] || die "no DISPLAY; the program opens a window"

T=$(mktemp -d "${TMPDIR:-/tmp}/kvit-notes-linux-test.XXXXXX")
PIDS=()
cleanup() {
    local p
    for p in "${PIDS[@]}"; do
        kill "$p" 2> /dev/null || true
    done
    rm -rf "$T"
}
trap cleanup EXIT

FAILED=0
ok() { echo "  ok    $*"; }
fail() { echo "  FAIL  $*"; FAILED=1; }
info() { echo "  info  $*"; }

VAULT=$T/vault
mkdir -p "$VAULT" "$T/home"
printf '# Packaging probe\n\nA note for the packaging test.\n\n$E = mc^2$\n' > "$VAULT/Packaging probe.md"
RUN_ENV=(env -u KVIT_MATH_LIB -u KVIT_MATH_RES HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config"
         XDG_CACHE_HOME="$T/home/.cache" XDG_DATA_HOME="$T/home/.local/share")

# The kvit-notes process started by the command whose process is $1: the
# command itself, or for the AppImage the child its runtime starts.
program_pid() {
    local p c
    for p in "$1" $(pgrep -P "$1" 2> /dev/null); do
        c=$(readlink "/proc/$p/exe" 2> /dev/null || true)
        [ "$(basename "$c")" = kvit-notes ] && { echo "$p"; return 0; }
    done
    return 1
}

# run_briefly LABEL COMMAND...: opens the scratch vault for 6 seconds.
run_briefly() {
    local label=$1
    shift
    local log=$T/$label.log pid prog status
    (cd "$T" && exec "${RUN_ENV[@]}" "$@" --close-after 6s "$VAULT") > "$log" 2>&1 &
    pid=$!
    PIDS+=("$pid")
    sleep 3
    if prog=$(program_pid "$pid"); then
        if grep -q libkvitmath.so "/proc/$prog/maps" 2> /dev/null; then
            info "$label: while running it loaded libkvitmath.so"
        else
            info "$label: while running it did not load libkvitmath.so (the program does not use mathtex yet)"
        fi
        # For the AppImage, APPDIR is where the runtime mounted the image.
        local appdir
        appdir=$(tr '\0' '\n' < "/proc/$prog/environ" 2> /dev/null | sed -n 's/^APPDIR=//p')
        if [ -n "$appdir" ]; then
            [ -f "$appdir/usr/lib/kvit-notes/libkvitmath.so" ] &&
                [ -d "$appdir/usr/share/kvit-notes/math-res/fonts" ] &&
                ok "$label: the mounted image ($appdir) has the math library and math-res" ||
                fail "$label: the mounted image ($appdir) lacks the math library or math-res"
        fi
    fi
    status=0
    wait "$pid" || status=$?
    if [ "$status" = 0 ] && grep -q 'first frame after' "$log"; then
        ok "$label: the window drew and the program exited ($(grep 'first frame after' "$log"))"
    else
        fail "$label: exit status $status; output:"
        sed 's/^/          /' "$log"
    fi
}

# self_test LABEL BIN_DIR: the math self-test from the folder of the
# program in an unpacked tree.
PROBE=
self_test() {
    local label=$1 bin=$2 out
    if has_math_selftest "$bin/kvit-notes"; then
        out=$(cd "$T" && "${RUN_ENV[@]}" "$bin/kvit-notes" --math-selftest 2>&1) || true
    else
        if [ -z "$PROBE" ]; then
            PROBE=$T/mathprobe
            math_probe linux amd64 "$PROBE"
        fi
        cp "$PROBE" "$bin/mathprobe"
        out=$(cd "$T" && "${RUN_ENV[@]}" "$bin/mathprobe" 2>&1) || true
        rm -f "$bin/mathprobe"
        label="$label (mathtex.SelfTest through the probe)"
    fi
    if grep -q '^selftest: OK' <<< "$out" && grep -q "^math-lib: $(dirname "$bin")/" <<< "$out"; then
        ok "$label: math self-test passed with the package's own files"
    else
        fail "$label: math self-test:"
    fi
    sed 's/^/          /' <<< "$out"
}

echo "== AppImage: $(basename "$APPIMAGE")"
run_briefly appimage "$APPIMAGE"
if grep -q -i -e 'fuse' -e 'cannot mount' "$T/appimage.log"; then
    info "the AppImage could not be mounted through FUSE; running it again unpacked"
    APPIMAGE_EXTRACT_AND_RUN=1 run_briefly appimage-extracted "$APPIMAGE"
fi
mkdir -p "$T/appimage"
(cd "$T/appimage" && "$APPIMAGE" --appimage-extract > /dev/null)
self_test appimage "$T/appimage/squashfs-root/usr/bin"

echo "== tar.gz: $(basename "$TARBALL")"
mkdir -p "$T/tar"
tar -xzf "$TARBALL" -C "$T/tar"
[ "$(ls "$T/tar")" = "$TREE_NAME" ] && ok "one top folder, $TREE_NAME" || fail "top folders: $(ls "$T/tar")"
run_briefly tar "$T/tar/$TREE_NAME/bin/kvit-notes"
self_test tar "$T/tar/$TREE_NAME/bin"

echo "== AUR: dist/aur/PKGBUILD"
want=$(sha256sum "$TARBALL" | cut -d' ' -f1)
grep -q "^sha256sums=('$want')" "$DIST/aur/PKGBUILD" && ok "sha256sums is the tar.gz's digest" ||
    fail "sha256sums is not the tar.gz's digest $want"
mkdir -p "$T/aur/src" "$T/aur/pkg"
tar -xzf "$TARBALL" -C "$T/aur/src"
(
    set -e
    source "$DIST/aur/PKGBUILD"
    srcdir=$T/aur/src pkgdir=$T/aur/pkg
    cd "$srcdir"
    package
)
for f in usr/bin/kvit-notes usr/lib/kvit-notes/libkvitmath.so usr/share/kvit-notes/math-res/fonts \
         usr/share/applications/kvit-notes.desktop usr/share/icons/hicolor/256x256/apps/kvit-notes.png \
         usr/share/metainfo/org.kvit.Notes.metainfo.xml usr/share/licenses/kvit-notes-bin/LICENSE \
         usr/share/licenses/kvit-notes-bin/THIRD-PARTY-NOTICES.md; do
    [ -e "$T/aur/pkg/$f" ] && ok "package() installs $f" || fail "package() does not install $f"
done
self_test aur "$T/aur/pkg/usr/bin"

leftover=$(pgrep -f -- "$VAULT" || true)
[ -z "$leftover" ] && ok "no program started here is still running" || fail "still running: $leftover"
if [ "$FAILED" != 0 ]; then
    echo "Some Linux checks failed."
    exit 1
fi
echo "All Linux checks passed."
