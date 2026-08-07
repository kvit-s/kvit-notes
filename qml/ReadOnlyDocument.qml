// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The Repeater's delegate is its own component scope; binding it lets each
// row read the surface it belongs to by id, and declares the model index it
// takes rather than relying on injection.
pragma ComponentBehavior: Bound

import QtQuick
import Kvit 1.0

// A markdown document drawn somewhere other than the editor pane
// (selection.md "A document drawn read-only").
//
// Several places in the application put a note's text on screen without being
// the editor: a stored version of the open note in the backup dialog, the
// context lines of a referring note in the backlinks pane, a search result's
// snippet. Each of them wants the same three things — the markdown rendered
// as blocks rather than shown as a paragraph of asterisks, the pointer able
// to sweep across the whole of it as one piece of text, and the copy coming
// out as markdown. This is that component.
//
// What it is made of is deliberately what the editor is made of. A surface
// takes a markdown string, parses it with the shared DocumentSerializer
// (`parse` is a pure function of its argument, so one instance serves every
// surface), and holds the result in a BlockModel of its own. A
// DocumentSelection of its own is pointed at that model, so a sweep produces
// the same anchor-and-head range the editor produces, `portionForBlock` tells
// each row what share of it to paint, and `rangeMarkdown()` is what a copy
// yields: whole blocks serialized, an inline fragment at each end. Nothing
// here addresses a singleton document, which is what lets one window hold
// several surfaces at once.
//
// Read-only is enforced rather than declared. No path from the view writes to
// the model, no undo stack is attached to it, and a surface over a file never
// opens that file for writing. A caller that wants the document edited opens
// it as a note.
//
// Sizing is by content: the rows are a Column, not a ListView, so a surface
// can sit inside a scrolling area it does not own. Every block is
// instantiated, which is acceptable at the sizes these callers have — a
// stored version of one note, a handful of context lines. If that stops being
// true it changes with evidence.
Item {
    id: surface

    // ---- in ----

    // The document to draw. Assigning re-parses and drops any selection.
    property string markdown: ""
    // The blank-line rhythm between blocks. The editor's own spacing by
    // default; a pane that wants a denser preview turns it down.
    property int blockSpacing: Typography.paragraphSpacing

    // ---- out ----

    readonly property int blockCount: docBlocks.count
    readonly property bool hasSelection: docSelection.hasTextSelection

    // The selected range as markdown: whole blocks serialized with their
    // prefixes, fences and ordinals, and a self-contained inline fragment at
    // each partially covered end. Empty when nothing is selected.
    function selectedMarkdown() { return docSelection.rangeMarkdown() }

    // Select the whole surface. Returns false when there is nothing to
    // select, which is what tells Ctrl+A to fall through.
    function selectAll() {
        var count = docBlocks.count
        if (count === 0)
            return false
        docSelection.beginTextSelection(0, 0, 2 /* whole-block granularity */)
        docSelection.updateTextSelectionHead(
            count - 1, docBlocks.getContent(count - 1).length)
        if (DocumentSelection.hasBlockSelection
            || DocumentSelection.hasTextSelection)
            DocumentSelection.clear()
        return docSelection.hasTextSelection
    }

    function clearSelection() { docSelection.clearTextSelection() }

    // Copy the selection in every clipboard flavour, as the editor's own
    // cross-block copy does (features.md §5.1). False when there was nothing
    // to copy.
    function copySelection() {
        var md = surface.selectedMarkdown()
        if (md === "")
            return false
        Clipboard.setMarkdown(md, MarkdownFormatter.toHtml(md))
        return true
    }

    // The item drawing one block, for a caller that needs the row itself
    // rather than the space it occupies. Null for an index the surface does
    // not hold.
    function blockItem(index) { return blockRows.itemAt(index) }

    // Place the rows now rather than at the next frame.
    //
    // A Column positions a child it has just been given during its next
    // polish, which the window runs as part of drawing a frame. Everything
    // that asks where a block is — a geometry query, and the sweep resolving
    // a pointer position to a row — would otherwise be answering from the
    // arrangement before the document was set, and a surface built and
    // measured in the same turn would report every row at the top.
    function forceLayout() { column.forceLayout() }

    // Where one block was drawn, in this surface's coordinates, for a caller
    // that wants to anchor something beside it. A null rectangle is the
    // answer for an index the surface does not hold.
    function blockRect(index) {
        var row = (blockRows.itemAt(index) as ReadOnlyBlock)
        if (!row)
            return Qt.rect(0, 0, 0, 0)
        surface.forceLayout()
        return Qt.rect(row.x + column.x, row.y + column.y,
                       row.width, row.height)
    }

    // The markdown of one block, which is what a caller that set ranges apart
    // by index needs to check it named the right ones.
    function blockMarkdown(index) { return docBlocks.getContent(index) }

    // The selected range as block indexes and markdown offsets:
    // {startIndex, startPos, endIndex, endPos}.
    function selectedRange() { return docSelection.orderedTextRange() }

    // ---- the sweep, as three calls ----
    //
    // The pointer is one caller of these and not a privileged one: the
    // MouseArea below forwards its three events here and does nothing else.
    // Scene coordinates, because that is what a pointer event carries and
    // what a row resolves against.
    function beginSweepAt(sceneX, sceneY) {
        // Once per gesture: nothing moves while the button is held, and a
        // press is where a stale arrangement would resolve to the wrong row.
        column.forceLayout()
        sweep.beginPress(sceneX, sceneY)
    }
    function updateSweepAt(sceneX, sceneY) { sweep.update(sceneX, sceneY) }
    function endSweep() { sweep.endPress() }

    // ---- the document, the selection, and the rows ----

    // Height follows the document; width comes from the container, since a
    // rendered document is as wide as it is given and wraps into it. An
    // implicit width taken from the rows would be circular: each row is as
    // wide as the column, and the column is as wide as this.
    implicitHeight: column.implicitHeight

    // The keyboard lands here rather than on any row: every row is switched
    // off, so this is the only item in the surface that can hold focus.
    activeFocusOnTab: true

    onMarkdownChanged: surface.reload()
    Component.onCompleted: surface.reload()

    function reload() {
        // A gesture in flight names rows that are about to go.
        sweep.endPress()
        docSelection.clearTextSelection()
        DocumentSerializer.loadIntoModel(docBlocks, surface.markdown)
    }

    // The surface's own document. `blockKindRegistry` is the shared one, so a
    // fence kind a linked module registered resolves here exactly as it does
    // in the note. No undo stack is attached, and nothing writes to it.
    DocumentBlocks {
        id: docBlocks
        blockKindRegistry: BlockKindRegistry
    }

    DocumentBlockSelection {
        id: docSelection
        model: docBlocks
    }

    Column {
        id: column
        objectName: "readOnlyDocumentColumn"
        width: surface.width
        spacing: surface.blockSpacing

        Repeater {
            id: blockRows
            model: docBlocks
            delegate: ReadOnlyBlock {
                required property int index
                width: column.width
                blockIndex: index
                selection: docSelection
            }
        }
    }

    ReadOnlyDocumentDrag {
        id: sweep
        content: column
        rows: blockRows
        blocks: docBlocks
        selection: docSelection
        onSweepStarted: surface.forceActiveFocus()
    }

    // Where the keyboard is, drawn. A surface is a tab stop, and a tab stop
    // that shows nothing when it is reached is a tab stop nobody can use.
    Rectangle {
        objectName: "readOnlyDocumentFocusRing"
        anchors.fill: parent
        visible: surface.activeFocus
        color: "transparent"
        radius: 3
        border.width: 1
        border.color: Theme.focusRing
        z: 1
    }

    // The sweep's pointer input. Declared after the rows so it is above them;
    // the rows are switched off anyway, which is belt and braces on the same
    // decision.
    MouseArea {
        id: sweepArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        // A surface usually sits inside a Flickable it does not own, and a
        // Flickable takes the grab away from its children once a drag has
        // enough travel — which is the same conflict CrossBlockTextDrag
        // resolves by stopping the block list flicking while a sweep runs.
        // Here the sweep keeps the grab outright: dragging inside a surface
        // always selects, and the pane is scrolled with the wheel or its
        // scrollbar.
        preventStealing: true
        cursorShape: Qt.IBeamCursor

        onPressed: function(mouse) {
            var p = sweepArea.mapToItem(null, mouse.x, mouse.y)
            surface.beginSweepAt(p.x, p.y)
        }
        onPositionChanged: function(mouse) {
            var p = sweepArea.mapToItem(null, mouse.x, mouse.y)
            surface.updateSweepAt(p.x, p.y)
        }
        onReleased: surface.endSweep()
        onCanceled: surface.endSweep()
        // A surface is not a control and activates nothing; the text it draws
        // carries its own name, row by row.
        Accessible.ignored: true
    }

    Keys.onPressed: function(event) {
        var ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        if (event.key === Qt.Key_Escape && surface.hasSelection) {
            surface.clearSelection()
            event.accepted = true
            return
        }
        if (ctrl && event.key === Qt.Key_C && surface.hasSelection) {
            surface.copySelection()
            event.accepted = true
            return
        }
        // Ctrl+A takes this surface first and whatever holds the key next
        // second, which is the two stages Ctrl+A already has inside a
        // paragraph and inside a rendered block.
        if (ctrl && event.key === Qt.Key_A && !surface.everythingSelected()) {
            if (surface.selectAll()) {
                event.accepted = true
                return
            }
        }
        event.accepted = false
    }

    function everythingSelected() {
        if (!docSelection.hasTextSelection || docBlocks.count === 0)
            return false
        var range = docSelection.orderedTextRange()
        var last = docBlocks.count - 1
        return range.startIndex === 0 && range.startPos === 0
            && range.endIndex === last
            && range.endPos === docBlocks.getContent(last).length
    }

    // A selection in the note ends this one. The two documents' selections
    // are as mutually exclusive as the note's own kinds are; two SURFACES are
    // not, so nothing here watches another surface.
    Connections {
        target: DocumentSelection
        function onRevisionChanged() {
            if (DocumentSelection.hasBlockSelection
                || DocumentSelection.hasTextSelection)
                surface.clearSelection()
        }
    }
}
