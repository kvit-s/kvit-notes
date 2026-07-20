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
                -e "s/SKIP/$(printf '\033[33mSKIP\033[0m')/g"
        }

        run_suite() {
            local name="$1"; shift
            echo ""
            echo "=== $name ==="
            "$@" 2>&1 | colorize
            if [ ${PIPESTATUS[0]} -ne 0 ]; then
                FAILED=1
                FAILED_SUITES="$FAILED_SUITES $name"
            fi
        }

        # Run every C++ unit test binary first, then the Qt Quick suites.
        # Discovery is by glob so a new test binary can never be silently
        # left out of the run.
        for t in "$BUILD_DIR"/tests/test_*; do
            [ -f "$t" ] && [ -x "$t" ] || continue
            base="$(basename "$t")"
            case "$base" in
                test_integration|test_visual) ;; # Qt Quick suites run last
                *) run_suite "$base" "$t" ;;
            esac
        done

        run_suite "test_integration (Qt Quick)" \
            "$BUILD_DIR/tests/test_integration" -input "$PROJECT_DIR/tests/tst_integration.qml"
        run_suite "test_visual (Qt Quick storyboard)" \
            "$BUILD_DIR/tests/test_visual" -input "$PROJECT_DIR/tests/tst_visual.qml"

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
