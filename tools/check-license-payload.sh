#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Verify that a built artifact carries every license text it is obliged to
# distribute, and fail if one is missing or empty.
#
#   tools/check-license-payload.sh <root> [--qt]
#
# <root> is the directory the install layout starts at: an AppDir's usr/, a
# `cmake --install` prefix, or an extracted portable tree. --qt additionally
# requires the Qt LGPL texts, which come from the Qt kit at deploy time and so
# only exist in artifacts a deploy tool has processed.
#
# The obligations, and why each file has to be in the artifact rather than
# only in the repository:
#
#   LICENSE                  MPL-2.0 §3.1, the project's own code
#   LICENSE.microtex         MIT, vendored MicroTeX
#   LICENSE.tinyxml2         Zlib, vendored with MicroTeX
#   THIRD-PARTY-NOTICES.md   the summary tying the above together
#   math-res font licenses   Knuth/OFL/dsrom/LPPL/Bitstream texts, which must
#                            sit beside the fonts they cover
#   Qt LGPL texts (--qt)     packaging/qt-lgpl-checklist.md
#
# An empty file counts as missing: the AppImage script used to copy the Qt
# texts with `|| true`, which turned a failed copy into an empty directory and
# a silently unlicensed artifact.
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "usage: $0 <root> [--qt]" >&2
    exit 2
fi

ROOT=$1
REQUIRE_QT=0
[ "${2:-}" = "--qt" ] && REQUIRE_QT=1

if [ ! -d "$ROOT" ]; then
    echo "not a directory: $ROOT" >&2
    exit 2
fi

# The layouts differ per platform, so each file is located by search rather
# than by a hardcoded path; what matters is that the artifact contains it.
#
# A third argument, when given, is a string the file must contain. Filename
# alone is not always enough to identify a license: the math resource tree
# ships res/greek/LICENSE and res/cyrillic/LICENSE, so a bare search for
# "LICENSE" would find a GPL-3 font text and report the project's own MPL-2.0
# license as present when it is not.
MISSING=0
require() {
    local label=$1 pattern=$2 must_contain=${3:-} found=""
    while IFS= read -r candidate; do
        if [ -z "$must_contain" ] || grep -qF "$must_contain" "$candidate"; then
            found=$candidate
            break
        fi
    done < <(find "$ROOT" -name "$pattern" -type f -size +0 2>/dev/null)

    if [ -z "$found" ]; then
        echo "MISSING: $label (no non-empty '$pattern'${must_contain:+ containing \"$must_contain\"} under $ROOT)" >&2
        MISSING=1
    else
        echo "  ok  $label -> ${found#"$ROOT"/}"
    fi
}

echo "License payload check: $ROOT"
require "project license (MPL-2.0)"      "LICENSE"   "Mozilla Public License"
require "third-party notices summary"    "THIRD-PARTY-NOTICES.md"
require "MicroTeX (MIT)"                 "LICENSE.microtex"
require "tinyxml2 (Zlib)"                "LICENSE.tinyxml2"
require "TeX font licenses (Knuth)"      "Knuth_License.txt"
require "TeX font licenses (OFL)"        "OFL.txt"
require "TeX font licenses (dsrom)"      "License_for_dsrom.txt"
# kvit-newtx ships its four texts inside a LICENSES/ directory, so each is
# named individually rather than checking for the directory.
require "kvit-newtx (LPPL-1.3c)"         "LPPL-1.3c.txt"
require "kvit-newtx (Bitstream Charter)" "BITSTREAM-CHARTER.txt"
require "kvit-newtx (OFL-1.1)"           "OFL-1.1.txt"
require "kvit-newtx (GPL-3.0)"           "GPL-3.0.txt"
require "kvit-newtx provenance notice"   "NOTICE.md"
require "Greek font pack (GPL-3)"        "LICENSE"   "GNU GENERAL PUBLIC LICENSE"

if [ "$REQUIRE_QT" -eq 1 ]; then
    require "Qt LGPL-3 text" "LICENSE.LGPL*"
fi

if [ "$MISSING" -ne 0 ]; then
    echo "License payload incomplete - refusing to publish this artifact." >&2
    exit 1
fi
echo "License payload complete."
