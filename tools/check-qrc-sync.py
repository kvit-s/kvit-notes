#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Keep the QML resource list in step with the QML on disk.

One hand-maintained list carries the QML components:

    resources.qrc   the shipped application, test_shell, and every Qt Quick
                    Test binary, which all compile this same file

A component written into qml/ but left out of it fails in two expensive ways.
The shipped shell cannot resolve the type, which ShellTests catches as a QML
warning. The Qt Quick harness is worse: a load error leaves its `when:`
condition waiting rather than failing, and the only backstop is a CTest
timeout, so the gate burns its full ten minutes before reporting anything
(observed 2026-07-07, hung for hours before the timeouts were added).
Comparing the list against the directory turns both into an immediate,
specific failure.

tests/integration_tests.qrc is checked only for targets that resolve. It used
to carry an aliased second copy of the component list, which is what made a
comparison between two lists necessary; it now holds the Qt Quick Test suite
files and nothing else.

    tools/check-qrc-sync.py     # exit 1 on any mismatch
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
QML_DIR = ROOT / "qml"
APP_QRC = ROOT / "resources.qrc"
TEST_QRC = ROOT / "tests" / "integration_tests.qrc"

# <file>qml/Foo.qml</file> and <file alias="qml/Foo.qml">../qml/Foo.qml</file>.
# The alias is what QML resolves, so it is the name that must match.
FILE_RE = re.compile(r"<file(?:\s+alias=\"([^\"]+)\")?\s*>([^<]+)</file>")


def qml_names(qrc_path):
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
    in_app = qml_names(APP_QRC)

    def report(title, names):
        if names:
            problems.append(
                "{}:\n{}".format(
                    title, "".join("    {}\n".format(n) for n in sorted(names))
                )
            )

    report(
        "QML files on disk but missing from resources.qrc (the shipped shell "
        "cannot resolve these types, and the Qt Quick harness will hang until "
        "its CTest timeout)",
        on_disk - in_app,
    )
    report(
        "Listed in resources.qrc but not present in qml/",
        in_app - on_disk,
    )

    # A second copy of the component list here is what this check used to
    # exist for. Catch one growing back rather than letting it drift again.
    strays = qml_names(TEST_QRC)
    report(
        "tests/integration_tests.qrc lists QML components again; the test "
        "binaries compile resources.qrc, so this is a second list to keep in "
        "step and it should hold only the tst_*.qml suite files",
        strays,
    )

    # Every target must resolve, whatever it points at — this catches a typo
    # in a path that happens not to be a qml/ file.
    for qrc in (APP_QRC, TEST_QRC):
        missing = [
            t for t in qrc_targets(qrc) if not (qrc.parent / t).resolve().exists()
        ]
        report("Listed in {} but not on disk".format(qrc.name), missing)

    if problems:
        sys.stderr.write(
            "The QML resource list is out of step.\n\n" + "\n".join(problems)
        )
        return 1

    print("qrc sync: {} QML files, all listed in resources.qrc".format(len(on_disk)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
