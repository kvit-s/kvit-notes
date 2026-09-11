#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Keep the open note and the editor's screen on opposite sides of one line.

qml/main.qml is the application's one window, and the parts around it — the
panes, the menus, the dialogs, the tray, the keyboard map — reach it through a
property they each call `appWindow`. Four unrelated things used to be reachable
that way, and only one of them was machinery rather than presentation:

  * **the window** — its geometry, its visibility, `show`, `raise`, `close`,
    `contentItem`;
  * **the editor's screen** — which sidebar view is showing, how wide each pane
    is, what is collapsed, focus and typewriter mode, the pane cycle;
  * **the editing surface** — the caret row, the drag, scrolling to a block,
    all of which qml/main.qml forwards to the BlockEditor in the document pane;
  * **the open note** — which note is open, every transition into another one,
    saving, the conflict and recovery questions, and the status line the
    answers are reported through.

The fourth is now qml/NoteSession.qml, a type a host declares and hands to the
parts that need it. Keeping it that way is two rules, and neither the compiler
nor qmllint can see either of them, because both describe what a file reaches
for rather than whether the reach resolves.

  1. **Nothing but the window itself asks the window about the open note.** A
     part that wants to open, save or navigate a note takes a NoteSession. The
     day one of them goes back through `appWindow` instead, that part needs the
     application's main window again in order to work, and a second window —
     quick capture, a preview, an embedded editor — cannot have it.

  2. **The session never reads the screen.** A NoteSession that asked for
     `focusMode` or a pane width would be a session only the full editor screen
     could own, which is the whole arrangement the split was made to end.

The first rule's member list is read out of qml/NoteSession.qml rather than
written here, so adding a member to the session extends the check by itself.

Run it directly, or as the WindowReachGuard ctest entry:

    python3 tools/check-window-reach.py

Exits non-zero and prints every problem with its file and line.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QML = os.path.join(ROOT, "qml")
SESSION = "NoteSession.qml"

# The window's own file declares the session and forwards its public names to
# it, so it names both sides of the line by definition.
HOST = "main.qml"

# Where an exception would be written down if one were ever justified. A file
# here would be one that reaches the window for the open note on purpose; there
# is no such file, and a new one should be argued for rather than added.
ALLOWED_TO_REACH_THE_WINDOW = set()

# The editor's screen, as qml/main.qml declares it. These are the members a
# session may not read: a pane's width, what is collapsed, which view is
# showing, and the two whole-window presentation modes.
SCREEN_MEMBERS = {
    "panelsVisible",
    "navigationRailsVisible",
    "sidebarView",
    "knownSidebarView",
    "notesFamilyView",
    "sidebarCollapsed",
    "sidebarWidth",
    "noteListCollapsed",
    "noteListWidth",
    "outlineVisible",
    "outlineWidth",
    "backlinksVisible",
    "backlinksWidth",
    "statusBarVisible",
    "bottomDockCollapsed",
    "bottomDockHeight",
    "bottomChromeHeight",
    "focusMode",
    "typewriterMode",
    "focusedPane",
    "focusPane",
    "cyclePane",
    "contentView",
}

# A `property <type> <name>` or `function <name>(` written at the root object's
# own indentation. Anything deeper belongs to an object declared inside the
# file and is not part of what a host can reach.
TOP_LEVEL_PROPERTY = re.compile(
    r"^    (?:readonly\s+)?property\s+(?:alias\s+|[A-Za-z_][\w.<>]*\s+)(\w+)")
TOP_LEVEL_FUNCTION = re.compile(r"^    function\s+(\w+)\s*\(")

COMMENT = re.compile(r"^\s*//")


def session_surface():
    """Every member a host can reach on a NoteSession."""
    members = set()
    with open(os.path.join(QML, SESSION), encoding="utf-8") as handle:
        for line in handle:
            for pattern in (TOP_LEVEL_PROPERTY, TOP_LEVEL_FUNCTION):
                found = pattern.match(line)
                if found:
                    members.add(found.group(1))
    return members


def qml_files():
    return sorted(name for name in os.listdir(QML) if name.endswith(".qml"))


def check_nothing_reaches_the_window_for_the_open_note(members, problems):
    reach = re.compile(r"\bappWindow\s*\.\s*(\w+)")
    for name in qml_files():
        if name in (HOST, SESSION) or name in ALLOWED_TO_REACH_THE_WINDOW:
            continue
        path = os.path.join(QML, name)
        with open(path, encoding="utf-8") as handle:
            for number, line in enumerate(handle, 1):
                if COMMENT.match(line):
                    continue
                for member in reach.findall(line):
                    if member in members:
                        problems.append(
                            "qml/%s:%d: reaches the window for `%s`, which is "
                            "the open note's. Take a NoteSession and call it "
                            "on that." % (name, number, member))


def check_the_session_never_reads_the_screen(problems):
    path = os.path.join(QML, SESSION)
    with open(path, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            if COMMENT.match(line):
                continue
            for member in SCREEN_MEMBERS:
                if re.search(r"\b%s\b" % re.escape(member), line):
                    problems.append(
                        "qml/%s:%d: reads `%s`, which is the editor's screen. "
                        "A session that needs the screen is a session only the "
                        "editor window can have." % (SESSION, number, member))


def main():
    members = session_surface()
    if not members:
        print("could not read any member off qml/%s" % SESSION, file=sys.stderr)
        return 1

    problems = []
    check_nothing_reaches_the_window_for_the_open_note(members, problems)
    check_the_session_never_reads_the_screen(problems)

    if problems:
        print("The open note and the editor's screen have run together again:",
              file=sys.stderr)
        for problem in problems:
            print("  " + problem, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
