#!/usr/bin/env bash
# Builds kvit-notes-go: every package, and kvit-notes into build/.
#
#   ./build.sh               build
#   ./build.sh --test        also check formatting, run go vet and the headless tests
#   ./build.sh --cross       also build kvit-notes for windows/amd64, darwin/arm64,
#                            darwin/amd64 and linux/amd64 into build/<os>-<arch>/
#   ./build.sh --win         build kvit-notes for Windows onto D: and start it there, on a
#                            copy of the Qt repository's demo vault (not your own notes)
#   ./build.sh --win-check   the same, driving the editor through a scripted check in
#                            its window, reading what Windows' screen-reader interface
#                            reports, saving a picture of the window and its memory
#   ./build.sh --shots       run the scenarios, write their screenshots into build/shots,
#                            and stack each image above Kvit's of the same name into
#                            build/shots/compare
#   ./build.sh --bench       time opening, scrolling and typing in 1,237 blocks
#   ./build.sh --run         start kvit-notes here (needs a display)
#
# Everything builds with cgo off. KVIT_WIN_DIR overrides where Windows builds go
# (default /mnt/d/projects/kvit-notes-go); KVIT_QT_SHOTS where Kvit's storyboard
# screenshots are (default ~/kvit-qt-reference/kvit-notes-storyboards); KVIT_QT_REPO
# where Kvit's Qt repository is, whose documentation --bench reads (default ~/kvit-notes).
set -euo pipefail
cd "$(dirname "$0")"
export CGO_ENABLED=0
test=0 cross=0 win=0 check=0 run=0 shots=0 bench=0
for a in "$@"; do
    case $a in
        --test) test=1 ;;
        --cross) cross=1 ;;
        --win) win=1 ;;
        --win-check) win=1 check=1 ;;
        --shots) shots=1 ;;
        --bench) bench=1 ;;
        --run) run=1 ;;
        -h|--help) sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $a" >&2; exit 2 ;;
    esac
done

mkdir -p build
go build ./...
go build -o build/kvit-notes ./cmd/kvit-notes

if [ $test = 1 ]; then
    unformatted=$(gofmt -l .)
    if [ -n "$unformatted" ]; then
        echo "not formatted with gofmt:" >&2
        echo "$unformatted" >&2
        exit 1
    fi
    go vet ./...
    go test ./...
fi

if [ $cross = 1 ]; then
    for target in windows/amd64 darwin/arm64 darwin/amd64 linux/amd64; do
        os=${target%/*} arch=${target#*/} ext=
        [ "$os" = windows ] && ext=.exe
        GOOS=$os GOARCH=$arch go build -o "build/$os-$arch/kvit-notes$ext" ./cmd/kvit-notes
    done
fi

if [ $win = 1 ]; then
    dest=${KVIT_WIN_DIR:-/mnt/d/projects/kvit-notes-go}
    mkdir -p "$dest"
    GOOS=windows GOARCH=amd64 go build -o "$dest/kvit-notes.exe" ./cmd/kvit-notes
    if [ $check = 0 ]; then
        # A copy of the Qt app's demo vault, so trying the build never touches
        # the vault you write in; kvit-notes.exe with no argument opens that.
        demo=${KVIT_QT_REPO:-$HOME/kvit-notes}/screenshots/demo-vault
        [ -d "$dest/demo-vault" ] || cp -r "$demo" "$dest/demo-vault"
        "$dest/kvit-notes.exe" "$(wslpath -w "$dest/demo-vault")" &
        disown
    else
        cp tools/win-check.ps1 "$dest/"
        winDest=$(wslpath -w "$dest")
        # First as the app runs, with unison's OpenGL renderer: the scripted
        # check, what the screen-reader interface reports, and memory.
        "$dest/kvit-notes.exe" --check 12s &
        pid=$!
        sleep 5
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$winDest\\win-check.ps1" \
            -Title "Kvit Notes check" | tr -d '\r'
        powershell.exe -NoProfile -Command '
            $p = Get-Process kvit-notes
            $c = (Get-Counter "\Process(kvit-notes)\Working Set - Private").CounterSamples[0].CookedValue
            "Windows memory: working set {0:N0} MB, private working set {1:N0} MB, private bytes {2:N0} MB" -f ($p.WorkingSet64 / 1MB), ($c / 1MB), ($p.PrivateMemorySize64 / 1MB)' | tr -d '\r'
        wait $pid
        # Then with unison's software renderer, for a picture of the window:
        # nothing reads back what the OpenGL renderer put on the screen.
        echo "== again with the software renderer, for a picture"
        UNISON_CPU_RENDERING=1 WSLENV=UNISON_CPU_RENDERING "$dest/kvit-notes.exe" --check 8s | grep -E 'first frame|check:|FAIL' &
        pid=$!
        sleep 4
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$winDest\\win-check.ps1" \
            -Title "Kvit Notes check" -Shot "$winDest\\check.png" | tr -d '\r' | grep saved
        wait $pid
        cp "$dest/check.png" build/win-check.png && echo "window picture: build/win-check.png"
    fi
fi

if [ $shots = 1 ]; then
    qt=${KVIT_QT_SHOTS:-$HOME/kvit-qt-reference/kvit-notes-storyboards}
    rm -rf build/shots
    compare=()
    [ -d "$qt" ] && compare=(--compare "$qt")
    build/kvit-notes --scenario all --out build/shots "${compare[@]}"
fi

if [ $bench = 1 ]; then
    build/kvit-notes --bench "${KVIT_QT_REPO:-$HOME/kvit-notes}"
fi

if [ $run = 1 ]; then
    build/kvit-notes
fi
