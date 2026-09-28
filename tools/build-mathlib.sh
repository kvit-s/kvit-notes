#!/usr/bin/env bash
# Builds kvitmath, the MicroTeX math engine as a shared library, which the Go
# package mathtex loads at run time and calls without cgo.
#
#   tools/build-mathlib.sh <os>/<arch> <output file>...
#
# <os>/<arch> is one of linux/amd64, windows/amd64, darwin/arm64 and
# darwin/amd64; the library is copied to each output file given. The engine is
# about 40,000 lines of C++ (third_party/microtex) and the recording back end
# with its C interface (mathtex/native), and compiling it takes a while, so it
# is rebuilt only when one of those files, this script or the compiler has
# changed: the build keeps a checksum of them in build/mathlib/<os>-<arch>/.
#
# Needs zig on the PATH (https://ziglang.org/download/; 0.16.0 is the version
# this was written with). zig's C++ compiler, `zig c++`, carries the C and C++
# libraries of every target, so all four libraries build on Linux with no
# other cross compiler. libc++ is linked into the library, so it depends only
# on the system's C library: glibc 2.28 or later on Linux, the C runtime every
# Windows 10 has, and libSystem on macOS 12 or later.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ $# -lt 2 ]; then
    sed -n '4,5p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
fi
target=$1
shift

case $target in
    linux/amd64) triple=x86_64-linux-gnu.2.28 lib=libkvitmath.so ;;
    windows/amd64) triple=x86_64-windows-gnu lib=kvitmath.dll ;;
    darwin/arm64) triple=aarch64-macos.12.0 lib=libkvitmath.dylib ;;
    darwin/amd64) triple=x86_64-macos.12.0 lib=libkvitmath.dylib ;;
    *) echo "build-mathlib: unknown target $target" >&2; exit 2 ;;
esac

if ! command -v zig >/dev/null; then
    echo "build-mathlib: zig is not on the PATH; it builds the math library (see tools/build-mathlib.sh)" >&2
    exit 1
fi

engine=third_party/microtex
native=mathtex/native
work=build/mathlib/${target/\//-}
mkdir -p "$work"

# One build at a time: two build.sh runs may ask for the same library at
# once. flock is Linux's; where it is missing the builds are not serialised.
exec 9>"$work/.lock"
if command -v flock >/dev/null; then flock 9; fi

# GNU tools here, and their BSD counterparts on macOS.
sha=(sha256sum)
command -v sha256sum >/dev/null || sha=(shasum -a 256)
jobs=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

flags=(-target "$triple" -std=c++20 -O2 -fvisibility=hidden -fvisibility-inlines-hidden
       -ffunction-sections -fdata-sections -DNDEBUG -w -I"$engine/src" -I"$engine/tinyxml2" -I"$native")
[ "$target" = windows/amd64 ] || flags+=(-fPIC)

# The generated NewTX metric tables under res/font/newtx-generated are
# #included by the kvit_newtx_* definitions and never compiled on their own.
sources=()
while IFS= read -r f; do sources+=("$f"); done < <(find "$engine/src" "$engine/tinyxml2" "$native" \
    -name '*.cpp' -not -path '*/newtx-generated/*' | sort)

sum=$( {
    echo "$triple ${flags[*]}"
    zig version
    "${sha[@]}" "$0"
    find "$engine/src" "$engine/tinyxml2" "$native" -type f -print0 | sort -z | xargs -0 "${sha[@]}"
} | "${sha[@]}" | cut -d' ' -f1)

if [ ! -f "$work/$lib" ] || [ "$(cat "$work/stamp" 2>/dev/null)" != "$sum" ]; then
    echo "building the math library for $target (${#sources[@]} C++ files)"
    start=$SECONDS
    rm -rf "$work/obj" "$work/out"
    mkdir -p "$work/obj" "$work/out"
    printf '%s\n' "${sources[@]}" | xargs -P "$jobs" -I{} sh -c '
        o="$0/obj/$(echo "{}" | tr / _).o"
        zig c++ "$@" -c "{}" -o "$o" || exit 255' "$work" "${flags[@]}"
    # Only the C interface is exported: tinyxml2 marks its classes visible,
    # and libc++ its operators new and delete.
    case $target in
        linux/*)
            printf '{ global: kvitmath_*; local: *; };\n' > "$work/out/exports.map"
            link=(-target "$triple" -shared -s -Wl,--gc-sections -Wl,--version-script="$work/out/exports.map") ;;
        darwin/*)
            printf '_kvitmath_*\n' > "$work/out/exports.txt"
            link=(-target "$triple" -shared -s -Wl,-dead_strip -Wl,-install_name,@rpath/$lib
                  -Wl,-exported_symbols_list,"$work/out/exports.txt") ;;
        windows/*)
            link=(-target "$triple" -shared -s -Wl,--gc-sections) ;;
    esac
    zig c++ "${link[@]}" -o "$work/out/$lib" "$work"/obj/*.o
    mv "$work/out/$lib" "$work/$lib"
    echo "$sum" > "$work/stamp"
    echo "built $work/$lib in $((SECONDS - start)) s"
fi

for out in "$@"; do
    mkdir -p "$(dirname "$out")"
    cmp -s "$work/$lib" "$out" 2>/dev/null || cp "$work/$lib" "$out"
done
