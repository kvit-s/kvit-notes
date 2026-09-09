#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Keep the QML component list in step with the QML on disk.

One hand-maintained list carries the QML components:

    CMakeLists.txt, set(KVIT_QML_FILES ...)   the Kvit QML module's own types,
                                              which the application and every
                                              test binary get by linking it

A component written into qml/ but left out of it fails in two expensive ways.
The shipped shell cannot resolve the type, which ShellTests catches as a QML
warning. The Qt Quick harness is worse: a load error leaves its `when:`
condition waiting rather than failing, and the only backstop is a CTest
timeout, so the gate burns its full ten minutes before reporting anything
(observed 2026-07-07, hung for hours before the timeouts were added).
Comparing the list against the directory turns both into an immediate,
specific failure.

The list is written out rather than globbed because CMake does not re-run for
a new file matching a glob, which would leave a newly added component out of
the build until somebody reconfigured by hand.

tests/integration_tests.qrc is checked too. It used to carry an aliased second
copy of the component list, which is what made a comparison between two lists
necessary; it now holds the Qt Quick Test suite files and nothing else.

    tools/check-qrc-sync.py     # exit 1 on any mismatch
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
QML_DIR = ROOT / "qml"
CMAKELISTS = ROOT / "CMakeLists.txt"
TEST_QRC = ROOT / "tests" / "integration_tests.qrc"

# set(KVIT_QML_FILES\n    qml/Foo.qml\n    ...\n)
QML_FILES_RE = re.compile(r"set\(KVIT_QML_FILES\s*(.*?)\n\)", re.DOTALL)

# <file>tst_foo.qml</file> and <file alias="qml/Foo.qml">../qml/Foo.qml</file>.
# The alias is what QML resolves, so it is the name that must match.
FILE_RE = re.compile(r"<file(?:\s+alias=\"([^\"]+)\")?\s*>([^<]+)</file>")


def module_qml_names():
    """The qml/*.qml basenames the Kvit module publishes as its own types."""
    body = QML_FILES_RE.search(CMAKELISTS.read_text())
    if not body:
        sys.stderr.write(
            "CMakeLists.txt has no set(KVIT_QML_FILES ...) block; the Kvit "
            "module's component list is what this check compares against.\n"
        )
        sys.exit(1)
    names = set()
    for line in body.group(1).splitlines():
        entry = line.strip()
        if entry.startswith("qml/") and entry.endswith(".qml"):
            names.add(pathlib.Path(entry).name)
    return names


def qrc_qml_names(qrc_path):
    """The qml/*.qml basenames a .qrc publishes, by their resolved alias."""
    names = set()
    for alias, target in FILE_RE.findall(qrc_path.read_text()):
        resolved = alias or target
        if resolved.startswith("qml/") and resolved.endswith(".qml"):
            names.add(pathlib.Path(resolved).name)
    return names


def qrc_targets(qrc_path):
    """Every path a .qrc points at, relative to the .qrc's own directory."""
    return [target for _, target in FILE_RE.findall(qrc_path.read_text())]


def main():
    problems = []

    on_disk = {p.name for p in QML_DIR.glob("*.qml")}
    in_module = module_qml_names()

    def report(title, names):
        if names:
            problems.append(
                "{}:\n{}".format(
                    title, "".join("    {}\n".format(n) for n in sorted(names))
                )
            )

    report(
        "QML files on disk but missing from KVIT_QML_FILES in CMakeLists.txt "
        "(the shipped shell cannot resolve these types, and the Qt Quick "
        "harness will hang until its CTest timeout)",
        on_disk - in_module,
    )
    report(
        "Listed in KVIT_QML_FILES but not present in qml/",
        in_module - on_disk,
    )

    # A second copy of the component list here is what this check used to
    # exist for. Catch one growing back rather than letting it drift again.
    strays = qrc_qml_names(TEST_QRC)
    report(
        "tests/integration_tests.qrc lists QML components again; the test "
        "binaries get them from the Kvit module they link, so this is a "
        "second list to keep in step and it should hold only the tst_*.qml "
        "suite files",
        strays,
    )

    # Every target must resolve, whatever it points at — this catches a typo
    # in a path that happens not to be a qml/ file.
    missing = [
        t for t in qrc_targets(TEST_QRC)
        if not (TEST_QRC.parent / t).resolve().exists()
    ]
    report("Listed in {} but not on disk".format(TEST_QRC.name), missing)

    if problems:
        sys.stderr.write(
            "The QML component list is out of step.\n\n" + "\n".join(problems)
        )
        return 1

    print(
        "qml module sync: {} QML files, all listed in KVIT_QML_FILES".format(
            len(on_disk)
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
