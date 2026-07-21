// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// The enlarged initial a paragraph shows when its `dropcap` attribute is set
// (features.md §1.2.16). QQuickTextEdit cannot float text around a glyph, so
// the effect is assembled here rather than in the reveal engine: the editor
// keeps drawing the paragraph's first character at body size, an opaque
// rectangle covers exactly that character, and the large initial is painted
// beside it. The text hangs indented because the block reserves the initial's
// width in the editor's left inset.
//
// Everything the effect needs arrives as a value. The mask has to land on the
// glyph the editor drew, which is why the caller passes the editor's text
// origin and the body font rather than this item measuring either itself.
Item {
    id: root

    // The paragraph's first character, drawn twice: once at body size by the
    // editor underneath, once enlarged here.
    property string letter: ""
    // Whether to draw at all. The effect is a display form: the block turns it
    // off while it holds the caret so the paragraph reflows to normal text for
    // editing.
    property bool active: false

    // The font the editor draws the covered glyph in, which sizes the mask.
    property string bodyFontFamily: ""
    property int bodyFontPixelSize: 0
    // The page color behind the paragraph, so the mask reads as absence.
    property color maskColor: Theme.windowBackground
    // Top-left of the editor's first character, in this item's coordinates.
    property real textOriginX: 0
    property real textOriginY: 0

    property color letterColor: Theme.textPrimary
    property int letterPixelSize: 0
    property string letterFontFamily: ""

    TextMetrics {
        id: bodyMetrics
        font.family: root.bodyFontFamily
        font.pixelSize: root.bodyFontPixelSize
        text: root.letter
    }

    Rectangle {
        objectName: "dropCapMask"
        visible: root.active
        // One pixel left of the glyph: antialiasing puts ink just outside the
        // advance width, which would otherwise survive as a sliver.
        x: root.textOriginX - 1
        y: root.textOriginY
        width: bodyMetrics.advanceWidth + 3
        height: bodyMetrics.height
        color: root.maskColor
        z: 4
    }

    Text {
        objectName: "dropCapLetter"
        visible: root.active
        text: root.letter
        color: root.letterColor
        font.bold: true
        font.pixelSize: root.letterPixelSize
        font.family: root.letterFontFamily
        x: 2
        y: root.textOriginY
        z: 5
    }
}
