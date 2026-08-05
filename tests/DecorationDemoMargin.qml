// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// A glyph in the reserved margin column, beside one visual line of one block.
// The module owns what this looks like and what it does; the editor owns the
// column it sits in and the position it is handed.
Text {
    id: glyph
    objectName: "demoMarginGlyph"

    property QtObject decorationContext: null
    property int decorationBlock: -1

    text: "●"
    color: "#cc4400"
    verticalAlignment: Text.AlignVCenter
}
