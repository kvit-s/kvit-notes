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
    // The character after it. Nothing draws it; it is here to measure where
    // the editor puts the second glyph, which the pair's kerning moves. Empty
    // when the paragraph is a single character.
    property string nextLetter: ""
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
    TextMetrics {
        id: pairMetrics
        font.family: root.bodyFontFamily
        font.pixelSize: root.bodyFontPixelSize
        text: root.letter + root.nextLetter
    }
    TextMetrics {
        id: nextMetrics
        font.family: root.bodyFontFamily
        font.pixelSize: root.bodyFontPixelSize
        text: root.nextLetter
    }

    // How far right of the text origin the editor starts the second glyph.
    // Taking the second character's own advance off the pair's width leaves the
    // first character's advance plus the kerning between the two, which is
    // where the mask has to stop: the first glyph's advance width alone
    // ignores the kern, and a font that tucks the next letter under the initial
    // then has its left edge painted over.
    readonly property real firstGlyphAdvance: root.nextLetter === ""
        ? bodyMetrics.advanceWidth
        : pairMetrics.advanceWidth - nextMetrics.advanceWidth

    Rectangle {
        objectName: "dropCapMask"
        visible: root.active
        // One pixel left of the glyph: antialiasing puts ink just outside the
        // advance width, which would otherwise survive as a sliver. There is no
        // matching slack on the right, where the next character begins.
        x: root.textOriginX - 1
        y: root.textOriginY
        width: Math.round(root.firstGlyphAdvance) + 1
        height: bodyMetrics.height
        color: root.maskColor
        z: 4

        // The hover tint the mask sits on fades over 100ms; matching it keeps
        // the covered glyph's patch from stepping ahead of its background.
        Behavior on color {
            ColorAnimation { duration: 100 }
        }
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
