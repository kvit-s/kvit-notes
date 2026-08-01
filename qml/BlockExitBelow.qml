// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Ctrl+Enter out of a block that folds when it stops being edited: the four
// blocks whose own Enter belongs to their content — a Mermaid diagram, a
// display equation, a collection query and a table.
//
// Such a block is taller while it is being edited than at rest: a diagram
// shows its source above its preview, an equation its TeX, a query its spec, a
// table a live cell grown to what is being typed. The block list must process
// that resting height before the insertion is applied, or the new row can be
// positioned against the editor's old height.
//
// This helper begins from completion state: begin() waits until the block
// reports that editing has ended. ListView keeps its row-position cache until
// the render loop consumes that height change, so the handoff then uses
// FrameAnimation for the one place an actual rendered-frame boundary is
// required. Unlike a 16 ms Timer, that callback follows the window's real
// animation/render cadence. The new row is handed to the shell's shared focus
// lifecycle, which waits for a virtualized delegate instead of trying once.
Item {
    id: exitBelow

    // Wired by the block: where it sits, and the list it sits in.
    property int blockIndex: -1
    property ListView listView
    property BlockDelegateBase blockItem
    property bool editing: false
    readonly property KvitShell shell:
        blockItem ? blockItem.geometryShell : null
    property bool pending: false

    width: 0
    height: 0
    visible: false

    // Called by the block once it has folded its editor away.
    function begin() {
        if (exitBelow.pending)
            return
        exitBelow.pending = true
        exitBelow.scheduleCompletion()
    }
    function scheduleCompletion() {
        if (!exitBelow.pending || exitBelow.editing
                || completionFrame.running)
            return
        completionFrame.start()
    }
    function completeInsertion() {
        if (!exitBelow.pending || exitBelow.editing)
            return

        // Height bindings have now observed editing=false. Process that
        // geometry before changing the model so the next row is positioned
        // against the block's resting height.
        if (exitBelow.blockItem)
            exitBelow.blockItem.notifyShellGeometryChanged()
        if (exitBelow.listView)
            exitBelow.listView.forceLayout()

        exitBelow.pending = false
        var newIndex = exitBelow.blockIndex + 1
        BlockModel.insertBlock(newIndex, 0, "")
        if (exitBelow.shell)
            exitBelow.shell.focusBlockAtIndex(newIndex)
        else if (exitBelow.listView)
            exitBelow.listView.currentIndex = newIndex
    }

    onEditingChanged: {
        if (exitBelow.editing) {
            completionFrame.stop()
            completionFrame.restingFrameSeen = false
        } else {
            exitBelow.scheduleCompletion()
        }
    }
    Connections {
        target: exitBelow.blockItem
        function onHeightChanged() {
            // Require a complete frame after the most recent resting-height
            // change, even when another change lands while already waiting.
            completionFrame.restingFrameSeen = false
            exitBelow.scheduleCompletion()
        }
    }
    FrameAnimation {
        id: completionFrame
        property bool restingFrameSeen: false
        running: false
        onTriggered: {
            if (!exitBelow.pending || exitBelow.editing) {
                completionFrame.stop()
                completionFrame.restingFrameSeen = false
                return
            }
            // FrameAnimation triggers while preparing a frame. The first tick
            // lets that frame consume the folded height; the next tick is the
            // boundary after it has actually been rendered.
            if (!completionFrame.restingFrameSeen) {
                completionFrame.restingFrameSeen = true
                if (exitBelow.listView)
                    exitBelow.listView.forceLayout()
                return
            }
            completionFrame.stop()
            completionFrame.restingFrameSeen = false
            exitBelow.completeInsertion()
        }
    }
}
