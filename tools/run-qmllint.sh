#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Static QML lint gate. Runs qmllint over qml/ and fails on any warning that
# survives the configuration below, so a broken import or a malformed QML file
# is caught before it reaches the runtime shell test.
#
#   tools/run-qmllint.sh              # lint qml/
#   tools/run-qmllint.sh a.qml b.qml  # lint specific files
#
# Finding qmllint: $QT_ROOT_DIR/bin/qmllint if that is set (CI exports it via
# install-qt-action), otherwise the newest 6.x kit under ~/Qt, otherwise PATH.
#
# ── Why some categories are switched off
#
# This project composes QML against C++ through context properties installed
# by AppContext::wire() (theme, appSettings, blockMenuModel, noteCollection,
# and roughly thirty more). qmllint performs a purely static analysis and has
# no way to know those names exist, so it reports every single use of them:
#
#   unqualified       2522 findings, essentially all context-property reads
#   missing-property   670 findings, properties reached through those objects
#
# Leaving either enabled means the gate reports ~3200 findings on a healthy
# tree, which is the same as having no gate. They are disabled here rather
# than tolerated with a warning budget, so that the findings which DO appear
# are all real. The runtime counterpart is ShellTests (tests/test_shell.cpp),
# which loads the production resources.qrc with the real context properties
# attached and therefore does catch a genuinely broken one.
#
# Imports, by contrast, are fully checked: tools/qmllint/Kvit supplies the
# type description for the imperatively-registered `Kvit` module, so the tree
# lints clean and a wrong module name or version fails this script.
#
# Three further categories are demoted to `info` — printed on every run, but
# not failing the gate — because they flag pre-existing findings in QML this
# change does not touch. Their counts as of this commit:
#
#   Quick.layout-positioning   32  width/height set on layout-managed items
#   equality-type-coercion      3  `source != ""` in main.qml
#   Quick.anchor-combinations   1  ImageBlock.qml conditional left/right anchors
#
# They are worth fixing in the QML itself; until then, demoting them keeps the
# gate meaningful instead of permanently red. Every other category qmllint
# knows about is left at its default and fails the build.
#
# Verified by injecting each break into a QML file and running this script:
# a syntax error, an import of a module that does not exist, and a use of a
# type that does not exist all fail it. One thing it does NOT catch is a bad
# version on a module that does resolve — `import QtQuick.Window 9.9` lints
# clean — so version correctness still rests on ShellTests loading the real
# shell at runtime.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ -n "${QT_ROOT_DIR:-}" ] && [ -x "$QT_ROOT_DIR/bin/qmllint" ]; then
    QMLLINT="$QT_ROOT_DIR/bin/qmllint"
elif [ -d "$HOME/Qt" ] && \
     kit=$(ls "$HOME/Qt" | grep -E '^6\.' | sort -V | tail -1) && \
     [ -x "$HOME/Qt/$kit/gcc_64/bin/qmllint" ]; then
    QMLLINT="$HOME/Qt/$kit/gcc_64/bin/qmllint"
elif command -v qmllint > /dev/null; then
    QMLLINT=qmllint
else
    echo "qmllint not found (set QT_ROOT_DIR to the Qt kit)" >&2
    exit 2
fi

if [ $# -gt 0 ]; then
    FILES=("$@")
else
    mapfile -t FILES < <(find qml -name '*.qml' | sort)
fi

# -W 0 turns "any warning at all" into a non-zero exit; without it qmllint
# prints findings and still succeeds.
"$QMLLINT" \
    -I qml \
    -I tools/qmllint \
    -W 0 \
    --unqualified disable \
    --missing-property disable \
    --equality-type-coercion info \
    --Quick.layout-positioning info \
    --Quick.anchor-combinations info \
    "${FILES[@]}"

echo "qmllint: ${#FILES[@]} files clean"
