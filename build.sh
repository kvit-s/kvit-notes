#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

# Find Qt installation
if [ -d "$HOME/Qt" ]; then
    QT_VERSION=$(ls "$HOME/Qt" | grep -E '^6\.' | sort -V | tail -1)
    if [ -n "$QT_VERSION" ]; then
        QT_PATH="$HOME/Qt/$QT_VERSION/gcc_64"
    fi
fi

# Parse arguments
CLEAN=0
RUN_TESTS=0
RUN_APP=0
HEADLESS=0
SHOW_SHOTS=0

for arg in "$@"; do
    case $arg in
        --clean) CLEAN=1 ;;
        --test) RUN_TESTS=1 ;;
        --run) RUN_APP=1 ;;
        --headless) HEADLESS=1 ;;
        --shots) SHOW_SHOTS=1 ;;
        --qt=*) QT_PATH="${arg#*=}" ;;
        --help)
            echo "Usage: ./build.sh [options]"
            echo "Options:"
            echo "  --clean     Clean build directory before building"
            echo "  --test      Run tests after building"
            echo "  --run       Run application after building"
            echo "  --headless  Run tests without display (smoke test only —"
            echo "              focus-dependent tests skip; never the acceptance gate)"
            echo "  --shots     Print the screenshot directory listing after tests"
            echo "  --qt=PATH   Specify Qt installation path"
            exit 0
            ;;
    esac
done

# Clean if requested
if [ "$CLEAN" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Run build in subshell to preserve working directory
(
    cd "$BUILD_DIR"

    # Configure
    CMAKE_ARGS=""
    if [ -n "$QT_PATH" ]; then
        echo "Using Qt from: $QT_PATH"
        CMAKE_ARGS="-DCMAKE_PREFIX_PATH=$QT_PATH"
    fi

    cmake .. $CMAKE_ARGS

    # Build
    make -j$(nproc)

    echo ""
    echo "Build complete!"

    # Run tests if requested
    if [ "$RUN_TESTS" -eq 1 ]; then
        echo ""
        if [ "$HEADLESS" -eq 1 ]; then
            echo "Running tests (headless)..."
            export QT_QPA_PLATFORM=offscreen
        else
            echo "Running tests..."
        fi

        # Screenshot directory: wiped at the start of each run so a
        # directory listing reads as this run's storyboard
        SHOT_DIR="$BUILD_DIR/screenshots"
        rm -rf "$SHOT_DIR"
        mkdir -p "$SHOT_DIR"
        export KVIT_SHOT_DIR="$SHOT_DIR"

        FAILED=0
        FAILED_SUITES=""

        # Color filter for test output
        colorize() {
            sed -e "s/PASS/$(printf '\033[32mPASS\033[0m')/g" \
                -e "s/FAIL/$(printf '\033[31mFAIL\033[0m')/g" \
                -e "s/SKIP/$(printf '\033[33mSKIP\033[0m')/g" \
                -e "s/Passed/$(printf '\033[32mPassed\033[0m')/g" \
                -e "s/\*\*\*Failed/$(printf '\033[31m***Failed\033[0m')/g" \
                -e "s/\*\*\*Timeout/$(printf '\033[31m***Timeout\033[0m')/g"
        }

        # Every suite runs through CTest rather than by invoking the test
        # binaries directly.
        #
        # The timeouts are the reason. tests/CMakeLists.txt sets TIMEOUT 600
        # on IntegrationTests and 300 on VisualTests and ShellTests, because a
        # QML load error leaves the Qt Quick Test harness waiting forever on
        # its `when:` condition - that hung a run for hours on 2026-07-07.
        # Invoking the binaries directly bypassed those properties entirely,
        # so the local path had no protection against exactly the hang the
        # timeouts were added for. CTest also applies its own default bound to
        # every other entry, and reports which suites timed out.
        #
        # The glob that used to drive this run had one virtue worth keeping:
        # a newly added test binary could not be silently left out. CTest
        # discovers registered tests instead, so an executable built but never
        # passed to add_test() would now be skipped. The check below closes
        # that gap by comparing the binaries on disk against the ones CTest
        # knows how to run, and fails loudly when they disagree.
        UNREGISTERED=$(ctest --test-dir "$BUILD_DIR" --show-only=json-v1 2>/dev/null \
            | python3 -c '
import glob, json, os, sys
try:
    data = json.load(sys.stdin)
except ValueError:
    sys.exit(0)   # no CTest metadata; the run below reports the real problem
registered = {os.path.basename(t["command"][0])
              for t in data.get("tests", []) if t.get("command")}
built = {os.path.basename(p) for p in glob.glob(sys.argv[1] + "/tests/test_*")
         if os.path.isfile(p) and os.access(p, os.X_OK)}
print(" ".join(sorted(built - registered)))
' "$BUILD_DIR")

        if [ -n "$UNREGISTERED" ]; then
            echo ""
            echo -e "\033[31mTest binaries not registered with CTest:\033[0m$UNREGISTERED"
            echo "Add an add_test() entry in tests/CMakeLists.txt, or they run nowhere."
            FAILED=1
            FAILED_SUITES="$FAILED_SUITES unregistered-binaries"
        fi

        echo ""
        echo "=== ctest ($BUILD_DIR) ==="
        ctest --test-dir "$BUILD_DIR" --output-on-failure 2>&1 | colorize
        if [ ${PIPESTATUS[0]} -ne 0 ]; then
            FAILED=1
            FAILED_SUITES="$FAILED_SUITES ctest"
        fi

        if [ "$SHOW_SHOTS" -eq 1 ]; then
            echo ""
            echo "Screenshots: $SHOT_DIR"
            ls -1 "$SHOT_DIR" 2>/dev/null | sed 's/^/  /'
        fi

        echo ""
        if [ "$FAILED" -eq 0 ]; then
            echo -e "\033[32mAll tests passed!\033[0m"
        else
            echo -e "\033[31mFailed suites:$FAILED_SUITES\033[0m"
            exit 1
        fi
    fi

    # Run app if requested
    if [ "$RUN_APP" -eq 1 ]; then
        echo ""
        echo "Starting kvit-notes..."
        # Under WSL, GPU GL through the d3d12 Gallium driver corrupts Qt
        # Quick glyph rendering on this machine (white text turns #ffff00,
        # some glyphs lose alpha entirely; worst on the Intel iGPU).
        # llvmpipe, Mesa's software GL, renders pixel-correct and carried
        # the performance-plan numbers, so it is forced here — including
        # over a stale GALLIUM_DRIVER=d3d12 inherited from an old shell.
        # Native Linux keeps hardware GL, and an explicit
        # LIBGL_ALWAYS_SOFTWARE / QT_QUICK_BACKEND override is respected.
        if [ -z "$LIBGL_ALWAYS_SOFTWARE" ] && [ -z "$QT_QUICK_BACKEND" ] \
           && grep -qiE "microsoft|wsl" /proc/version 2>/dev/null; then
            export GALLIUM_DRIVER=llvmpipe
        fi
        ./kvit-notes
    fi
)
