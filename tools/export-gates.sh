#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
# The five export gates (launch-plan.md A1.4), run against an exported
# public tree (tools/export-public-tree.sh). All five must pass before the
# tree ever becomes public, and again before every release (D4 gates).
#
#   tools/export-gates.sh <tree> [--skip-build]
#
# --skip-build skips gate 1 (fresh-clone build+test, the slow one) for
# quick iterations on the other four; a release run never skips it.
set -uo pipefail
cd "$(dirname "$0")/.."
PRIVATE_ROOT=$(pwd)

TREE="${1:?usage: export-gates.sh <tree> [--skip-build]}"
SKIP_BUILD="${2:-}"
TREE=$(cd "$TREE" && pwd)
FAILED=0

gate() {  # gate <name> <command...>
    echo ""
    echo "══ gate: $1 ══"
    shift
    if "$@"; then
        echo "── PASS"
    else
        echo "── FAIL"
        FAILED=1
    fi
}

# ── 1. Fresh-clone configure, build, full unit-suite run ──────────────
gate1() {
    # No RETURN trap: under `local` + set -u it re-fires in the caller's
    # scope after the local is gone and kills the whole run.
    local scratch status
    scratch=$(mktemp -d)
    (
        set -e
        git clone -q "$TREE" "$scratch/clone"
        cd "$scratch/clone"
        cmake --preset linux-release > "$scratch/configure.log" 2>&1 \
            || { tail -20 "$scratch/configure.log"; exit 1; }
        cmake --build --preset linux-release -j "$(nproc)" > "$scratch/build.log" 2>&1 \
            || { tail -20 "$scratch/build.log"; exit 1; }
        QT_QPA_PLATFORM=offscreen ctest --preset unit-linux > "$scratch/test.log" 2>&1 \
            || { tail -30 "$scratch/test.log"; exit 1; }
        grep -E "tests passed|tests failed" "$scratch/test.log" | tail -1
    )
    status=$?
    rm -rf "$scratch"
    return "$status"
}

# ── 2. Broken-link scan over all markdown ─────────────────────────────
gate2() { python3 tools/check-md-links.py "$TREE"; }

# ── 3. Secret / PII / absolute-path scan ──────────────────────────────
gate3() {
    local bad=0 home_pat email_re
    home_pat='/home'/
    # Machine paths anywhere (incl. vendored code) are an export bug.
    if grep -RIn --exclude-dir=.git -e "$home_pat" "$TREE"; then
        echo "absolute home paths found"; bad=1
    fi
    # The owner's personal address must never ship: the project is published
    # under project identities only. This check is deliberately separate and
    # named, because the address was once on the allowlist below and four
    # files reached the public repository carrying it.
    if grep -RIn --exclude-dir=.git -e 'skolos@yahoo\.com' "$TREE"; then
        echo "personal email address found (use info@/security@/conduct@kvit.app)"
        bad=1
    fi
    # Any other address outside the intended contact points; vendored code
    # keeps its upstream authors' addresses.
    email_re='[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}'
    if grep -RInE --exclude-dir=.git --exclude-dir=third_party "$email_re" "$TREE" \
        | grep -vE '(info|security|conduct)@kvit\.app|user@example|@example\.com|@2x\.' \
        | grep -v '^\s*$'; then
        echo "unexpected email addresses found"; bad=1
    fi
    # Key material and token shapes.
    if grep -RInE --exclude-dir=.git \
        -e 'BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY' \
        -e 'ghp_[A-Za-z0-9]{20,}' -e 'github_pat_[A-Za-z0-9_]{20,}' \
        -e 'sk-[A-Za-z0-9]{20,}' -e 'AKIA[0-9A-Z]{16}' "$TREE"; then
        echo "secret-shaped strings found"; bad=1
    fi
    return "$bad"
}

# ── 4. License / SBOM scan ────────────────────────────────────────────
gate4() {
    local bad=0
    cmp -s "$TREE/LICENSE" /usr/share/common-licenses/MPL-2.0 \
        || cmp -s "$TREE/LICENSE" "$PRIVATE_ROOT/LICENSE" \
        || { echo "LICENSE missing or not the canonical MPL-2.0 text"; bad=1; }
    (cd "$TREE" && tools/apply-license-headers.sh --check) || bad=1
    (cd "$TREE" && python3 tools/generate-notices.py --check) || bad=1
    [ -f "$TREE/packaging/sbom.yaml" ] || { echo "sbom.yaml missing"; bad=1; }
    return "$bad"
}

# ── 5. Forbidden-file scan (denylist; belt to the allowlist's braces),
#      over the working tree AND every commit of the mirror's history ──
gate5() {
    local bad=0 forbidden
    forbidden='chat.md to-market.md launch-plan.md pre-launch-plan.md
        agentic-app-prd.md agentic-spreadsheet.md llm-diagram.md jupyter.md
        gaps.md search.md diagrams-prd.md diagrams-progress.md
        foundation-progress.md agent_resources.qrc plan.md progress.md
        basic-features.md performance-progress.md'
    for f in $forbidden; do
        [ -e "$TREE/$f" ] && { echo "forbidden file present: $f"; bad=1; }
    done
    for d in src/agent qml/agent tests/agent tests/perf/live planning; do
        [ -e "$TREE/$d" ] && { echo "forbidden tree present: $d"; bad=1; }
    done
    local hist
    hist=$(cd "$TREE" && git log --all --name-only --format= | sort -u)
    # Forbidden entries are exact root-relative paths: the scrubbed fixture
    # tests/fixtures/llm-diagram.md legitimately shares a basename with the
    # private root document it replaced.
    for f in $forbidden; do
        echo "$hist" | grep -qx "$f" && { echo "forbidden file in history: $f"; bad=1; }
    done
    echo "$hist" | grep -qE '^(src|qml|tests)/agent/' \
        && { echo "agent module present in history"; bad=1; }
    return "$bad"
}

# ── 6. References to planning documents that ship nowhere ─────────────
#      Gate 5 proves the documents are absent. This proves nothing points
#      at them: a comment reading "(phase8-plan.md decision 8)" sends a
#      public reader to a file they cannot open, and the phase plans do not
#      even exist privately any more, having been deleted as their
#      milestones closed. Provenance that only the author can resolve is
#      not provenance; the comment should carry the reasoning itself.
#
#      Matched by shape rather than by a list of what happens to exist
#      today, so a planning document written next month is covered without
#      touching this gate. Names that resolve to a file actually shipped in
#      the tree are fine, which is what keeps features.md and devel.md
#      citable. Note filenames used as test data (Welcome.md, Cherry.md)
#      do not match these shapes.
gate6() {
    local bad=0 shapes hit
    # The prefix is optional: bare plan.md and progress.md are cited too.
    shapes='([A-Za-z0-9_-]+-)?(plan|prd|progress)\.md'
    shapes="$shapes|(chat|search|gaps|jupyter|to-market|tex-editing)\.md"
    shapes="$shapes|(llm-normalization|llm-diagram|decisions|CLAUDE)\.md"
    shapes="$shapes|(basic-features|agentic-app-prd|agentic-spreadsheet)\.md"

    # Drop hits whose name resolves to a file the tree actually ships.
    hit=$(grep -RInE --exclude-dir=.git "(^|[^A-Za-z0-9_-])($shapes)" "$TREE" \
        | while IFS= read -r line; do
            name=$(printf '%s' "$line" | grep -oE "($shapes)" | head -1)
            (cd "$TREE" && git ls-files | grep -qE "(^|/)${name}$") || echo "$line"
          done)
    if [ -n "$hit" ]; then
        printf '%s\n' "$hit" | head -40
        printf '%s\n' "$hit" | wc -l | xargs printf '%s reference(s) to documents that ship nowhere\n'
        bad=1
    fi
    return "$bad"
}

if [ "$SKIP_BUILD" = "--skip-build" ]; then
    echo "══ gate: fresh-clone build+test ══ (SKIPPED - never for a release)"
else
    gate "fresh-clone build+test" gate1
fi
gate "broken-link scan" gate2
gate "secret/PII/path scan" gate3
gate "license/SBOM scan" gate4
gate "forbidden-file scan" gate5
gate "private-document reference scan" gate6

echo ""
if [ "$FAILED" -eq 0 ]; then
    echo "ALL GATES GREEN"
else
    echo "GATES FAILED"
fi
exit "$FAILED"
