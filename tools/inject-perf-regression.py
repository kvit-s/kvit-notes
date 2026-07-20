#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Inject a deliberate slowdown into a hot path, for tools/verify-perf-budget.sh.

Kept in its own file rather than inline in the shell script because both use
heredocs and nesting them is how you get a script that silently does nothing.

Each injection is the shape of an ordinary refactor mistake - repeating work
that was already done - rather than a sleep. A budget that only catches a
sleep is not evidence of much. The caller reverts with `git checkout`.
"""

import pathlib
import sys

INJECTIONS = {
    # A refactor that re-derives fields by indexing a second time. Roughly
    # doubles the CPU cost of opening a collection.
    "notecollection.cpp": (
        """    indexNoteFromText(relPath, fileText, info);
    const NoteEntry *entry = note(relPath);""",
        """    indexNoteFromText(relPath, fileText, info);
    indexNoteFromText(relPath, fileText, info);   // injected regression
    const NoteEntry *entry = note(relPath);""",
    ),
    # Verification landing inside a nested loop, so every candidate block is
    # checked several times over. Sized to about 2.5x the query cost, which is
    # the scale this budget can separate from machine noise - see the comment
    # on the budget in tests/test_searchindexdb.cpp.
    "searchindexdb.cpp": (
        """            const QList<int> offsets = SearchMatching::verifyOccurrences(
                display, effective, wholeWord);""",
        """            for (int kvitInjected = 0; kvitInjected < 5; ++kvitInjected)
                SearchMatching::verifyOccurrences(display, effective,
                                                  wholeWord);   // injected
            const QList<int> offsets = SearchMatching::verifyOccurrences(
                display, effective, wholeWord);""",
    ),
    # Sort keys recomputed inside the comparator instead of once per row: the
    # classic reason a sort that looked linear turns out not to be.
    "querydata.cpp": (
        """            const int cmp = compareTyped(fieldValue(*a.entry, key.field),
                                         fieldValue(*b.entry, key.field));""",
        """            for (int kvitInjected = 0; kvitInjected < 8; ++kvitInjected)
                compareTyped(fieldValue(*a.entry, key.field),
                             fieldValue(*b.entry, key.field));   // injected
            const int cmp = compareTyped(fieldValue(*a.entry, key.field),
                                         fieldValue(*b.entry, key.field));""",
    ),
}


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: inject-perf-regression.py <source file>", file=sys.stderr)
        return 2

    path = pathlib.Path(sys.argv[1])
    if path.name not in INJECTIONS:
        print(f"no injection defined for {path.name}", file=sys.stderr)
        return 2

    old, new = INJECTIONS[path.name]
    text = path.read_text()
    if old not in text:
        print(f"injection point not found in {path}; has it been refactored?",
              file=sys.stderr)
        return 1

    path.write_text(text.replace(old, new, 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
