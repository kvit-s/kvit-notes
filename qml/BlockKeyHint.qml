// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// The line a source-editing block prints in its bottom-right corner to name
// the key that leaves it.
//
// Four blocks edit a fenced source rather than prose — code, display math,
// Mermaid diagrams, and the collection query — and in every one of them Enter
// is a line break, because blank lines and multi-line bodies are ordinary
// content there. That leaves Ctrl+Enter as the only way to a new block, and a
// key that has no visible affordance is a key nobody finds. The hint says it
// while the caret is in the block.
//
// It is one component so the wording, the colour and the corner match across
// all four; each block anchors it into whatever footer strip its own layout
// has, and passes its own text size so the hint stays proportional.
Text {
    id: hint

    // The block's content text size. The hint sits a few pixels below it,
    // never smaller than 9px.
    property int basePixelSize: 13

    text: qsTr("Ctrl+Enter: new block")
    color: Theme.textFaint
    font.pixelSize: Math.max(9, hint.basePixelSize - 4)
    horizontalAlignment: Text.AlignRight
}
