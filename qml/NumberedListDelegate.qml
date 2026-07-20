// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Delegates in this file read ids from the enclosing component scope,
// which qmllint reports as unqualified access. Binding those ids into
// the nested scopes resolves it; the delegates here already declare a
// required property for every model role they read, so nothing relied on
// the injection this turns off.
pragma ComponentBehavior: Bound

import QtQuick
import Kvit 1.0

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
                color: Theme.textSecondary
                text: root.ordinal + "."
            }
        }
    }
}
