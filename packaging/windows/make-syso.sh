#!/usr/bin/env bash
# Writes cmd/kvit-notes/rsrc_windows_amd64.syso: the Windows resources of
# kvit-notes.exe, which are its icon (packaging/icons/kvit.ico), its version
# information (what Explorer shows under Properties > Details) and its
# application manifest (packaging/windows/kvit-notes.manifest, which says
# what each setting in it does).
#
#   packaging/windows/make-syso.sh [version]
#
# The version defaults to baseVersion in cmd/kvit-notes/version.go; the
# release script passes the full release version. The go command links a
# .syso file in a package's folder into every windows/amd64 build of that
# package, so ./build.sh --win and `go build` get the icon and manifest too.
#
# The tool is go-winres (github.com/tc-hib/go-winres), run with `go run` at a
# pinned version, so it needs nothing installed and leaves go.mod alone. It is
# written in Go and runs on Linux, so the file is made in WSL like every
# other build step. unison's own packaging tool, upack, also writes these
# resources, but it is built only for Windows (its resource code is in
# rsrc_windows.go) and its manifest is fixed: it has no UTF-8 code page and
# no Common Controls 6 entry. go-winres takes the manifest as a file.
set -euo pipefail
cd "$(dirname "$0")/../.."

GO_WINRES=github.com/tc-hib/go-winres@v0.3.3

version=${1:-$(sed -n 's/^const baseVersion = "\(.*\)"$/\1/p' cmd/kvit-notes/version.go)}
[ -n "$version" ] || { echo "make-syso: no version given and none in cmd/kvit-notes/version.go" >&2; exit 1; }

# --product-version and --file-version set both the numeric version
# (2.0.0-rc1 becomes 2.0.0.0) and the text shown beside it (2.0.0-rc1).
go run "$GO_WINRES" make \
    --in packaging/windows/winres.json \
    --out cmd/kvit-notes/rsrc \
    --arch amd64 \
    --product-version "$version" \
    --file-version "$version"
echo "wrote cmd/kvit-notes/rsrc_windows_amd64.syso (version $version)"
