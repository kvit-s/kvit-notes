#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Check that every block kind is implemented, and that its delegate exists.

A note is a list of blocks, and about twenty kinds of them exist. Everything
decided per kind — how it serializes, what its text is, what it exports as,
which delegate draws it — is a pure virtual on BlockKindDef, so a kind that
has not answered something does not compile. That is the guarantee, and it
covers almost all of this.

Two things a compiler cannot see are checked here:

  * an enumerator in BlockKind that no source under src/domain/blockkinds/
    mentions at all. The compiler is satisfied — nothing forces an enumerator
    to have a class — and the block resolves to the paragraph at runtime, so
    it silently draws and exports as prose.

  * a delegate URL naming a QML file that is not in qml/ or not listed in
    resources.qrc. The shell builds one DelegateChoice per registered kind
    from these, so an unresolvable one draws an empty row, and the warning
    it logs is easy to miss in a running application.

Only the second of those needs a script. The first is checked far more
exactly by BlockKindDefTests, which walks the enumeration from its metaobject
and asks the registry for each definition; this is the cheap version that
runs without a built tree, and it deliberately accepts a bare mention rather
than trying to recognise a class — four heading kinds are one class with four
instances, and a scanner that insisted on `return BlockKind::Heading2;` would
report them as missing.

The behaviour of each kind — that its markdown round-trips, that its HTML and
its plain text are not empty — is BlockKindDefTests' as well.

    tools/check-block-kinds.py     # exit 1 on any problem
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
KIND_HEADER = ROOT / "src" / "domain" / "blockkind.h"
KINDS_DIR = ROOT / "src" / "domain" / "blockkinds"
QML_DIR = ROOT / "qml"
APP_QRC = ROOT / "resources.qrc"

# `Paragraph    = Block::Paragraph,` and `Kanban  = 100,` alike: the name is
# what matters here, not the value.
ENUMERATOR_RE = re.compile(r"^\s{4}(\w+)\s*=\s*[^,]+,\s*$", re.MULTILINE)

# Any mention: `return BlockKind::Kanban;` in a one-instance class, and
# `BlockKind::Heading2` as a constructor argument where one class serves four.
IMPLEMENTED_RE = re.compile(r"BlockKind::(\w+)\b")

# `return QStringLiteral("qrc:/qml/KanbanBlock.qml");`
DELEGATE_URL_RE = re.compile(r"\"(qrc:/qml/[A-Za-z0-9_]+\.qml)\"")


def enumerators():
    """Every BlockKind enumerator, in declaration order."""
    text = KIND_HEADER.read_text(encoding="utf-8")
    start = text.index("enum class Kind")
    end = text.index("};", start)
    return ENUMERATOR_RE.findall(text[start:end])


def kind_sources():
    return sorted(KINDS_DIR.glob("*.cpp"))


def implemented_kinds():
    """Which enumerator each source implements, and where."""
    found = {}
    for path in kind_sources():
        for name in IMPLEMENTED_RE.findall(path.read_text(encoding="utf-8")):
            found.setdefault(name, []).append(path.name)
    return found


def delegate_urls():
    urls = set()
    for path in kind_sources():
        urls.update(DELEGATE_URL_RE.findall(path.read_text(encoding="utf-8")))
    return urls


def main():
    problems = []

    declared = enumerators()
    implemented = implemented_kinds()

    for name in declared:
        if name not in implemented:
            problems.append(
                "BlockKind::{} is declared in src/domain/blockkind.h and no "
                "source under src/domain/blockkinds/ mentions it. A block of "
                "that kind resolves to the paragraph, so it draws and exports "
                "as prose with nothing to say so.".format(name))

    for name, sources in sorted(implemented.items()):
        if name not in declared:
            problems.append(
                "{} names BlockKind::{}, which src/domain/blockkind.h does "
                "not declare.".format(", ".join(sorted(set(sources))), name))

    listed = APP_QRC.read_text(encoding="utf-8")
    for url in sorted(delegate_urls()):
        name = url[len("qrc:/"):]              # qml/KanbanBlock.qml
        if not (ROOT / name).exists():
            problems.append(
                "a block kind names the delegate {}, which is not in qml/. "
                "The shell logs a warning and the block draws an empty row."
                .format(url))
        elif name not in listed:
            problems.append(
                "a block kind names the delegate {}, which is not listed in "
                "resources.qrc. It exists on disk but is not in the binary, "
                "so the shipped application draws an empty row for it."
                .format(url))

    if problems:
        sys.stderr.write("Block kinds are out of step.\n\n")
        for problem in problems:
            sys.stderr.write("  {}\n\n".format(problem))
        return 1

    print("block kinds: {} declared, all implemented across {} files, "
          "{} delegates resolve"
          .format(len(declared), len(kind_sources()), len(delegate_urls())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
