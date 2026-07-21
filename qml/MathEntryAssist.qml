// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// Typing mathematics inside a prose block: the `$…$` auto-pair, the backslash
// command menu, and the Tab chain that walks the empty slots a template
// leaves behind (features.md §1.2.15).
//
// All three are one subject because they share two pieces of transient state
// that only exist between keystrokes. dollarPairOpenPos remembers where an
// auto-inserted pair opened, which is what makes Backspace remove both
// dollars, Delete leave a literal one, and a typed `$` step over the closer
// instead of adding a third. slotChain remembers that the last insertion was
// a template, which is what gives Tab a meaning other than indenting a list.
// Both are dropped the moment the caret leaves the construct they describe.
//
// Positions here are document offsets, never stored across edits. A reveal
// transition rewrites the document — the span materializing around a fresh
// "$\a$" bounces every offset in the block — so the menu query is derived
// from the text at the caret each time it is needed rather than from a
// remembered trigger position.
QtObject {
    id: root

    // The editor being typed into. Its own document is the subject, and the
    // menu is opened against it as the host, so the same editor has to be
    // both the thing read and the thing named.
    property TextArea editor: null
    // The engine, for the questions only it can answer: where the math span
    // under the caret runs, and whether a `$` here should auto-pair at all.
    property BlockEditorEngine engine: null
    // The window that owns the shared command menu. One menu serves every
    // block, so "is it mine" is a question about the host it was opened for.
    property KvitShell shell: null
    // Code blocks type dollars and backslashes literally.
    property bool verbatim: false

    // Armed by a template insertion; Tab hops between the empty {} / [] pairs
    // inside the current math span while it holds.
    property bool slotChain: false
    // Document position of the opening $ of a freshly auto-paired $$, else -1.
    property int dollarPairOpenPos: -1

    // The command menu while it is open FOR THIS EDITOR, else null.
    function activeMenu() {
        return root.shell ? root.shell.activeMathMenu(root.editor) : null
    }

    function openMenu() {
        var rect = root.editor.positionToRectangle(root.editor.cursorPosition)
        var topLeft = root.editor.mapToItem(null, rect.x, rect.y)
        AppActions.requestMathCommandMenu(root.editor,
            Qt.rect(topLeft.x, topLeft.y, rect.width, rect.height),
            false /* inline context: single-line templates */)
        root.syncQuery()
    }

    // The pair's closing $ position while tracking is valid for the
    // current caret, else -1: the opening $ still stands, the caret is
    // inside the pair, and the next $ is at or right of the caret.
    function dollarPairClosePos() {
        var open = root.dollarPairOpenPos
        if (open < 0 || open >= root.editor.text.length
            || root.editor.text.charAt(open) !== "$")
            return -1
        if (root.editor.cursorPosition <= open)
            return -1
        var close = root.editor.text.indexOf("$", open + 1)
        if (close < 0 || close < root.editor.cursorPosition)
            return -1
        return close
    }

    // The backslash-word ending at the caret — {trigger, query} or null.
    function wordAtCaret() {
        var text = root.editor.text
        var pos = root.editor.cursorPosition
        // TeX control symbols are one non-letter character.
        // These five are the symbol-style commands in the menu.
        if (pos >= 2 && text.charAt(pos - 2) === "\\"
            && /^[|,;:!]$/.test(text.charAt(pos - 1))) {
            return { trigger: pos - 2,
                     query: text.charAt(pos - 1) }
        }
        var s = pos
        while (s > 0 && /[A-Za-z]/.test(text.charAt(s - 1)))
            s--
        if (s === 0 || text.charAt(s - 1) !== "\\")
            return null
        // A second backslash right after the trigger is the
        // "\\" (row break) query.
        if (s === pos && s > 1 && text.charAt(s - 2) === "\\")
            return { trigger: s - 2, query: "\\" }
        return { trigger: s - 1, query: text.substring(s, pos) }
    }

    // Recompute the menu query; when the caret no longer ends a
    // backslash-word (trigger deleted, caret moved away, non-letter typed)
    // the menu closes with the text kept.
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

    // Bring the query and the pair tracking back in step with the caret.
    // Callers must defer this: during an edit the editor's caret signal
    // arrives before its text property reflects the same edit, so reading
    // both immediately sees an inconsistent snapshot and would mis-dismiss.
    function settleState() {
        root.syncQuery()
        if (root.dollarPairOpenPos >= 0 && root.dollarPairClosePos() < 0)
            root.dollarPairOpenPos = -1
    }

    // Whether anything is being tracked, which is what decides whether a
    // keystroke needs the deferred settle at all.
    function tracking() {
        if (root.activeMenu())
            return true
        return root.dollarPairOpenPos >= 0
    }

    // The editor lost focus: nothing here survives it.
    function releaseOnFocusLoss() {
        root.dollarPairOpenPos = -1
        root.slotChain = false
        var menu = root.activeMenu()
        if (menu)
            menu.dismiss()
    }

    // Insertion (the menu hands the chosen row here): replace the
    // backslash-word at the caret with the template, caret into the first
    // slot, arm the Tab chain. Inline context always takes the single-line
    // form.
    function applyCommand(row) {
        var ed = root.editor
        var insertText = row.insert
        var offset = row.cursorOffset
        var word = root.wordAtCaret()
        var start = word ? word.trigger : ed.cursorPosition
        var end = ed.cursorPosition
        // A bare command fuses with a following letter
        // (\alphax); pad with a space.
        if (offset < 0 && end < ed.text.length
            && /[A-Za-z]/.test(ed.text.charAt(end))
            && /[A-Za-z]$/.test(insertText))
            insertText += " "
        ed.remove(start, end)
        ed.insert(start, insertText)
        ed.cursorPosition = start
            + (offset >= 0 ? offset : insertText.length)
        root.slotChain = insertText.indexOf("{}") >= 0
            || insertText.indexOf("[]") >= 0
        ed.forceActiveFocus()
    }

    // The next (or previous) empty {} / [] pair inside the math span under
    // the caret; false when none is left or the caret left the span — the
    // chain ends there.
    function jumpToNextSlot(backward) {
        var ed = root.editor
        var span = root.engine.mathSpanRangeAt(ed.cursorPosition)
        if (!span.found) {
            root.slotChain = false
            return false
        }
        var from = span.docContentStart
        var to = Math.min(span.docContentEnd, ed.text.length)
        var positions = []
        for (var i = from; i + 1 < to; ++i) {
            var two = ed.text.substring(i, i + 2)
            if (two === "{}" || two === "[]")
                positions.push(i + 1)
        }
        if (positions.length === 0) {
            root.slotChain = false
            return false
        }
        if (backward) {
            for (var j = positions.length - 1; j >= 0; --j) {
                if (positions[j] < ed.cursorPosition) {
                    ed.cursorPosition = positions[j]
                    return true
                }
            }
            return false
        }
        for (var k = 0; k < positions.length; ++k) {
            if (positions[k] > ed.cursorPosition) {
                ed.cursorPosition = positions[k]
                return true
            }
        }
        root.slotChain = false
        return false
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
        if (event.key === Qt.Key_Left
            || event.key === Qt.Key_Right) {
            var consumed = event.key === Qt.Key_Left
                ? menu.moveLeft() : menu.moveRight()
            if (consumed) {
                event.accepted = true
                return true
            }
            // Completion mode: the caret moves, menu closes.
            menu.dismiss()
            return true
        }
        if (event.key === Qt.Key_Tab) {
            menu.applyHighlighted()
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_Escape) {
            // Closes the menu only; the editor keeps focus and the next
            // Escape behaves as before.
            menu.dismiss()
            event.accepted = true
            return true
        }
        return false
    }

    // Enter selects the highlighted entry while the menu targets this
    // editor.
    function handleReturn(event) {
        var menu = root.activeMenu()
        if (!menu)
            return false
        menu.applyHighlighted()
        event.accepted = true
        return true
    }

    // Backslash: the command-menu trigger — inside a math context only. The
    // gate pre-checks the span (the transitional "$\$" state is not a parsed
    // span, so a post-insertion check would miss the primary flow) and honors
    // the fresh $-pair, which is not a span until it has content. A second
    // backslash right after the trigger becomes the "\\" query instead of a
    // new one.
    function handleBackslash(event) {
        if (event.text !== "\\" || root.verbatim)
            return false
        var ed = root.editor
        var menu = root.activeMenu()
        var bsPos = ed.selectionStart
        var inMathContext = root.dollarPairClosePos() >= 0
            || root.engine.mathSpanRangeAt(bsPos).found
        if (!menu && !inMathContext)
            return false // plain prose: a literal backslash, default typing
        if (ed.selectionEnd > ed.selectionStart)
            ed.remove(ed.selectionStart, ed.selectionEnd)
        ed.insert(bsPos, "\\")
        ed.cursorPosition = bsPos + 1
        // With the menu already open the sync derives the new state (a "\\"
        // query, or a fresh trigger elsewhere in the formula).
        if (!menu)
            root.openMenu()
        event.accepted = true
        return true
    }

    // The remaining math-entry keys: re-triggering completion, the dollar
    // auto-pair and its two escape hatches, and the Tab slot chain.
    function handleEntryKey(event) {
        var ed = root.editor

        // Ctrl+Space: re-trigger completion for the backslash-word at the
        // caret, inside a math span.
        if (event.key === Qt.Key_Space
            && (event.modifiers & Qt.ControlModifier)
            && !root.verbatim
            && root.engine.mathSpanRangeAt(ed.cursorPosition).found) {
            var wordStart = ed.cursorPosition
            while (wordStart > 0
                   && /[A-Za-z]/.test(ed.text.charAt(wordStart - 1)))
                wordStart--
            if (wordStart > 0
                && ed.text.charAt(wordStart - 1) === "\\")
                root.openMenu()
            event.accepted = true
            return true
        }

        // Dollar auto-pair for entering inline math: type-over the tracked
        // closer, wrap a selection, or insert the pair with the caret
        // between — each gated by the engine's suppression rules.
        if (event.text === "$" && !root.verbatim) {
            var closePos = root.dollarPairClosePos()
            if (closePos >= 0 && closePos === ed.cursorPosition
                && ed.selectionStart === ed.selectionEnd) {
                // Types over the auto-inserted $ instead of inserting a
                // third: "$x$" typed in full.
                root.dollarPairOpenPos = -1
                ed.cursorPosition = closePos + 1
                event.accepted = true
                return true
            }
            if (ed.selectionEnd > ed.selectionStart) {
                if (root.engine.shouldAutoPairDollar(ed.selectionStart, true)) {
                    var selStart = ed.selectionStart
                    var selEnd = ed.selectionEnd
                    var wrapped = "$"
                        + ed.text.substring(selStart, selEnd) + "$"
                    ed.remove(selStart, selEnd)
                    ed.insert(selStart, wrapped)
                    ed.cursorPosition = selStart + wrapped.length
                    event.accepted = true
                    return true
                }
                // Suppressed: default replace-selection typing.
            } else if (root.engine.shouldAutoPairDollar(ed.cursorPosition)) {
                var pairPos = ed.cursorPosition
                ed.insert(pairPos, "$$")
                ed.cursorPosition = pairPos + 1
                root.dollarPairOpenPos = pairPos
                event.accepted = true
                return true
            }
            // Suppressed: a literal $, default typing.
        }

        // Backspace on the still-empty pair removes both dollars — as if the
        // keystroke never happened.
        if (event.key === Qt.Key_Backspace
            && ed.selectionStart === ed.selectionEnd
            && root.dollarPairOpenPos >= 0
            && ed.cursorPosition === root.dollarPairOpenPos + 1
            && root.dollarPairClosePos() === root.dollarPairOpenPos + 1) {
            var openPos = root.dollarPairOpenPos
            root.dollarPairOpenPos = -1
            ed.remove(openPos, openPos + 2)
            event.accepted = true
            return true
        }

        // Delete just before the tracked closer removes only the
        // auto-inserted $, leaving a literal dollar sign — the escape hatch
        // for typing $ as a character.
        if (event.key === Qt.Key_Delete
            && ed.selectionStart === ed.selectionEnd
            && root.dollarPairOpenPos >= 0) {
            var closer = root.dollarPairClosePos()
            if (closer >= 0 && closer === ed.cursorPosition) {
                root.dollarPairOpenPos = -1
                ed.remove(closer, closer + 1)
                event.accepted = true
                return true
            }
        }

        // Tab slot-chain: hop between the empty {} / [] pairs a template
        // insertion left in the math span. Runs before the list-indent Tab.
        if (root.slotChain && event.key === Qt.Key_Tab
            && !(event.modifiers & Qt.ControlModifier)) {
            if (root.jumpToNextSlot(false)) {
                event.accepted = true
                return true
            }
        }
        if (root.slotChain && event.key === Qt.Key_Backtab) {
            if (root.jumpToNextSlot(true)) {
                event.accepted = true
                return true
            }
        }
        return false
    }
}
