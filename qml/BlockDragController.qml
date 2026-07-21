// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Block drag-and-drop reordering (features.md §3.2, §5.4).
//
// A single-block drag live-moves the row as the pointer passes each
// neighbour's midpoint, using preview moves that bypass the undo stack while
// the ListView's move and displaced transitions animate the room being made;
// the drop then commits one pre-applied command, so the whole gesture is a
// single Ctrl+Z. Escape puts the row back and leaves nothing on the stack.
//
// A multi-block drag cannot live-move a discontiguous selection, so it leaves
// the blocks where they are, shows a gap indicator, and commits one compound
// move.
//
// BlockDragState declares the seven members the delegates read; this is the
// implementation behind them. It drives four objects the window owns, which
// arrive as the properties below.
BlockDragState {
    id: controller

    // Wired by main.qml: the list being reordered, the shared edge scroller,
    // the layer that draws the proxy and the drop indicator, and the
    // block-selection key handler that takes focus back after a multi-block
    // drop.
    property var listView
    property var scroller
    property var dragLayer
    property var selectionKeys

    property int originalIndex: -1
    property var dragIndexes: []
    property int dragCount: 0
    property int indicatorGap: -1   // multi: gap BEFORE this index

    function begin(index, sceneX, sceneY) {
        controller.isMulti = DocumentSelection.hasBlockSelection
            && DocumentSelection.isBlockSelected(index)
        if (DocumentSelection.hasBlockSelection && !controller.isMulti)
            DocumentSelection.clear()
        if (DocumentSelection.hasTextSelection)
            DocumentSelection.clearTextSelection()
        controller.sourceIndex = index
        controller.originalIndex = index
        controller.dragIndexes = controller.isMulti
            ? DocumentSelection.selectedIndexes() : [index]
        controller.dragCount = controller.dragIndexes.length
        controller.indicatorGap = -1
        controller.active = true
        controller.dragLayer.buildFrom(controller.dragIndexes)
        controller.update(sceneX, sceneY)
    }

    function update(sceneX, sceneY) {
        if (!controller.active)
            return
        controller.dragLayer.moveTo(sceneX, sceneY)
        var pos = controller.listView.contentItem.mapFromItem(null, sceneX, sceneY)
        var cx = Math.max(1, Math.min(pos.x, controller.listView.width - 1))
        if (controller.isMulti) {
            controller.indicatorGap = controller.gapAt(cx, pos.y)
        } else {
            var idx = controller.listView.indexAt(cx, Math.max(0,
                Math.min(pos.y, controller.listView.contentHeight - 1)))
            if (idx >= 0 && idx !== controller.sourceIndex) {
                var item = (controller.listView.itemAtIndex(idx)
                            as BlockDelegateBase)
                // Move only once the pointer passes the target row's
                // midpoint, so unequal row heights cannot oscillate
                if (item) {
                    var centerY = item.y + item.height / 2
                    if ((idx > controller.sourceIndex && pos.y > centerY)
                        || (idx < controller.sourceIndex && pos.y < centerY)) {
                        BlockModel.previewMoveBlock(controller.sourceIndex, idx)
                        controller.sourceIndex = idx
                    }
                }
            }
        }
        controller.scroller.pointerY =
            controller.listView.mapFromItem(null, sceneX, sceneY).y
        controller.scroller.active = true
    }

    function gapAt(cx, cy) {
        if (cy <= 0)
            return 0
        if (cy >= controller.listView.contentHeight)
            return BlockModel.count
        var idx = controller.listView.indexAt(cx, cy)
        if (idx < 0) {
            idx = controller.listView.indexAt(cx,
                Math.max(0, cy - controller.listView.spacing))
            return idx < 0 ? -1 : idx + 1
        }
        var item = (controller.listView.itemAtIndex(idx) as BlockDelegateBase)
        if (!item)
            return idx
        return cy > item.y + item.height / 2 ? idx + 1 : idx
    }

    function drop() {
        if (!controller.active)
            return
        if (controller.isMulti) {
            if (controller.indicatorGap >= 0)
                BlockModel.moveBlocksTo(controller.dragIndexes,
                                        controller.indicatorGap)
            // The selection follows the moved blocks by id; keys
            // stay with the selection handler
            controller.selectionKeys.forceActiveFocus()
        } else {
            BlockModel.commitDragMove(controller.originalIndex,
                                      controller.sourceIndex)
            controller.listView.currentIndex = controller.sourceIndex
        }
        controller.end()
    }

    // Escape cancels (§5.4): the row returns to where the drag
    // started and nothing lands on the undo stack
    function cancel() {
        if (!controller.active)
            return
        if (!controller.isMulti
                && controller.sourceIndex !== controller.originalIndex)
            BlockModel.previewMoveBlock(controller.sourceIndex,
                                        controller.originalIndex)
        controller.end()
    }

    function end() {
        controller.active = false
        controller.sourceIndex = -1
        controller.originalIndex = -1
        controller.indicatorGap = -1
        controller.dragLayer.clear()
        controller.scroller.active = false
    }
}
