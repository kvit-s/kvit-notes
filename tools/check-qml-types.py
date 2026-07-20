#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Keep the qmllint type stub in step with the real QML registrations.

The `Kvit` QML module is registered imperatively, in
AppContext::registerQmlTypes() (src/appcontext.cpp), rather than through
QML_ELEMENT plus qt_add_qml_module. Nothing therefore generates a .qmltypes
description at build time, and qmllint cannot resolve `import Kvit 1.0` on its
own; tools/qmllint/Kvit/kvit.qmltypes is a hand-written stand-in that lets it.

A hand-written file drifts. If a type is registered in C++ and not added to
the stub, qmllint stops resolving it and the lint gate fails with a confusing
"type not found" against correct QML. This script compares the two lists and
says plainly which side is missing what.

    tools/check-qml-types.py     # exit 1 on any mismatch
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
APPCONTEXT = ROOT / "src" / "appcontext.cpp"
QMLTYPES = ROOT / "tools" / "qmllint" / "Kvit" / "kvit.qmltypes"

# qmlRegisterType<T>("Kvit", 1, 0, "Name") and the Uncreatable/Singleton
# variants; the QML-visible name is the last string argument.
REGISTER = re.compile(
    r'qmlRegister\w*Type\s*<[^>]+>\s*\(\s*"Kvit"\s*,\s*\d+\s*,\s*\d+\s*,\s*"(\w+)"'
)
EXPORT = re.compile(r'exports:\s*\[\s*"Kvit/(\w+)\s')


def main() -> int:
    for path in (APPCONTEXT, QMLTYPES):
        if not path.exists():
            print(f"missing file: {path}", file=sys.stderr)
            return 2

    registered = set(REGISTER.findall(APPCONTEXT.read_text()))
    described = set(EXPORT.findall(QMLTYPES.read_text()))

    if not registered:
        print(
            f"no Kvit registrations found in {APPCONTEXT.relative_to(ROOT)} — "
            "has registerQmlTypes() moved or changed shape?",
            file=sys.stderr,
        )
        return 2

    undescribed = sorted(registered - described)
    stale = sorted(described - registered)

    for name in undescribed:
        print(
            f"registered in C++ but absent from the qmllint stub: {name}\n"
            f"  add a Component block for it to {QMLTYPES.relative_to(ROOT)}",
            file=sys.stderr,
        )
    for name in stale:
        print(
            f"described in the qmllint stub but no longer registered: {name}\n"
            f"  remove its Component block from {QMLTYPES.relative_to(ROOT)}",
            file=sys.stderr,
        )

    if undescribed or stale:
        return 1

    print(f"qmllint type stub matches all {len(registered)} Kvit registrations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
