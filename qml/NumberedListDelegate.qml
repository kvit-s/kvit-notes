// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// Numbered list item (features.md §1.2.5). The number is the model's
// computed ordinal — renumbering on insert/remove/move and nested restart
// come from the model role, never from stored text.
EditableBlock {
    id: root

    leadingChrome: Component {
        Item {
            implicitWidth: Math.max(20, ordinalText.implicitWidth + 4)

            Text {
                id: ordinalText
                objectName: "ordinalLabel"
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                font.pixelSize: 15
                color: theme.textSecondary
                text: root.ordinal + "."
            }
        }
    }
}
