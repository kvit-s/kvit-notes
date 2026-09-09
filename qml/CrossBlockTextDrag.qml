// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Cross-block text selection, mouse path (features.md §2.5, §21.3).
//
// Each block's TextArea hosts a passive PointHandler that reports its presses
// and drag moves here. An ancestor-level handler never sees a press the
// TextArea accepts, which a feasibility test pinned, but the TextArea's own
// handlers do, and a passive grab keeps reporting moves while the TextArea
// drags its native in-block selection — which is the anchor block's portion
// of the range. This coordinator engages when the pointer crosses into
// another block and disengages when it returns, and while engaged it feeds
// the edge auto-scroller. Presses in the gutter never reach a TextArea, so
// they never seed a text drag.
//
// blockPositionAt is here because this is its only caller: resolving a scene
// point to a block and a markdown offset is how a drag knows what it is over.
QtObject {
    id: textDrag

    // The editor this belongs to, and the document it is showing.
    property BlockEditorSurface editor: null
    readonly property BlockModel blocks:
        textDrag.editor ? textDrag.editor.blocks : BlockModel
    readonly property DocumentSelection selection:
        textDrag.editor ? textDrag.editor.selection : DocumentSelection

    // Wired by main.qml.
    property var listView
    property var scroller

    property int pressIndex: -1
    property int pressMd: 0
    property bool engaged: false
    property int clickCount: 1
    property double lastPressAt: 0
    property real lastPressX: 0
    property real lastPressY: 0

    function beginPress(index, mdPos, sceneX, sceneY) {
        // Text under the pointer owns the drag. The list flicks on a drag
        // with enough vertical travel and filters its rows' mouse events to
        // do it, so it took the pointer away from the editor the moment a
        // downward drag passed the threshold — which is the gesture for
        // selecting a paragraph. The editor then never started a selection:
        // the drag highlighted nothing and the caret simply landed wherever
        // the button came up, differently every attempt. Sideways drags,
        // which the list has no use for, always worked, and that difference
        // is what the reports of "random" selection were.
        //
        // Scrolling is untouched: the wheel, the scrollbar and the edge
        // auto-scroller all move contentY without the list's drag handling.
        // endPress puts it back, and so would the next press in any block's
        // text, so a lost release cannot leave the list stuck.
        if (textDrag.listView)
            textDrag.listView.interactive = false

        // Click multiplicity sets the drag granularity (§21.3):
        // 1 character, 2 word, 3 whole-block
        var now = Date.now()
        var near = Math.abs(sceneX - lastPressX) < 8
                && Math.abs(sceneY - lastPressY) < 8
        clickCount = (now - lastPressAt < 400 && near)
            ? Math.min(clickCount + 1, 3) : 1
        lastPressAt = now
        lastPressX = sceneX
        lastPressY = sceneY
        pressIndex = index
        pressMd = mdPos
        engaged = false
    }

    function update(sceneX, sceneY) {
        if (pressIndex < 0)
            return
        // A plain click is rarely pixel-identical between press and release,
        // and focusing the clicked block collapses the revealed markers of the
        // previously-focused one, reflowing everything under a still-held
        // pointer. Either can remap the point to a different block index; with
        // no travel gate that alone seeds a full multi-block selection from a
        // click. Require real pointer travel before engaging, matching the 5px
        // gate the block-drag gesture already uses (BlockGutter.qml §).
        var moved = Math.abs(sceneX - lastPressX) >= 5
                 || Math.abs(sceneY - lastPressY) >= 5
        var hit = textDrag.blockPositionAt(sceneX, sceneY)
        if (hit) {
            if (!engaged && moved && hit.index !== pressIndex) {
                textDrag.selection.beginTextSelection(pressIndex, pressMd,
                    clickCount >= 3 ? 2 : clickCount === 2 ? 1 : 0)
                engaged = true
            }
            if (engaged) {
                if (hit.index === pressIndex) {
                    // Back inside the anchor block: the native
                    // in-block selection takes over again
                    textDrag.selection.clearTextSelection()
                    engaged = false
                } else {
                    textDrag.selection.updateTextSelectionHead(
                        hit.index, hit.mdPos)
                }
            }
        }
        if (engaged) {
            textDrag.scroller.pointerY =
                textDrag.listView.mapFromItem(null, sceneX, sceneY).y
            textDrag.scroller.active = true
        } else {
            textDrag.scroller.active = false
        }
    }

    function endPress() {
        if (textDrag.listView)
            textDrag.listView.interactive = true
        // The anchor block painted its share of the range with its own
        // native selection while the drag ran, and the release collapses
        // that selection to the press point: Qt holds a press in a text area
        // back until it knows the gesture was not a flick, and replays it on
        // the release. That left the block a drag started in blank while
        // every block after it stayed highlighted, which is what "the
        // selection starts at the end of the first block" was.
        if (engaged && pressIndex >= 0 && textDrag.listView) {
            var anchorRow = (textDrag.listView.itemAtIndex(pressIndex)
                             as BlockDelegateBase)
            if (anchorRow)
                anchorRow.reapplySelectionPortion()
        }
        pressIndex = -1
        engaged = false
        textDrag.scroller.active = false
    }

    // Map a scene point to {index, mdPos, inText} on the block list.
    // Pointer positions above, below, or between blocks resolve to the
    // nearest block edge so a selection drag never loses its target.
    function blockPositionAt(sceneX, sceneY) {
        if (!textDrag.blocks || textDrag.blocks.count === 0)
            return null
        var pos = textDrag.listView.contentItem.mapFromItem(null, sceneX, sceneY)
        if (pos.y < 0)
            return { index: 0, mdPos: 0, inText: false }
        if (pos.y >= textDrag.listView.contentHeight) {
            var last = textDrag.blocks.count - 1
            return { index: last, mdPos: textDrag.blocks.getContent(last).length,
                     inText: false }
        }
        var cx = Math.max(1, Math.min(pos.x, textDrag.listView.width - 1))
        var idx = textDrag.listView.indexAt(cx, pos.y)
        if (idx < 0) {
            // In the spacing gap: attach to the block just above
            idx = textDrag.listView.indexAt(
                cx, Math.max(0, pos.y - textDrag.listView.spacing))
            if (idx < 0)
                return null
            return { index: idx, mdPos: textDrag.blocks.getContent(idx).length,
                     inText: false }
        }
        var item = (textDrag.listView.itemAtIndex(idx) as BlockDelegateBase)
        if (!item || !item.markdownPositionAt)
            return { index: idx, mdPos: 0, inText: false }
        return { index: idx,
                 mdPos: item.markdownPositionAt(sceneX, sceneY),
                 inText: item.pointInText ? item.pointInText(sceneX, sceneY) : false }
    }
}
