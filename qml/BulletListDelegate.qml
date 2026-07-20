// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Bulleted list item (features.md §1.2.4). The bullet glyph cycles with
// the nesting depth: disc, circle, square.
EditableBlock {
    id: root

    leadingChrome: Component {
        Item {
            implicitWidth: 16

            Text {
                objectName: "bulletGlyph"
                anchors.horizontalCenter: parent.horizontalCenter
                y: 2
                font.pixelSize: 15
                color: Theme.textSecondary
                text: {
                    switch (root.indentLevel % 3) {
                        case 1: return "◦"   // ◦ circle
                        case 2: return "▪"   // ▪ square
                        default: return "•"  // • disc
                    }
                }
            }
        }
    }
}
