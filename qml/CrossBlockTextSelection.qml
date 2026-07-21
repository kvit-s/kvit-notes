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
    id: selection

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
        var hit = selection.blockPositionAt(sceneX, sceneY)
        if (hit) {
            if (!engaged && hit.index !== pressIndex) {
                DocumentSelection.beginTextSelection(pressIndex, pressMd,
                    clickCount >= 3 ? 2 : clickCount === 2 ? 1 : 0)
                engaged = true
            }
            if (engaged) {
                if (hit.index === pressIndex) {
                    // Back inside the anchor block: the native
                    // in-block selection takes over again
                    DocumentSelection.clearTextSelection()
                    engaged = false
                } else {
                    DocumentSelection.updateTextSelectionHead(
                        hit.index, hit.mdPos)
                }
            }
        }
        if (engaged) {
            selection.scroller.pointerY =
                selection.listView.mapFromItem(null, sceneX, sceneY).y
            selection.scroller.active = true
        } else {
            selection.scroller.active = false
        }
    }

    function endPress() {
        pressIndex = -1
        engaged = false
        selection.scroller.active = false
    }

    // Map a scene point to {index, mdPos, inText} on the block list.
    // Pointer positions above, below, or between blocks resolve to the
    // nearest block edge so a selection drag never loses its target.
    function blockPositionAt(sceneX, sceneY) {
        if (!BlockModel || BlockModel.count === 0)
            return null
        var pos = selection.listView.contentItem.mapFromItem(null, sceneX, sceneY)
        if (pos.y < 0)
            return { index: 0, mdPos: 0, inText: false }
        if (pos.y >= selection.listView.contentHeight) {
            var last = BlockModel.count - 1
            return { index: last, mdPos: BlockModel.getContent(last).length,
                     inText: false }
        }
        var cx = Math.max(1, Math.min(pos.x, selection.listView.width - 1))
        var idx = selection.listView.indexAt(cx, pos.y)
        if (idx < 0) {
            // In the spacing gap: attach to the block just above
            idx = selection.listView.indexAt(
                cx, Math.max(0, pos.y - selection.listView.spacing))
            if (idx < 0)
                return null
            return { index: idx, mdPos: BlockModel.getContent(idx).length,
                     inText: false }
        }
        var item = (selection.listView.itemAtIndex(idx) as BlockDelegateBase)
        if (!item || !item.markdownPositionAt)
            return { index: idx, mdPos: 0, inText: false }
        return { index: idx,
                 mdPos: item.markdownPositionAt(sceneX, sceneY),
                 inText: item.pointInText ? item.pointInText(sceneX, sceneY) : false }
    }
}
