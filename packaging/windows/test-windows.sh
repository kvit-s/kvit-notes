#!/usr/bin/env bash
# Tries the Windows artifacts on this machine's Windows, from WSL:
#   - unzips the portable zip, starts kvit-notes.exe briefly and runs the
#     math self-test in its folder;
#   - builds a test installer from the same staged tree and kvit-notes.iss,
#     with its own AppId, product name ("Kvit Notes Packaging Test") and .md
#     ProgID, installs it silently into a scratch folder, checks the files,
#     the Start-menu shortcut and the association keys, starts the installed
#     program briefly, uninstalls it and checks everything is gone.
#
#   packaging/windows/test-windows.sh
#
# Run packaging/windows/build-windows.sh first; this uses its staged tree and
# zip. The real installer is never run here: on a machine with Kvit Notes
# installed it would replace that installation. The work happens in
# %TEMP%\kvit-notes-packaging-test, which is emptied first and kept afterwards
# for inspection (install.log, the program's output).
set -euo pipefail
source "$(dirname "$0")/../lib.sh"
source packaging/windows/wsl.sh
release_version

STAGE_NAME=Kvit_Notes-$VERSION-windows-x64
STAGE=$WORK/windows/$STAGE_NAME
ZIP=$DIST/$STAGE_NAME.zip
[ -d "$STAGE" ] && [ -f "$ZIP" ] || die "run packaging/windows/build-windows.sh first"

# The test product's identity. It must differ from the real product's in all
# three: AppId (which installation is upgraded or removed), name (the folder
# and Start-menu group) and ProgID (the .md association keys).
TEST_APPID=5E0C6B7A-3D1F-4C2B-9A8E-7F60D1B2C3A4
TEST_NAME="Kvit Notes Packaging Test"
TEST_PROGID=KvitNotesPackagingTest.md
TEST_SETUP=Kvit_Notes-$VERSION-packaging-test-setup

echo "== building the test installer"
mkdir -p "$WORK/windows-test"
ISCC=$(find_iscc)
"$ISCC" /Q \
    "/DKvitVersion=$VERSION" \
    "/DKvitVersionNumeric=$BASE_VERSION" \
    "/DKvitAppId={{$TEST_APPID}" \
    "/DKvitAppName=$TEST_NAME" \
    "/DKvitProgId=$TEST_PROGID" \
    "/DKvitOutputBase=$TEST_SETUP" \
    "/DStageDir=$(win_path "$STAGE")" \
    "/DOutputDir=$(win_path "$WORK/windows-test")" \
    "$(win_path packaging/windows/kvit-notes.iss)" | tr -d '\r'

WIN_WORK="$(win_env TEMP)\\kvit-notes-packaging-test"
work=$(wslpath -u "$WIN_WORK")
rm -rf "$work"
mkdir -p "$work"
cp "$ZIP" "$WORK/windows-test/$TEST_SETUP.exe" packaging/windows/test-windows.ps1 "$work/"

# The math self-test: the program's own --math-selftest when it has one,
# otherwise the probe (packaging/lib.sh, math_probe).
if has_math_selftest "$STAGE/kvit-notes.exe"; then
    SELFTEST=program
else
    SELFTEST=probe
    math_probe windows amd64 "$work/mathprobe.exe"
fi

# The keys of the real installation (the Qt Kvit Notes on the development
# machine): its ProgID, the .md OpenWithProgids values, and its uninstall
# entry. They are recorded with reg.exe before and after the test and must
# not change.
REAL_KEYS=('HKCU\Software\Classes\KvitNotes.md'
           'HKCU\Software\Classes\.md\OpenWithProgids'
           'HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\{7B3D2E1A-9C64-4F58-A2D7-0E5F1B8C6A34}_is1')
record_real_keys() {
    local k
    for k in "${REAL_KEYS[@]}"; do
        echo "## reg.exe query $k /s"
        (cd /mnt/c && reg.exe query "$k" /s 2>&1 || true) | tr -d '\r' | grep -v '^$'
    done
}
record_real_keys > "$work/real-keys-before.txt"

echo "== running the checks on Windows in $WIN_WORK"
status=0
cd /mnt/c
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$WIN_WORK\\test-windows.ps1" \
    -Work "$WIN_WORK" \
    -Zip "$WIN_WORK\\$STAGE_NAME.zip" \
    -Setup "$WIN_WORK\\$TEST_SETUP.exe" \
    -AppId "$TEST_APPID" \
    -AppName "$TEST_NAME" \
    -ProgId "$TEST_PROGID" \
    -SelfTest "$SELFTEST" | tr -d '\r' || status=$?

record_real_keys > "$work/real-keys-after.txt"
echo "== the real installation's keys (reg.exe query), before and after"
sed 's/^/  /' "$work/real-keys-before.txt"
if diff -u "$work/real-keys-before.txt" "$work/real-keys-after.txt"; then
    echo "  unchanged"
else
    echo "  CHANGED (the difference is above)"
    status=1
fi
exit "$status"
