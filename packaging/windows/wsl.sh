# Sourced by the Windows packaging scripts, which run in WSL and start
# Windows programs directly (WSL runs a Windows .exe like a Linux program).
#
#   win_env NAME   prints a Windows environment variable, such as LOCALAPPDATA
#   win_path PATH  prints the Windows form of a WSL path
#   find_iscc      prints the WSL path of Inno Setup's compiler, ISCC.exe

win_env() {
    # cmd.exe warns when started from a Linux folder, so it starts from C:.
    (cd /mnt/c && cmd.exe /d /c "echo %$1%") | tr -d '\r'
}

win_path() {
    wslpath -w "$1"
}

# Inno Setup 6, looked for in this order after an explicit
# KVIT_ISCC (a WSL or Windows path): ISCC.exe on the PATH; the two
# machine-wide Program Files folders; the per-user install that Inno's own
# installer and winget make without administrator rights,
# %LOCALAPPDATA%\Programs\Inno Setup 6, which is where it is on the
# development machine.
find_iscc() {
    local c
    if [ -n "${KVIT_ISCC:-}" ]; then
        c=$KVIT_ISCC
        [[ $c == [A-Za-z]:* ]] && c=$(wslpath -u "$c")
        [ -f "$c" ] || { echo "KVIT_ISCC=$KVIT_ISCC does not exist" >&2; return 1; }
        echo "$c"
        return 0
    fi
    c=$(command -v ISCC.exe || command -v iscc.exe || true)
    if [ -n "$c" ]; then
        echo "$c"
        return 0
    fi
    for c in "C:\\Program Files (x86)\\Inno Setup 6\\ISCC.exe" \
             "C:\\Program Files\\Inno Setup 6\\ISCC.exe" \
             "$(win_env LOCALAPPDATA)\\Programs\\Inno Setup 6\\ISCC.exe"; do
        c=$(wslpath -u "$c")
        if [ -f "$c" ]; then
            echo "$c"
            return 0
        fi
    done
    echo "Inno Setup 6 (ISCC.exe) was not found; install it per user from" \
         "https://jrsoftware.org/isdl.php (or: winget install JRSoftware.InnoSetup)," \
         "or set KVIT_ISCC to its ISCC.exe" >&2
    return 1
}
