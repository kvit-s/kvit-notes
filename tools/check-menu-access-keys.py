#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Check the access keys the menus in qml/ declare.

A menu label marks its access key with `&` before a letter, as in "&Copy".
Windows and Linux draw that letter underlined and run the command when it is
typed while the menu is open; the whole mechanism is described in
src/platform/menuaccesskeys.h.

Two ways of getting it wrong are checked here, and they share a property:
both leave a menu that reads correctly in the source, passes qmllint and
photographs correctly, and goes wrong only under a keystroke that nobody
without a keyboard-driven habit would try.

  * Two commands in one menu claiming the same letter. Both are drawn
    underlined, and the key runs the wrong command or nothing. The rule is one
    key per letter per menu; a label written as a conditional ("Mark as done"
    / "Mark as not done") is one command with two spellings that never appear
    at once, so its two halves may share a letter with each other.

  * A label that never goes through MenuText at all, which is what writing
    `text: qsTr("Copy")` gives — the spelling every menu in the tree used
    before access keys existed. That command then has no key and no underline
    while everything around it does.

Run it directly, or as the MenuAccessKeyGuard ctest entry:

    python3 tools/check-menu-access-keys.py

Exits non-zero and prints every problem with its file, menu and lines.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QML = os.path.join(ROOT, "qml")

# A label literal, and the marker inside one. `&&` is an ampersand the label
# means to show, not a marker, so it is stepped over.
QSTR = re.compile(r'qsTr\(\s*"((?:[^"\\]|\\.)*)"')
MARKER = re.compile(r"(?<!&)&(&&)*([^&])")

TITLE = re.compile(r"\btitle\s*:")

# The one menu with no access keys, and therefore the one file whose labels do
# not go through MenuText. Its entries are a list of code languages to pick
# from rather than commands to run, most of them come from a list in C++, and
# several share their first letters; the reasoning is written out at the top of
# the file. Exempting it by name keeps that a decision somebody made rather
# than a check nobody notices is not running.
EXEMPT = {"LanguagePicker.qml"}


def access_keys(text):
    """Every access key `text` declares, upper-cased.

    A marker on a `%1` placeholder — the outline panel's "Heading &%1", whose
    four entries come from one Repeater — declares a key that only exists once
    the argument is filled in, so there is nothing here to compare. Those are
    left out rather than recorded as a claim on `%`, which would be a letter
    no menu ever shows.
    """
    return [m.group(2).upper() for m in MARKER.finditer(text)
            if m.group(2) != "%"]


def bindings(lines):
    """The file as (start_line, is_title, [access keys]) tuples.

    Every label is found by looking for the marker itself rather than for the
    property it is bound to, because a label reaches a menu by more than one
    route: written on a `text:` line, on a `title:` line, or carried through a
    Repeater's model as a `name`/`label` field in an array of objects, which
    is how the formatting spans and the diagram shapes and colours are built.

    An expression can span several lines — a conditional label is written over
    three — so lines are joined until their brackets balance, and every key
    that one joined expression declares counts as a single claim. That is what
    makes the two spellings of a conditional label ("Mark as done" and "Mark
    as not done") free to share a letter with each other.
    """
    out = []
    i = 0
    while i < len(lines):
        start = i
        joined = lines[i]
        depth = joined.count("(") - joined.count(")")
        while depth > 0 and i + 1 < len(lines):
            i += 1
            joined += " " + lines[i].strip()
            depth += lines[i].count("(") - lines[i].count(")")
        keys = []
        for literal in QSTR.findall(joined):
            for key in access_keys(literal):
                if key not in keys:
                    keys.append(key)
        if keys:
            out.append((start + 1, bool(TITLE.search(joined)), keys))
        i += 1
    return out


def menu_at(lines, line_number):
    """Which menu the line at `line_number` (1-based) belongs to.

    Menus nest, so this walks the braces: the innermost `Menu {` still open
    at that line owns it. The value is that menu's opening line, which names
    it well enough for a message and is unique within the file. A line
    outside every menu belongs to the file itself — the toolbar's three menu
    buttons are the case that matters, and their keys have to be distinct
    from each other for the same reason a menu's do.
    """
    depth = 0
    stack = []          # (brace depth at which this menu opened, opening line)
    for i, line in enumerate(lines):
        if i + 1 == line_number:
            return stack[-1][1] if stack else 0
        opens_menu = re.match(r"\s*(?:\w+\s*:\s*)?Menu\s*\{", line)
        if opens_menu:
            stack.append((depth, i + 1))
        depth += line.count("{") - line.count("}")
        while stack and depth <= stack[-1][0]:
            stack.pop()
    return stack[-1][1] if stack else 0


def unrouted_labels(path, lines):
    """Menu labels that skip MenuText, which is the other way to get this wrong.

    A clash needs two labels marked; a label with no marker at all is the
    quieter mistake, and it arrives by writing `text: qsTr("Copy")` the way
    every menu in the tree was written before access keys existed. Nothing
    then underlines anything and no key runs the command, and the menu still
    looks correct. So the route is required rather than the marker: a label
    inside a Menu goes through MenuText.label() when it is written here or
    MenuText.plain() when it is a name from somewhere else, and which of the
    two is a judgement the author makes.
    """
    problems = []
    for i, line in enumerate(lines):
        if not re.match(r"\s*(?:text|title)\s*:", line):
            continue
        if "MenuText." in line or not menu_at(lines, i + 1):
            continue
        problems.append(
            "%s:%d: a menu label that does not go through MenuText: %s"
            % (os.path.relpath(path, ROOT), i + 1, line.strip()))
    return problems


def check(path):
    lines = open(path, encoding="utf-8").read().split("\n")
    claimed = {}        # (owning menu line) -> {key: first line that took it}
    problems = []
    if os.path.basename(path) not in EXEMPT:
        problems += unrouted_labels(path, lines)
    for line_number, is_title, keys in bindings(lines):
        owner = menu_at(lines, line_number)
        if is_title:
            # A submenu's title is a command in the menu above it, not in
            # itself, so it is claimed one level out.
            owner = menu_at(lines, owner) if owner else 0
        taken = claimed.setdefault(owner, {})
        for key in keys:
            if key in taken:
                where = ("the menu opening at line %d" % owner) if owner \
                        else "the window"
                problems.append(
                    "%s:%d: access key %s in %s is already taken at line %d"
                    % (os.path.relpath(path, ROOT), line_number, key, where,
                       taken[key]))
            else:
                taken[key] = line_number
    return problems


def main():
    problems = []
    for name in sorted(os.listdir(QML)):
        if name.endswith(".qml"):
            problems += check(os.path.join(QML, name))
    for problem in problems:
        print(problem)
    if problems:
        print("\n%d menu access key problem(s)." % len(problems))
        return 1
    print("Menu access keys: every label routed, no clashes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
