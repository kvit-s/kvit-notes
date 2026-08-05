#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Check the QML in qml/ for the mistakes that fail silently at runtime.

Five are checked. They share a property with the menu access keys the sibling
script guards: each leaves QML that reads correctly in the source, passes
qmllint, and photographs correctly. Nothing goes wrong until somebody uses the
application without a pointer, or with a screen reader, or with the system's
reduce-motion setting on — and by then the control has been in the tree for
months. The first three are accessibility.md's, which explains each at length;
the fourth is the trap that arrived with the shared dialog base, described in
qml/KvitDialog.qml, and the fifth is the one described in
qml/DiscoverableMenuItem.qml.

  * A `MouseArea` or `TapHandler` with an activation handler, inside an item
    that declares no `Accessible.name`. A rectangle with a tap handler on it
    is a button to everyone who can see it and nothing at all to the
    accessibility tree: it cannot be announced, found or activated.

  * A control whose visible text is a single non-alphanumeric character —
    "×", "▲", ".*" — with no `Accessible.name` beside it. Qt hands the glyph
    through verbatim, so a screen reader says whatever its dictionary has for
    that character.

  * An animation `duration:` written as a bare number. `Theme.motionScale` is
    0 when reduced motion is on, so a duration written `150 * Theme.motionScale`
    becomes instant and one written `150` ignores the setting entirely.

  * A popup declared inside a `KvitDialog` that assigns its own `contentItem`,
    without an explicit `parent`. It defaults into the content item the base
    created and the derived file then replaced, so it holds an item that is in
    no window, and `Popup.open()` on such a popup does nothing and says
    nothing. The dialog simply never appears.

  * A bare `MenuItem`, or a menu holding a submenu without
    `delegate: DiscoverableMenuItem {}` for the row that submenu gets. Both
    leave a disabled command drawn in the same color as a live one, so the
    only sign that it cannot be run is that it refuses to highlight under the
    pointer — which is no sign at all to somebody reading the menu rather
    than sweeping it with the mouse.

Each check has a named exemption list, so an exception is a decision somebody
made and wrote down rather than a check nobody notices is off.

Run it directly, or as the AccessibleNameGuard ctest entry:

    python3 tools/check-accessible-names.py

Exits non-zero and prints every problem with its file and line.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QML = os.path.join(ROOT, "qml")

# ---------------------------------------------------------------- patterns

# The start of a nested object: `Rectangle {`, `MouseArea {`, `Item {`.
# Also matches a component definition's own header (`component Foo: Item {`)
# and a property that is an object (`background: Rectangle {`), which is what
# is wanted — those are scopes too.
OBJECT_OPEN = re.compile(r"^\s*(?:(?:readonly\s+)?property\s+\w+\s+\w+\s*:\s*|"
                         r"component\s+\w+\s*:\s*|\w+\s*:\s*)?"
                         r"([A-Z]\w*)\s*\{\s*(?://.*)?$")

# An activation handler on a pointer area or handler.
ACTIVATION = re.compile(r"^\s*on(?:Clicked|Tapped)\s*:")

# `Accessible.name:`, the attached role, or an explicit `Accessible.ignored`.
# Any of the three means somebody has decided what this item is to a screen
# reader — including the decision that it is not an element at all, which is
# the right answer for the block-wide areas that carry the modifier-click
# selection gestures rather than a control.
ACCESSIBLE = re.compile(r"\bAccessible\.(?:name|role|ignored)\s*:")

# The three property names the shared components take a spoken name through:
# IconButton's `label` and `help`, and the Kanban and Table controls' `tip`.
# A call site that sets one of these has named its control; the component it
# instantiates is where that name reaches QAccessible.
NAMING_PROPERTY = re.compile(r"^\s*(?:label|help|tip)\s*:")

# A visible label: `text: "×"` or `glyph: "▲"` or `label: qsTr("…")`.
TEXT_LITERAL = re.compile(r'^\s*(?:text|glyph)\s*:\s*"((?:[^"\\]|\\.)*)"\s*$')

# `duration: 150` — a bare number rather than an expression.
BARE_DURATION = re.compile(r"^\s*duration\s*:\s*[0-9.]+\s*(?://.*)?$")

# A popup type, and the two lines that decide whether a nested one is safe.
POPUP_TYPES = ("KvitDialog", "Dialog", "Popup", "Menu")
CONTENT_ITEM = re.compile(r"^\s*contentItem\s*:")
PARENT_ASSIGN = re.compile(r"^\s*parent\s*:")

# `delegate: DiscoverableMenuItem {}` — what a menu builds a submenu's own row
# from. The row is Qt's, not the file's, so this line is the only way to reach
# it.
MENU_DELEGATE = re.compile(
    r"^\s*delegate\s*:\s*DiscoverableMenuItem\s*\{\s*\}\s*$")

# ------------------------------------------------------------- exemptions

# Files whose tap targets are deliberately not controls.
#
# CrossBlockTextSelection / CrossBlockTextDrag / RenderedTextSelection /
# EdgeAutoScroller / BlockDragController: gesture plumbing, not affordances.
# There is nothing to announce — the thing being selected is the text, which
# carries its own name.
#
# EditorDropArea and BlockGapCursor are drop targets and a caret; a drop has
# no keyboard equivalent to name, and the gap cursor is a position rather
# than a control.
#
# AppShortcuts holds the window-wide area that accepts the mouse's own
# back and forward buttons and nothing else. It draws nothing, and the two
# commands it runs are already in the shortcut catalog under Alt+Left and
# Alt+Right.
NO_NAME_EXEMPT = {
    "AppShortcuts.qml",
    "BlockDragController.qml",
    "BlockDragLayer.qml",
    "BlockGapCursor.qml",
    "CrossBlockTextDrag.qml",
    "CrossBlockTextSelection.qml",
    "EdgeAutoScroller.qml",
    "EditorDropArea.qml",
    "RenderedTextSelection.qml",
    "SelectableText.qml",
}

# Glyph labels that are content rather than a control's name.
#
# DiagramBlock and MathBlock draw notation; TocBlock and QueryBlock render
# document structure. A one-character label in those is part of what the note
# says, and renaming it would be renaming the note's content.
GLYPH_EXEMPT = {
    "DiagramBlock.qml",
    "MathBlock.qml",
    "MathCommandMenu.qml",
    "MathEntryAssist.qml",
    "QueryBlock.qml",
    "TocBlock.qml",
}

# Animations that are deliberately unscaled.
#
# Empty today: every duration in the tree runs through Theme.motionScale. A
# file belongs here only when the animation is load-bearing rather than
# decorative — an animation whose end state is the correct rendering, which
# an instant duration would skip past.
MOTION_EXEMPT = set()


def scopes(lines):
    """Yield (open_line_index, type_name, body_line_indices) per object scope.

    A brace counter rather than a parser: QML's block structure is regular
    enough for this, and the alternative is a dependency. Strings and comments
    can carry unbalanced braces, so both are blanked first.
    """
    cleaned = []
    in_block_comment = False
    for line in lines:
        text = line
        if in_block_comment:
            end = text.find("*/")
            if end < 0:
                cleaned.append("")
                continue
            text = " " * (end + 2) + text[end + 2:]
            in_block_comment = False
        # Blank string literals, then line comments, then open block comments.
        text = re.sub(r'"(?:[^"\\]|\\.)*"', '""', text)
        text = re.sub(r"'(?:[^'\\]|\\.)*'", "''", text)
        text = re.sub(r"/\*.*?\*/", " ", text)
        if "/*" in text:
            text = text[:text.index("/*")]
            in_block_comment = True
        if "//" in text:
            text = text[:text.index("//")]
        cleaned.append(text)

    open_stack = []          # (line index, type name, depth at open)
    depth = 0
    result = []
    bodies = {}
    for i, text in enumerate(cleaned):
        match = OBJECT_OPEN.match(lines[i].rstrip())
        opens = text.count("{")
        closes = text.count("}")
        if match and opens == 1 and closes == 0:
            open_stack.append((i, match.group(1), depth))
            bodies[i] = []
        for start, _name, _d in open_stack:
            bodies[start].append(i)
        depth += opens - closes
        while open_stack and depth <= open_stack[-1][2]:
            start, name, _d = open_stack.pop()
            result.append((start, name, bodies.pop(start)))
    for start, name, _d in open_stack:      # unbalanced file; report anyway
        result.append((start, name, bodies.get(start, [])))
    return result


def check_file(path, problems):
    name = os.path.basename(path)
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().splitlines()

    all_scopes = scopes(lines)
    found = []

    # A scope's OWN lines: its body minus everything belonging to a scope
    # nested inside it. The distinction is what keeps the check honest — the
    # outermost scope of a file contains every line in it, so "is there an
    # Accessible.name anywhere inside this scope" is answered yes by one name
    # on one unrelated control at the bottom of the file.
    own = {}
    for start, _type_name, body in all_scopes:
        inner = set()
        for other_start, _t, other_body in all_scopes:
            if other_start != start and (other_start in body or other_start == start):
                if other_start in body:
                    inner.add(other_start)
                    inner.update(other_body)
        own[start] = [i for i in body if i not in inner]

    def named_at_or_around(line_index):
        """Whether the item at `line_index`, or one enclosing it, is named.

        Outward rather than local, because the name usually belongs on the
        drawing rather than on the handler or the label inside it: the
        rectangle is the control, and the tap handler and the glyph are
        details of it. Only each scope's own lines count, so a name on a
        sibling elsewhere in the file does not answer for this one.
        """
        for start, _type_name, body in all_scopes:
            if line_index != start and line_index not in body:
                continue
            for i in own[start]:
                if ACCESSIBLE.search(lines[i]) or NAMING_PROPERTY.match(lines[i]):
                    return True
        return False

    # 1. A tap target whose enclosing item names nothing.
    if name not in NO_NAME_EXEMPT:
        for start, type_name, body in all_scopes:
            if type_name not in ("MouseArea", "TapHandler"):
                continue
            if not any(ACTIVATION.match(lines[i]) for i in body):
                continue
            if not named_at_or_around(start):
                found.append(
                    "%s:%d: %s with an activation handler, and no "
                    "Accessible.name on it or on anything containing it"
                    % (name, start + 1, type_name))

    # 2. A glyph label with nothing spoken beside it.
    if name not in GLYPH_EXEMPT:
        for i, line in enumerate(lines):
            match = TEXT_LITERAL.match(line)
            if not match:
                continue
            literal = match.group(1)
            if not literal or len(literal) > 2:
                continue
            if any(ch.isalnum() for ch in literal):
                continue
            if named_at_or_around(i):
                continue
            found.append(
                '%s:%d: the label "%s" is a glyph, and a screen reader '
                "reads it as that character; give the control an "
                "Accessible.name" % (name, i + 1, literal))

    # 3. An animation that ignores the reduced-motion setting.
    if name not in MOTION_EXEMPT:
        for i, line in enumerate(lines):
            if BARE_DURATION.match(line):
                found.append(
                    "%s:%d: a bare duration ignores reduced motion; write it "
                    "as `N * Theme.motionScale`" % (name, i + 1))

    # 4. A nested popup with no explicit parent, inside a dialog that
    # replaces the content item its base created.
    root_scopes = [sc for sc in all_scopes
                   if not any(sc[0] in other[2] for other in all_scopes
                              if other[0] != sc[0])]
    for root_start, root_type, root_body in root_scopes:
        if root_type != "KvitDialog":
            continue
        # Not `own` lines: `contentItem: ColumnLayout {` opens a scope of its
        # own, so it is never one of the root's own lines. What identifies it
        # is sitting one indent step in from the root.
        root_indent = len(lines[root_start]) - len(lines[root_start].lstrip())
        assigns_content = any(
            CONTENT_ITEM.match(lines[i])
            and (len(lines[i]) - len(lines[i].lstrip())) == root_indent + 4
            for i in root_body)
        if not assigns_content:
            continue
        for start, type_name, body in all_scopes:
            if start == root_start or type_name not in POPUP_TYPES:
                continue
            if start not in root_body:
                continue
            if any(PARENT_ASSIGN.match(lines[i]) for i in own[start]):
                continue
            found.append(
                "%s:%d: a %s nested in a KvitDialog that assigns its own "
                "contentItem must set `parent` explicitly, or it opens into "
                "no window and never appears (see qml/KvitDialog.qml)"
                % (name, start + 1, type_name))

    # 5. A menu entry that cannot show its own disabled state.
    if name != "DiscoverableMenuItem.qml":
        for start, type_name, body in all_scopes:
            if type_name == "MenuItem":
                found.append(
                    "%s:%d: a bare MenuItem draws a disabled command in the "
                    "same color as a live one; use DiscoverableMenuItem"
                    % (name, start + 1))
                continue
            if type_name != "Menu":
                continue
            # A submenu is a Menu opening inside this one. Qt builds its row
            # in this menu from this menu's delegate, so a menu that has one
            # has to say what that row is made of.
            has_submenu = any(
                other_type == "Menu" and other_start != start
                and other_start in body
                for other_start, other_type, _b in all_scopes)
            if not has_submenu:
                continue
            if any(MENU_DELEGATE.match(lines[i]) for i in own[start]):
                continue
            found.append(
                "%s:%d: this menu holds a submenu, whose row Qt builds from "
                "the menu's delegate; add `delegate: DiscoverableMenuItem {}` "
                "so a disabled submenu greys out" % (name, start + 1))

    # One report per problem: the scope walk visits an item once per scope it
    # sits inside, so the same line can be reached several times.
    for problem in sorted(set(found), key=found.index):
        if problem not in problems:
            problems.append(problem)


def main():
    problems = []
    for entry in sorted(os.listdir(QML)):
        if entry.endswith(".qml"):
            check_file(os.path.join(QML, entry), problems)

    if problems:
        print("Accessibility problems in qml/ (see accessibility.md):\n")
        for problem in problems:
            print("  " + problem)
        print("\n%d problem(s)." % len(problems))
        return 1
    print("check-accessible-names: qml/ is clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
