# Shared by the packaging scripts, which source it. It moves to the
# repository root and defines:
#
#   release_version       sets VERSION (the full release version, such as
#                         2.0.0 or 2.0.0-rc1) and BASE_VERSION
#   go_build OS ARCH OUT  builds the release executable
#   math_library OS/ARCH OUT
#                         puts the math library at OUT; returns 1 when the
#                         math library cannot be built yet
#   stage_math_res DIR    copies the math resources to DIR
#   stage_licenses DIR ARTIFACT MATH EXE...
#                         copies the notices and licence texts into DIR
#   check_manifest TREE MANIFEST [PREFIX [EXCLUDE]]
#                         fails when TREE's files differ from the manifest
#   has_math_selftest EXE, math_probe OS ARCH OUT
#                         run the math self-test in an unpacked package
#   fetch_verified URL SHA256
#                         downloads a build tool into packaging/.tools once
#                         and checks its digest
#   zip_tree, tar_tree, sha256_lines
#                         archives with fixed times and order, and checksums
#
# and the paths DIST (dist/, the finished artifacts), WORK (build/packaging/,
# staging trees) and TOOLS (packaging/.tools/, downloaded tools). dist/ and
# packaging/.tools/ are in .gitignore; build/ already was.
#
# It is written for bash 3.2 as well, the version macOS has, because
# packaging/macos/build-macos.sh runs on a Mac too; the functions that use
# GNU tools (sha256sum, GNU tar and touch) are the Linux-only ones.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO=$PWD
DIST=$REPO/dist
WORK=$REPO/build/packaging
TOOLS=$REPO/packaging/.tools
MATHLIB_SCRIPT=tools/build-mathlib.sh
export CGO_ENABLED=0

die() {
    echo "packaging: $*" >&2
    exit 1
}

# ── Release version
#
# The rules, in order:
#   1. KVIT_VERSION_FULL, when set;
#   2. otherwise the tag being built: GITHUB_REF_NAME in a GitHub tag job, or
#      a v<version> tag on the checked-out commit (git describe);
#   3. otherwise the base version, for a local untagged build.
# The base version is the baseVersion constant in cmd/kvit-notes/version.go.
# The version must be SemVer, and its three numbers must be the base version, so
# a tag and the source cannot disagree about what is being released.
release_version() {
    BASE_VERSION=$(sed -n 's/^const baseVersion = "\([0-9]*\.[0-9]*\.[0-9]*\)"$/\1/p' \
        cmd/kvit-notes/version.go)
    [ -n "$BASE_VERSION" ] || die "could not read baseVersion from cmd/kvit-notes/version.go"
    local tag=
    if [ -n "${KVIT_VERSION_FULL:-}" ]; then
        VERSION=$KVIT_VERSION_FULL
        VERSION_SOURCE=KVIT_VERSION_FULL
    else
        if [ "${GITHUB_REF_TYPE:-}" = tag ]; then
            tag=$GITHUB_REF_NAME
        fi
        [ -n "$tag" ] || tag=$(git describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null || true)
        if [ -n "$tag" ]; then
            VERSION=${tag#v}
            VERSION_SOURCE="tag $tag"
        else
            VERSION=$BASE_VERSION
            VERSION_SOURCE="cmd/kvit-notes/version.go (untagged build)"
        fi
    fi
    local semver='^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$'
    [[ $VERSION =~ $semver ]] ||
        die "version '$VERSION' (from $VERSION_SOURCE) is not SemVer; tags look like v2.0.0 or v2.0.0-rc1"
    [[ $VERSION == "$BASE_VERSION" || $VERSION == "$BASE_VERSION"[-+]* ]] ||
        die "version '$VERSION' (from $VERSION_SOURCE) does not start with the base version" \
            "$BASE_VERSION in cmd/kvit-notes/version.go; change baseVersion or the tag"
    export VERSION BASE_VERSION
    echo "Version $VERSION (from $VERSION_SOURCE)"
}

# ── The executable
#
# -trimpath keeps this machine's paths out of the executable, -s -w leave out
# the symbol table and debugging information, and -X sets the version the
# program reports (cmd/kvit-notes/version.go). On Windows -H windowsgui makes
# it a window program, so starting it from Explorer opens no console window.
go_build() {
    local os=$1 arch=$2 out=$3
    local ldflags="-s -w -X main.version=$VERSION"
    [ "$os" = windows ] && ldflags+=" -H windowsgui"
    mkdir -p "$(dirname "$out")"
    GOOS=$os GOARCH=$arch go build -trimpath -ldflags "$ldflags" -o "$out" ./cmd/kvit-notes
}

# ── The math library and its resources
#
# The library is built by the math agent's script, which caches its result
# per target under build/mathlib/. Until that script exists the packages are
# made without the library and without math-res, and the scripts say so;
# KVIT_REQUIRE_MATH=1 turns that into an error, for release builds.
math_library() {
    local target=$1 out=$2
    if [ ! -f "$MATHLIB_SCRIPT" ]; then
        [ "${KVIT_REQUIRE_MATH:-0}" = 1 ] && die "$MATHLIB_SCRIPT does not exist and KVIT_REQUIRE_MATH=1"
        echo "NOTE: $MATHLIB_SCRIPT does not exist yet; packaging without the math library and math-res" >&2
        return 1
    fi
    bash "$MATHLIB_SCRIPT" "$target" "$out"
}

stage_math_res() {
    local dest=$1
    mkdir -p "$dest"
    cp -R third_party/microtex/res/. "$dest/"
}

# ── Licences
#
# ARTIFACT is windows, macos, linux or appimage (packaging/sbom.yaml's
# ships_in names); MATH is 1 when the artifact contains the math library.
# The notices file is regenerated first and must match sbom.yaml.
stage_licenses() {
    local dest=$1 artifact=$2 math=$3
    shift 3
    local flag=
    [ "$math" = 1 ] && flag=--math
    python3 packaging/sbom.py notices --check
    python3 packaging/sbom.py licenses "$dest" "$artifact" $flag "$@"
}

# ── Manifests
#
# A manifest lists every file an artifact contains, one path per line,
# relative to the artifact's top folder (# starts a comment). The build fails
# when the staged tree has a file the manifest does not list, or lacks one it
# does, so nothing reaches a package unnoticed. Lines of the math library and
# math-res (those containing "kvitmath" or "math-res/" or "math-runtime/")
# are skipped while the math library cannot be built.
#
# PREFIX, when given, checks only the manifest lines under that folder, with
# it removed, and EXCLUDE leaves out the lines containing it: the Linux
# tar.gz is the usr/ folder of the AppImage's tree without the AppImage
# runtime's licences.
check_manifest() {
    local tree=$1 manifest=$2 prefix=${3:-} exclude=${4:-}
    local want have
    want=$(mktemp) have=$(mktemp)
    # (grep exits with 1 when it selects no line, so each grep has || true.)
    { grep -v -e '^#' -e '^$' "$manifest" || true; } |
        { if [ -n "$prefix" ]; then sed -n "s#^$prefix/##p"; else cat; fi; } |
        { if [ -n "$exclude" ]; then grep -v -F -e "$exclude" || true; else cat; fi; } |
        { if [ "${MATH:-1}" = 1 ]; then cat; else grep -v -e kvitmath -e 'math-res/' -e 'math-runtime/' || true; fi; } |
        LC_ALL=C sort > "$want"
    (cd "$tree" && find . \( -type f -o -type l \) | sed 's#^\./##' | LC_ALL=C sort) > "$have"
    # KVIT_UPDATE_MANIFESTS=1 rewrites the manifest from the staged tree
    # instead, keeping its leading comment; review the change with git diff.
    if [ "${KVIT_UPDATE_MANIFESTS:-0}" = 1 ] && [ -z "$prefix" ] && [ "${MATH:-1}" = 1 ]; then
        { sed -n '/^#/!q;p' "$manifest"; cat "$have"; } > "$manifest.new"
        mv "$manifest.new" "$manifest"
        cp "$have" "$want"
        echo "  rewrote ${manifest#"$REPO"/} from $tree"
    fi
    if ! diff -u --label "manifest ${manifest#"$REPO"/}" --label "staged $tree" "$want" "$have"; then
        rm -f "$want" "$have"
        die "the files in $tree differ from ${manifest#"$REPO"/} (+ is a file the manifest" \
            "does not list, - is one missing); change the manifest if the difference is intended"
    fi
    echo "  $(wc -l < "$have") files, as ${manifest#"$REPO"/} lists"
    rm -f "$want" "$have"
}

# ── The math self-test
#
# The test scripts check that a package's layout lets the program find the
# math library and its resources, by running the self-test of the Go package
# mathtex (mathtex.SelfTest, which kvit-notes --math-selftest runs)
# from inside the unpacked package. When the program offers it as
# --math-selftest, that is used. Until then, math_probe builds a one-line
# program that calls it, in its own module under build/packaging/mathprobe
# which refers to this checkout, so nothing in the repository changes; the
# probe is put beside kvit-notes in the unpacked package and run from there.
#
# has_math_selftest EXE      true when EXE's --help lists --math-selftest
# math_probe OS ARCH OUT     builds the probe
has_math_selftest() {
    local help
    help=$("$@" --help 2>&1 || true)
    grep -q -e '-math-selftest' <<< "$help"
}

math_probe() {
    local os=$1 arch=$2 out=$3 dir=$WORK/mathprobe
    [ -f mathtex/locate.go ] && grep -q '^func SelfTest' mathtex/locate.go ||
        die "the mathtex package has no SelfTest yet"
    rm -rf "$dir"
    mkdir -p "$dir"
    # The checkout's module requirements and replacements, with the
    # replacement paths made absolute, and the checkout itself as the module
    # the probe imports.
    {
        echo "module kvit-notes-mathprobe"
        sed -n -e '/^go /p' -e '/^toolchain /p' go.mod
        echo "require github.com/kvit-s/kvit-notes v0.0.0"
        echo "replace github.com/kvit-s/kvit-notes => $REPO"
        sed -n 's#^replace \(.*\) => \(\.\./.*\)$#\1 \2#p' go.mod | while read -r mod path; do
            echo "replace $mod => $(cd "$REPO/$path" && pwd)"
        done
    } > "$dir/go.mod"
    cp go.sum "$dir/go.sum"
    cat > "$dir/main.go" <<'EOF'
// Command mathprobe runs the math self-test from the folder it is put in.
package main

import (
	"os"

	"github.com/kvit-s/kvit-notes/mathtex"
)

func main() { os.Exit(mathtex.SelfTest(os.Stdout)) }
EOF
    (cd "$dir" && GOFLAGS=-mod=mod GOOS=$os GOARCH=$arch go build -o "$out" .)
}

# ── Downloaded build tools
#
# A URL is not a content pin: a release asset can be replaced after it was
# published. Each tool is checked against the digest recorded in the script
# that uses it before it is made executable, and a mismatch stops the build
# and removes the file. To update a tool, change its URL and digest together.
fetch_verified() {
    local url=$1 want=$2
    local f
    f="$TOOLS/$(basename "$url")"
    mkdir -p "$TOOLS"
    [ -f "$f" ] || curl -fsSL "$url" -o "$f"
    local got
    got=$(sha256sum "$f" | cut -d' ' -f1)
    if [ "$got" != "$want" ]; then
        rm -f "$f"
        die "digest mismatch for $(basename "$url"): expected $want, got $got"
    fi
    chmod +x "$f"
}

# ── Archives
#
# Every file put into a zip or tar.gz gets the time of the commit being
# packaged (SOURCE_DATE_EPOCH), and the entries are sorted by name, so
# packaging the same commit again gives the same archive.
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct 2>/dev/null || date +%s)}
export SOURCE_DATE_EPOCH

# zip_tree DIR NAME OUT zips the folder DIR/NAME into OUT, with NAME as the
# one folder at the top of the zip. Symbolic links are stored as links and
# Unix permissions are kept, which the macOS app needs.
zip_tree() {
    local dir=$1 name=$2 out=$3
    rm -f "$out"
    find "$dir/$name" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
    (cd "$dir" && find "$name" | LC_ALL=C sort | zip -q -X -y -@ "$out")
}

# tar_tree DIR NAME OUT does the same as a gzip-compressed tar.
tar_tree() {
    local dir=$1 name=$2 out=$3
    rm -f "$out"
    tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
        -C "$dir" -cf - "$name" | gzip -n -9 > "$out"
}

# sha256_lines FILE... prints "<sha256>  <name>" for each file, the format
# sha256sum -c reads, with the file's name only.
sha256_lines() {
    local f
    for f in "$@"; do
        echo "$(sha256sum "$f" | cut -d' ' -f1)  $(basename "$f")"
    done
}
