// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The boxes are a Repeater delegate, which is its own component scope and
// reads the box list and the editor from this file's root by id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// The outlines around text a linked module has marked
// (DocumentDecorations::addSpan). A marked range has two visual channels: a
// background wash, which the editing engine paints as a character format in
// its highlight pass, and this border, which no character format can express
// and which is therefore drawn over the text as items.
//
// One box per visual line a span crosses, because a marked phrase that wraps
// is two runs of characters in two places. The engine works out where those
// runs are and how wide each is; the position they start at is a caret
// rectangle, which only the item that laid the text out can answer.
//
// The layer takes no input of its own: a border around a phrase must not
// intercept a click meant for the text under it.
Item {
    id: root

    // The editor whose text these outlines sit on, asked for caret
    // rectangles and for the offset of its text within this layer.
    property TextArea editor: null
    // The marked runs, as the engine reports them: { id, wash, outline,
    // docStart, width }. An entry whose `outline` is empty is a wash-only
    // span, which this layer carries but does not draw.
    property var boxes: []
    // Bumped by the delegate whenever the text relayouts, which moves every
    // box without changing the list.
    property int tick: 0

    objectName: "spanDecorationLayer"
    z: 4

    Repeater {
        model: root.boxes
        delegate: Rectangle {
            id: outlineBox
            required property var modelData
            objectName: "decorationSpanOutline"

            visible: outlineBox.modelData.outline !== ""
            // Where the run starts, in the editor's coordinates. Re-read on
            // relayout: the characters move, the box list does not change.
            readonly property rect caret: {
                var t = root.tick
                if (!root.editor)
                    return Qt.rect(0, 0, 0, 0)
                return root.editor.positionToRectangle(outlineBox.modelData.docStart)
            }
            x: (root.editor ? root.editor.x : 0) + outlineBox.caret.x
            y: (root.editor ? root.editor.y : 0) + outlineBox.caret.y
            width: outlineBox.modelData.width
            height: outlineBox.caret.height
            color: "transparent"
            border.color: outlineBox.modelData.outline
            border.width: 1
            radius: 2
        }
    }
}
