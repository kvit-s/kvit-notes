// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Sweeping across a drawn document (selection.md "A document drawn
// read-only").
//
// This is qml/CrossBlockTextDrag.qml for a surface rather than for the
// editor, and the two differ in one structural way. In the editor each block
// hosts a real TextArea, so a drag inside one block is Qt's own in-block
// selection and the document-level range only takes over once the pointer
// crosses into another block. Here nothing is editable and no block selects
// anything by itself, so the surface's own DocumentSelection holds every
// range from the first pixel of travel — including a range that never leaves
// the block it started in.
//
// Everything else follows the gestures already in the tree: the five-pixel
// travel gate the block drag and the cross-block drag both use, and press
// multiplicity for word and whole-block granularity.
QtObject {
    id: drag

    // The item the rows are laid out in, which is the space a scene point is
    // resolved against.
    property Item content: null
    // The rows themselves. They are Repeater output, so they are asked for by
    // index rather than kept in a list of their own.
    property Repeater rows: null
    // The surface's document and the selection over it. Neither is the
    // window's: that is the whole point of the surface.
    property DocumentBlocks blocks: null
    property DocumentBlockSelection selection: null

    // Raised when travel (or a second click) turns a press into a selection,
    // so the surface can take the keyboard for Ctrl+C and Escape.
    signal sweepStarted()

    property int pressIndex: -1
    property int pressMd: 0
    property bool engaged: false
    property int clickCount: 1
    property double lastPressAt: 0
    property real lastPressX: -1e9
    property real lastPressY: -1e9

    function beginPress(sceneX, sceneY) {
        if (!drag.selection)
            return
        // Click multiplicity sets the drag granularity, as in the editor
        // (features.md §21.3): 1 character, 2 word, 3 whole-block.
        var now = Date.now()
        var near = Math.abs(sceneX - drag.lastPressX) < 8
                && Math.abs(sceneY - drag.lastPressY) < 8
        drag.clickCount = (now - drag.lastPressAt < 400 && near)
            ? Math.min(drag.clickCount + 1, 3) : 1
        drag.lastPressAt = now
        drag.lastPressX = sceneX
        drag.lastPressY = sceneY

        var hit = drag.blockPositionAt(sceneX, sceneY)
        if (!hit) {
            drag.pressIndex = -1
            return
        }
        drag.pressIndex = hit.index
        drag.pressMd = hit.mdPos
        drag.engaged = false
        if (drag.clickCount === 1) {
            // A plain press starts nothing until the pointer has moved: a
            // click is rarely pixel-identical between press and release, and
            // without a gate that alone would paint a selection.
            drag.selection.clearTextSelection()
            return
        }
        // A double or triple click selects on the press, without waiting for
        // travel it is never going to see.
        drag.engage()
        drag.selection.updateTextSelectionHead(hit.index, hit.mdPos)
    }

    function update(sceneX, sceneY) {
        if (drag.pressIndex < 0 || !drag.selection)
            return
        if (!drag.engaged) {
            // The same five-pixel gate the block drag and the editor's
            // cross-block drag use.
            if (Math.abs(sceneX - drag.lastPressX) < 5
                && Math.abs(sceneY - drag.lastPressY) < 5)
                return
            drag.engage()
        }
        var hit = drag.blockPositionAt(sceneX, sceneY)
        if (hit)
            drag.selection.updateTextSelectionHead(hit.index, hit.mdPos)
    }

    function endPress() {
        drag.pressIndex = -1
        drag.engaged = false
    }

    function engage() {
        drag.engaged = true
        // The surface's selection and the note's are mutually exclusive, the
        // way the note's own kinds are: this is the rule selection.md states,
        // applied across the two documents. Clearing the note's bumps its
        // revision and leaves it with neither kind set, which is why the
        // surfaces watching it do not immediately clear this one back.
        if (DocumentSelection.hasBlockSelection
            || DocumentSelection.hasTextSelection)
            DocumentSelection.clear()
        drag.selection.beginTextSelection(drag.pressIndex, drag.pressMd,
            drag.clickCount >= 3 ? 2 : drag.clickCount === 2 ? 1 : 0)
        drag.sweepStarted()
    }

    // Map a scene point to {index, mdPos} on the surface. Points above,
    // below, or in the gap between two blocks resolve to the nearest block
    // edge, so a sweep never loses its target.
    function blockPositionAt(sceneX, sceneY) {
        if (!drag.blocks || !drag.content || !drag.rows)
            return null
        var count = drag.blocks.count
        if (count === 0)
            return null
        var last = count - 1
        var pos = drag.content.mapFromItem(null, sceneX, sceneY)
        if (pos.y < 0)
            return { index: 0, mdPos: 0 }
        if (pos.y >= drag.content.height)
            return { index: last, mdPos: drag.blocks.getContent(last).length }
        for (var i = 0; i < count; ++i) {
            var row = (drag.rows.itemAt(i) as ReadOnlyBlock)
            if (!row)
                continue
            if (pos.y < row.y) {
                // In the blank rhythm between two blocks: attach to the one
                // above, at its end, as the editor's coordinator does.
                if (i === 0)
                    return { index: 0, mdPos: 0 }
                return { index: i - 1,
                         mdPos: drag.blocks.getContent(i - 1).length }
            }
            if (pos.y <= row.y + row.height) {
                return { index: i,
                         mdPos: row.markdownPositionAt(sceneX, sceneY) }
            }
        }
        return { index: last, mdPos: drag.blocks.getContent(last).length }
    }
}
