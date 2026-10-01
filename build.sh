#!/usr/bin/env bash
# Builds every package of kvit-notes, and the program into build/kvit-notes.
#
#   ./build.sh               build
#   ./build.sh --test        also check formatting, run go vet and the headless tests
#   ./build.sh --cross       also build kvit-notes for windows/amd64, darwin/arm64,
#                            darwin/amd64 and linux/amd64 into build/<os>-<arch>/
#   ./build.sh --win         build kvit-notes for Windows onto D: and start it there, on an
#                            empty notes folder beside the build, try-vault (not your
#                            own notes)
#   ./build.sh --win-check   the same, driving the editor through a scripted check in
#                            its window, reading what Windows' screen-reader interface
#                            reports, saving a picture of the window and its memory
#   ./build.sh --shots       run the scenarios and write their screenshots into build/shots
#   ./build.sh --bench       time opening, scrolling and typing in 1,237 blocks of Kvit
#                            Notes' documentation, read from the folder KVIT_BENCH_VAULT names
#   ./build.sh --run         start kvit-notes here (needs a display)
#
# LaTeX math is drawn by MicroTeX, a C++ engine built as a shared library the
# program loads at run time (tools/build-mathlib.sh). Building it needs zig on
# the PATH (https://ziglang.org/download/), whose C++ compiler builds all four
# platforms' libraries here; it is rebuilt only when its sources change. Every
# build puts the library, and the math-res folder of fonts it reads, beside
# the program: build/libkvitmath.so here (the program finds the resources in
# third_party/microtex/res), kvitmath.dll or libkvitmath.dylib and math-res in
# each build/<os>-<arch>/ with --cross, and beside kvit-notes.exe with --win.
# Without zig a plain build leaves the library out, with a warning, and the
# program shows TeX as its source; --cross and --win stop, because what they
# build is what gets shipped.
#
# Everything builds with cgo off. KVIT_WIN_DIR overrides where Windows builds go
# (default /mnt/d/projects/kvit-notes). KVIT_BENCH_VAULT is the notes folder
# --bench reads, holding features.md, block-arch.md, selection.md, devel.md and
# accessibility.md from the tag v1.0.0; it has no default, and --bench stops without it.
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
        -h|--help) sed -n '2,34p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $a" >&2; exit 2 ;;
    esac
done
if [ $bench = 1 ] && [ -z "${KVIT_BENCH_VAULT:-}" ]; then
    echo "build.sh: --bench needs KVIT_BENCH_VAULT set to a notes folder holding Kvit Notes'" \
        "documentation (features.md, block-arch.md, selection.md, devel.md, accessibility.md" \
        "from the tag v1.0.0)" >&2
    exit 1
fi

# mathres copies the math library's resources into a folder beside a program.
mathres() {
    mkdir -p "$1/math-res"
    if command -v rsync >/dev/null; then
        rsync -a --delete third_party/microtex/res/ "$1/math-res/"
    else
        rm -rf "$1/math-res" && cp -R third_party/microtex/res "$1/math-res"
    fi
}

mkdir -p build
go build ./...
go build -o build/kvit-notes ./cmd/kvit-notes
case "$(go env GOOS)/$(go env GOARCH)" in
    linux/amd64) lib=build/libkvitmath.so ;;
    darwin/arm64 | darwin/amd64) lib=build/libkvitmath.dylib ;;
    *) lib= ;;
esac
if [ -n "$lib" ]; then
    if command -v zig >/dev/null; then
        tools/build-mathlib.sh "$(go env GOOS)/$(go env GOARCH)" "$lib"
    else
        echo "build.sh: zig is not on the PATH, so the math library is not built:" \
            "the program shows TeX as its source and the math tests skip (see tools/build-mathlib.sh)" >&2
    fi
fi

if [ $test = 1 ]; then
    # The toolchain's own gofmt, which reads the Go version go.mod asks for,
    # over the repository's Go files, tracked or new; ignored build output
    # such as the packages' staged sources under build/ is left out.
    unformatted=$(git ls-files -z --cached --others --exclude-standard -- '*.go' |
        xargs -0 "$(go env GOROOT)/bin/gofmt" -l)
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
        lib=libkvitmath.so
        [ "$os" = windows ] && lib=kvitmath.dll
        [ "$os" = darwin ] && lib=libkvitmath.dylib
        tools/build-mathlib.sh "$target" "build/$os-$arch/$lib"
        mathres "build/$os-$arch"
    done
fi

if [ $win = 1 ]; then
    dest=${KVIT_WIN_DIR:-/mnt/d/projects/kvit-notes}
    mkdir -p "$dest"
    GOOS=windows GOARCH=amd64 go build -o "$dest/kvit-notes.exe" ./cmd/kvit-notes
    tools/build-mathlib.sh windows/amd64 "$dest/kvitmath.dll"
    mathres "$dest"
    if [ $check = 0 ]; then
        # An empty notes folder beside the build, kept between runs, so trying
        # the build never touches the notes you write in.
        mkdir -p "$dest/try-vault"
        "$dest/kvit-notes.exe" "$(wslpath -w "$dest/try-vault")" &
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
    rm -rf build/shots
    build/kvit-notes --scenario all --out build/shots
fi

if [ $bench = 1 ]; then
    build/kvit-notes --bench "$KVIT_BENCH_VAULT"
fi

if [ $run = 1 ]; then
    build/kvit-notes
fi
