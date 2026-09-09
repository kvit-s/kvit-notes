// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// Completing a `[[wiki-link]]` as it is typed (features.md §3.5): the second
// bracket opens the note picker, further typing filters it, and choosing a
// row rewrites the whole run as a finished link.
//
// The run is found by looking at the text rather than by remembering where
// the brackets went in. Reveal transitions rewrite a block's document, so any
// offset stored when the menu opened would be stale a keystroke later,
// whereas the "[[…" ending at the caret is always there to be read.
QtObject {
    id: root

    // The editor being typed into, which is also the host the shared menu is
    // opened for.
    property TextArea textEditor: null
    // The engine, asked only whether the caret is inside a math span, where
    // brackets belong to the formula.
    property BlockEditorEngine engine: null
    // The editing surface that owns the shared menu; one serves every block.
    property BlockEditorSurface editor: null
    // Code blocks type brackets literally.
    property bool verbatim: false

    // The menu while it is open FOR THIS EDITOR, else null.
    function activeMenu() {
        return root.editor ? root.editor.activeWikiMenu(root.textEditor) : null
    }

    function openMenu() {
        var rect = root.textEditor.positionToRectangle(root.textEditor.cursorPosition)
        var topLeft = root.textEditor.mapToItem(null, rect.x, rect.y)
        AppActions.requestWikiLinkMenu(root.textEditor,
            Qt.rect(topLeft.x, topLeft.y, rect.width, rect.height))
        root.syncQuery()
    }

    // The "[[…" run ending at the caret — {trigger, query} or null.
    function wordAtCaret() {
        var text = root.textEditor.text
        var pos = root.textEditor.cursorPosition
        var open = text.lastIndexOf("[[", Math.max(0, pos - 2))
        if (open < 0 || open + 2 > pos)
            return null
        var between = text.substring(open + 2, pos)
        if (between.indexOf("]]") >= 0
            || between.indexOf("[") >= 0
            || between.indexOf("\n") >= 0)
            return null
        return { trigger: open, query: between }
    }

    // Recompute the query; when the caret no longer sits in an open "[[…"
    // run (trigger deleted, "]]" typed, caret moved away) the menu closes
    // with the text kept.
    function syncQuery() {
        var menu = root.activeMenu()
        if (!menu)
            return
        var word = root.wordAtCaret()
        if (!word) {
            menu.dismiss()
            return
        }
        menu.updateQuery(word.query)
    }

    function releaseOnFocusLoss() {
        var menu = root.activeMenu()
        if (menu)
            menu.dismiss()
    }

    // Insertion (the menu hands the chosen row here): replace the whole
    // "[[…" run with the completed link, caret after the closing "]]".
    function applyCompletion(row) {
        var ed = root.textEditor
        var word = root.wordAtCaret()
        if (!word)
            return
        var insertText
        if (row.kind === "heading") {
            var hashIdx = word.query.indexOf("#")
            var targetPart = hashIdx >= 0
                ? word.query.substring(0, hashIdx) : ""
            insertText = "[[" + targetPart + "#"
                         + row.heading + "]]"
        } else {
            insertText = "[[" + row.target + "]]"
        }
        var start = word.trigger
        var end = ed.cursorPosition
        ed.remove(start, end)
        ed.insert(start, insertText)
        ed.cursorPosition = start + insertText.length
        ed.forceActiveFocus()
    }

    // While the menu targets this editor it owns navigation; Enter is
    // claimed in handleReturn. Everything else keeps typing, which feeds the
    // query. Returns true when the key was consumed.
    function handleMenuKey(event) {
        var menu = root.activeMenu()
        if (!menu)
            return false
        if (event.key === Qt.Key_Down) {
            menu.highlightNext()
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_Up) {
            menu.highlightPrevious()
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_Tab) {
            menu.applyHighlighted()
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_Escape) {
            menu.dismiss()
            event.accepted = true
            return true
        }
        return false
    }

    // Enter completes the highlighted note or heading. With no match the
    // menu just closes and Enter splits the block as usual — the typed link
    // is already valid text.
    function handleReturn(event) {
        var menu = root.activeMenu()
        if (!menu)
            return false
        if (menu.highlightIndex >= 0) {
            menu.applyHighlighted()
        } else {
            menu.dismiss()
        }
        event.accepted = true
        return true
    }

    // The second "[" of the trigger — prose only, with somewhere for a wiki
    // link to resolve to, and never inside a math span. It is inserted by
    // hand so the menu opens on a settled document.
    function handleBracket(event) {
        var ed = root.textEditor
        if (event.text !== "[" || root.verbatim
            || !root.editor || !root.editor.linkResolver
            || root.activeMenu()
            || ed.selectionStart !== ed.selectionEnd
            || ed.cursorPosition <= 0
            || ed.text.charAt(ed.cursorPosition - 1) !== "["
            || root.engine.mathSpanRangeAt(ed.cursorPosition).found)
            return false
        var bracketPos = ed.cursorPosition
        ed.insert(bracketPos, "[")
        ed.cursorPosition = bracketPos + 1
        root.openMenu()
        event.accepted = true
        return true
    }
}
