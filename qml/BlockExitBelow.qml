// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Ctrl+Enter out of a block that folds when it stops being edited: the four
// blocks whose own Enter belongs to their content — a Mermaid diagram, a
// display equation, a collection query and a table.
//
// The fold and the insert are one frame apart, and that is the whole point of
// this file. Such a block is taller while it is being edited than at rest: a
// diagram shows its source above its preview, an equation its TeX, a query
// its spec, a table a live cell grown to what is being typed. The block list
// positions an inserted row against the height its neighbour has at the
// moment of the insert. Folding the editor away in the same frame as the
// insert therefore left the new block a whole editor's height below its
// neighbour, with a band of nothing in between.
//
// So the block folds first, the list lays that out, and the row is inserted a
// frame later against the height the list can see. The delay is one frame, so
// what the reader sees is the block closing and the caret arriving below it.
Timer {
    id: exitBelow

    // Wired by the block: where it sits, and the list it sits in.
    property int blockIndex: -1
    property var listView

    interval: 16
    repeat: false

    // Called by the block once it has folded its editor away.
    function begin() { exitBelow.restart() }

    onTriggered: {
        var newIndex = exitBelow.blockIndex + 1
        BlockModel.insertBlock(newIndex, 0, "")
        Qt.callLater(function() {
            if (!exitBelow.listView)
                return
            exitBelow.listView.currentIndex = newIndex
            var item = (exitBelow.listView.itemAtIndex(newIndex)
                        as BlockDelegateBase)
            if (item)
                item.focusAtStart()
        })
    }
}
