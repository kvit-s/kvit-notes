#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Keep the QML resource lists in step with the QML on disk, and with each other.

Two hand-maintained .qrc files carry the same set of QML files:

    resources.qrc               the shipped application, and test_shell
    tests/integration_tests.qrc the Qt Quick Test binaries, which re-export
                                the same files under aliases

A file added to qml/ and to only one of them fails in a way that is expensive
to diagnose. Missing from resources.qrc, the shipped shell cannot resolve the
type; ShellTests now catches that as a QML warning. Missing from
integration_tests.qrc, the Qt Quick harness waits forever on its `when:`
condition — a load error leaves it hanging rather than failing, and the only
backstop is a CTest timeout, so the gate burns its full ten minutes before
reporting anything (observed 2026-07-07, hung for hours before the timeouts
were added).

Comparing the lists directly turns both cases into an immediate, specific
failure.

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
    in_test = qml_names(TEST_QRC)

    def report(title, names):
        if names:
            problems.append(
                "{}:\n{}".format(
                    title, "".join("    {}\n".format(n) for n in sorted(names))
                )
            )

    report(
        "QML files on disk but missing from resources.qrc (the shipped shell "
        "cannot resolve these types)",
        on_disk - in_app,
    )
    report(
        "QML files on disk but missing from tests/integration_tests.qrc (the "
        "Qt Quick harness will hang until its CTest timeout)",
        on_disk - in_test,
    )
    report(
        "Listed in resources.qrc but not present in qml/",
        in_app - on_disk,
    )
    report(
        "Listed in tests/integration_tests.qrc but not present in qml/",
        in_test - on_disk,
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
            "QML resource lists are out of step.\n\n" + "\n".join(problems)
        )
        return 1

    print(
        "qrc sync: {} QML files, listed consistently in resources.qrc and "
        "tests/integration_tests.qrc".format(len(on_disk))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
