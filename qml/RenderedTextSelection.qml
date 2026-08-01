// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Selecting part of what a block drew, for the blocks that draw rather than
// edit.
//
// A paragraph puts its text in one editor, so the pointer selects it and the
// document-level range (DocumentSelection) addresses it by markdown offset. A
// web embed card, a query's result grid and a table of contents have neither:
// their text is computed for display and has no position in the note's
// markdown at all — a query's rows come from other notes entirely. Those
// blocks stay in the document-level range as whole units, exactly as before,
// and this is the second, private selection they keep alongside it: an anchor
// and a head, each a SelectableText run in this block plus a character offset
// into what that run is showing.
//
// The runs are found rather than registered. They are Repeater output over
// results that are replaced underneath the block, so a list kept between
// gestures would name items that no longer exist; the tree under `content` is
// walked at the start of every gesture instead, and the runs it finds are
// ordered by where they sit on screen — grouped into visual lines top to
// bottom, then left to right within each line. That ordering is what makes a
// sweep across a grid of cells read as one run of text, and it is what decides
// whether two runs are joined by a tab or by a newline when the selection is
// copied.
//
// The two selections are mutually exclusive, the same way the document's two
// are: starting a sweep here clears whatever DocumentSelection held, and a
// document selection starting anywhere clears this one.
Item {
    id: sel

    // The subtree holding the runs — the block's rendered card.
    property Item content: null
    // The list the block is a row of. It stops flicking while a sweep is in
    // flight, for the reason CrossBlockTextDrag gives: a downward drag with
    // enough travel is a selection here and a flick to the list, and the list
    // wins by filtering its rows' events.
    property ListView blockList: null

    // A sweep is in flight (button down, past the travel gate).
    property bool sweeping: false
    // The gesture that just ran produced a selection. The runs sit under the
    // card's own click handlers — a heading entry opens that heading, a
    // result row opens that note — and a MouseArea's onClicked fires on
    // release however far the pointer travelled, so those handlers ask this
    // before acting.
    property bool suppressClick: false

    // The selected span, ordered document-forward: run indexes into the
    // ordered run list, with a character offset into each run's shown text.
    property int startRun: -1
    property int startPos: 0
    property int endRun: -1
    property int endPos: 0
    readonly property bool hasSelection: startRun >= 0
        && (endRun > startRun || endPos > startPos)

    // Raised when a drag becomes a sweep, so the block can put the keyboard
    // on the item that answers Ctrl+C and Escape.
    signal sweepStarted()

    visible: false
    width: 0
    height: 0

    // ---- the ordered runs ----

    // The runs under `content`, in reading order, the visual line each one
    // sits on, and where each one is in the card's coordinates. All three are
    // rebuilt per gesture; nothing outside this file reads them.
    //
    // The rectangles are kept rather than measured again because hit testing
    // scans every run and a drag hit-tests on every pointer move. A query
    // showing its full row window is several hundred cells, and mapping each
    // of them through the item tree sixty times a second to answer a question
    // whose answer has not changed is the one place here where the scale of
    // these blocks would be felt. Nothing moves while a button is held.
    property var runs: []
    property var runLines: []
    property var runRects: []

    function collectRuns(item, out) {
        var kids = item.children
        for (var i = 0; i < kids.length; i++) {
            var kid = kids[i]
            if (!kid.visible)
                continue
            if (kid.isSelectableRun === true) {
                if (kid.runLength > 0)
                    out.push(kid)
            } else {
                sel.collectRuns(kid, out)
            }
        }
    }

    function rebuildRuns() {
        var found = []
        // A card that is not on screen has nothing to select, and a card can
        // be in the tree with everything still in it while hidden.
        if (sel.content && sel.content.visible)
            sel.collectRuns(sel.content, found)
        var placed = []
        for (var i = 0; i < found.length; i++) {
            var where = found[i].mapToItem(sel.content, 0, 0)
            placed.push({ run: found[i], x: where.x, y: where.y,
                          w: found[i].width,
                          h: Math.max(1, found[i].height) })
        }
        placed.sort(function(a, b) { return a.y - b.y })

        // Group into visual lines before ordering left to right. Sorting by
        // "same line, then x" in one comparator is not an ordering — three
        // runs can each be within tolerance of the next and not of each
        // other — and an inconsistent comparator gives a different answer
        // every time the grid is redrawn.
        var ordered = []
        var lines = []
        var rects = []
        var group = []
        var line = -1
        var lineY = -1e9
        var lineHeight = 1
        function flushGroup() {
            group.sort(function(a, b) { return a.x - b.x })
            for (var k = 0; k < group.length; k++) {
                ordered.push(group[k].run)
                lines.push(line)
                rects.push(group[k])
            }
            group = []
        }
        for (i = 0; i < placed.length; i++) {
            if (placed[i].y >= lineY + lineHeight * 0.5) {
                flushGroup()
                line++
                lineY = placed[i].y
                lineHeight = placed[i].h
            }
            group.push(placed[i])
        }
        flushGroup()
        sel.runs = ordered
        sel.runLines = lines
        sel.runRects = rects
    }

    // ---- hit testing ----

    // The run and offset under a scene point. Vertical distance dominates, so
    // a point beside a grid resolves to the nearest cell on the line it is
    // level with rather than to whatever cell happens to be closest by
    // straight-line distance; a point off the ends of the block resolves to
    // the first or last run, and the run clamps the offset into itself.
    function hitAt(sceneX, sceneY) {
        if (sel.runs.length === 0)
            return null
        var here = sel.content.mapFromItem(null, sceneX, sceneY)
        var best = -1
        var bestScore = Infinity
        for (var i = 0; i < sel.runRects.length; i++) {
            var at = sel.runRects[i]
            var above = at.y - here.y
            var below = here.y - (at.y + at.h)
            var left = at.x - here.x
            var right = here.x - (at.x + at.w)
            var dy = Math.max(0, above, below)
            var dx = Math.max(0, left, right)
            var score = dy * 10000 + dx
            if (score < bestScore) {
                bestScore = score
                best = i
            }
        }
        if (best < 0)
            return null
        return { run: best, pos: sel.runs[best].positionAtPoint(sceneX, sceneY) }
    }

    // ---- word boundaries ----

    // Three classes, as DocumentSelection::wordStart uses: word characters,
    // whitespace, everything else. A boundary walk stays inside one class,
    // which is what makes a double-click on a space take the run of spaces
    // rather than nothing.
    function charClass(ch) {
        if (ch === undefined || ch === "")
            return 0
        if (ch === " " || ch === "\t" || ch === "\n" || ch === "\r")
            return 1
        if (ch === "_" || (ch >= "0" && ch <= "9"))
            return 2
        // A letter in any script: the only characters that differ between
        // their two cases. Testing for [A-Za-z] would break every language
        // that is not English.
        return ch.toLowerCase() !== ch.toUpperCase() ? 2 : 3
    }
    function wordStart(text, pos) {
        var i = Math.max(0, Math.min(pos, text.length))
        var cls = sel.charClass(text.charAt(Math.max(0, i - 1)))
        while (i > 0 && sel.charClass(text.charAt(i - 1)) === cls)
            i--
        return i
    }
    function wordEnd(text, pos) {
        var i = Math.max(0, Math.min(pos, text.length))
        var cls = sel.charClass(text.charAt(i))
        while (i < text.length && sel.charClass(text.charAt(i)) === cls)
            i++
        return i
    }

    // ---- the gesture ----

    property bool pressActive: false
    property bool engaged: false
    property real pressX: 0
    property real pressY: 0
    property int anchorRun: -1
    property int anchorPos: 0
    // The anchor's extent at the granularity the press chose: a word for a
    // double-click, the whole run for a third click. Dragging away from a
    // double-click keeps the whole first word selected, which is what it does
    // in a paragraph.
    property int anchorLow: 0
    property int anchorHigh: 0
    property int headRun: -1
    property int headPos: 0
    property int clicks: 1
    property double lastPressAt: 0
    property real lastPressX: -1e9
    property real lastPressY: -1e9

    function beginPress(sceneX, sceneY) {
        sel.suppressClick = false
        sel.pressActive = true
        sel.engaged = false
        sel.pressX = sceneX
        sel.pressY = sceneY

        var now = Date.now()
        var near = Math.abs(sceneX - sel.lastPressX) < 8
                && Math.abs(sceneY - sel.lastPressY) < 8
        sel.clicks = (now - sel.lastPressAt < 400 && near)
            ? Math.min(sel.clicks + 1, 3) : 1
        sel.lastPressAt = now
        sel.lastPressX = sceneX
        sel.lastPressY = sceneY

        sel.rebuildRuns()
        sel.clearSpan()
        var hit = sel.hitAt(sceneX, sceneY)
        if (!hit) {
            sel.pressActive = false
            return
        }
        sel.anchorRun = hit.run
        sel.headRun = hit.run
        sel.anchorPos = hit.pos
        sel.headPos = hit.pos
        var run = sel.runs[hit.run]
        if (sel.clicks === 1) {
            sel.anchorLow = hit.pos
            sel.anchorHigh = hit.pos
            return
        }
        if (sel.clicks === 2) {
            sel.anchorLow = sel.wordStart(run.runText, hit.pos)
            sel.anchorHigh = sel.wordEnd(run.runText, hit.pos)
        } else {
            sel.anchorLow = 0
            sel.anchorHigh = run.runLength
        }
        // A double or triple click selects on the press, without waiting for
        // travel it is never going to see.
        sel.engage()
        sel.commitSpan()
    }

    function updatePress(sceneX, sceneY) {
        if (!sel.pressActive || sel.anchorRun < 0)
            return
        if (!sel.engaged) {
            // The same five-pixel gate the block drag and the cross-block
            // text drag use: a click is rarely pixel-identical between press
            // and release, and without a gate that alone paints a selection.
            if (Math.abs(sceneX - sel.pressX) < 5
                && Math.abs(sceneY - sel.pressY) < 5)
                return
            sel.engage()
        }
        var hit = sel.hitAt(sceneX, sceneY)
        if (!hit)
            return
        sel.headRun = hit.run
        sel.headPos = hit.pos
        sel.commitSpan()
    }

    function endPress() {
        sel.pressActive = false
        if (sel.sweeping && sel.blockList)
            sel.blockList.interactive = true
        sel.sweeping = false
        if (sel.hasSelection)
            sel.suppressClick = true
    }

    function engage() {
        sel.engaged = true
        sel.sweeping = true
        if (sel.blockList)
            sel.blockList.interactive = false
        if (DocumentSelection.hasBlockSelection
            || DocumentSelection.hasTextSelection)
            DocumentSelection.clear()
        sel.sweepStarted()
    }

    // ---- painting the span ----

    function commitSpan() {
        if (sel.anchorRun < 0 || sel.headRun < 0)
            return
        var forward = sel.headRun > sel.anchorRun
            || (sel.headRun === sel.anchorRun && sel.headPos >= sel.anchorPos)
        var head = sel.runs[sel.headRun]
        if (sel.clicks >= 2) {
            var outward = sel.clicks === 3
                ? (forward ? head.runLength : 0)
                : (forward ? sel.wordEnd(head.runText, sel.headPos)
                           : sel.wordStart(head.runText, sel.headPos))
            if (forward) {
                sel.startRun = sel.anchorRun; sel.startPos = sel.anchorLow
                sel.endRun = sel.headRun; sel.endPos = outward
            } else {
                sel.startRun = sel.headRun; sel.startPos = outward
                sel.endRun = sel.anchorRun; sel.endPos = sel.anchorHigh
            }
        } else if (forward) {
            sel.startRun = sel.anchorRun; sel.startPos = sel.anchorPos
            sel.endRun = sel.headRun; sel.endPos = sel.headPos
        } else {
            sel.startRun = sel.headRun; sel.startPos = sel.headPos
            sel.endRun = sel.anchorRun; sel.endPos = sel.anchorPos
        }
        sel.paintSpan()
    }

    function paintSpan() {
        for (var i = 0; i < sel.runs.length; i++) {
            var run = sel.runs[i]
            if (i < sel.startRun || i > sel.endRun)
                run.deselectRun()
            else if (i === sel.startRun && i === sel.endRun)
                run.selectRange(sel.startPos, sel.endPos)
            else if (i === sel.startRun)
                run.selectRange(sel.startPos, run.runLength)
            else if (i === sel.endRun)
                run.selectRange(0, sel.endPos)
            else
                run.selectRange(0, run.runLength)
        }
    }

    // Drop the span without touching the run list, for the paths that have
    // just rebuilt it.
    function clearSpan() {
        for (var i = 0; i < sel.runs.length; i++) {
            // A query's results and a table of contents are replaced under
            // the block, taking their runs with them, and a selection can
            // outlive them by the frame it takes the clear to arrive. Reading
            // a property of a destroyed item throws, and there is nothing to
            // deselect on one, so the loop goes on to the runs that are still
            // there.
            try {
                sel.runs[i].deselectRun()
            } catch (e) {
            }
        }
        sel.startRun = -1
        sel.startPos = 0
        sel.endRun = -1
        sel.endPos = 0
        sel.anchorRun = -1
        sel.headRun = -1
    }

    function clear() {
        sel.clearSpan()
        sel.pressActive = false
        sel.engaged = false
        if (sel.sweeping && sel.blockList)
            sel.blockList.interactive = true
        sel.sweeping = false
        sel.suppressClick = false
    }

    function selectAll() {
        sel.rebuildRuns()
        if (sel.runs.length === 0)
            return false
        sel.startRun = 0
        sel.startPos = 0
        sel.endRun = sel.runs.length - 1
        sel.endPos = sel.runs[sel.endRun].runLength
        sel.anchorRun = 0
        sel.anchorPos = 0
        sel.headRun = sel.endRun
        sel.headPos = sel.endPos
        sel.clicks = 1
        sel.paintSpan()
        return true
    }

    function everythingSelected() {
        var last = sel.runs[sel.runs.length - 1]
        return sel.hasSelection && last !== undefined
            && sel.startRun === 0 && sel.startPos === 0
            && sel.endRun === sel.runs.length - 1
            && sel.endPos === last.runLength
    }

    // ---- what comes out ----

    // The selected text as the reader sees it laid out: cells that share a
    // visual line are joined by a tab, and a new line starts a new line. A
    // partial selection has no markdown to offer — half of an `![](url)`
    // expression is nothing, and a query's rows are not in the note at all —
    // so this is the only payload, and it goes to the clipboard as plain
    // text.
    function selectedText() {
        if (!sel.hasSelection)
            return ""
        var out = ""
        for (var i = sel.startRun; i <= sel.endRun; i++) {
            var run = sel.runs[i]
            // The span is dropped whenever the runs can change under it, so
            // this is only reachable if one of those paths is ever missed;
            // stopping at the gap beats throwing out of a copy.
            if (run === undefined)
                break
            var from = i === sel.startRun ? sel.startPos : 0
            var to = i === sel.endRun ? sel.endPos : run.runLength
            if (i > sel.startRun)
                out += sel.runLines[i] === sel.runLines[i - 1] ? "\t" : "\n"
            out += run.slice(from, to)
        }
        return out
    }

    function copySelection() {
        var text = sel.selectedText()
        if (text === "")
            return false
        Clipboard.text = text
        return true
    }

    // Escape, Ctrl+C and Ctrl+A over a rendered selection. The block's own
    // focus item calls this first, the way it calls handleContextMenuKey.
    function handleSelectionKey(event) {
        var ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        if (event.key === Qt.Key_Escape && sel.hasSelection) {
            sel.clearSpan()
            event.accepted = true
            return true
        }
        if (ctrl && event.key === Qt.Key_C && sel.hasSelection) {
            sel.copySelection()
            event.accepted = true
            return true
        }
        // Ctrl+A takes this block's rendered text first and the document
        // second, which is the two stages Ctrl+A already has inside a
        // paragraph. A block with nothing drawn in it has no first stage.
        if (ctrl && event.key === Qt.Key_A && !sel.everythingSelected()) {
            if (sel.selectAll()) {
                if (DocumentSelection.hasBlockSelection
                    || DocumentSelection.hasTextSelection)
                    DocumentSelection.clear()
                event.accepted = true
                return true
            }
        }
        return false
    }

    // A selection anywhere in the document ends this one: the two are as
    // mutually exclusive as the document's own two are. The clear this makes
    // during engage() bumps the revision too, and finds neither kind set,
    // which is why engaging does not immediately undo itself.
    Connections {
        target: DocumentSelection
        function onRevisionChanged() {
            if (DocumentSelection.hasBlockSelection
                || DocumentSelection.hasTextSelection)
                sel.clear()
        }
    }
}
