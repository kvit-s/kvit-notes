// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls

// Grid-size picker for inserting a table: a word-processor-style hover grid
// up to 8×8, Enter accepting the default 3×3. Emits sizePicked(columns, rows).
Popup {
    id: root
    signal sizePicked(int columns, int rows)

    property int maxCols: 8
    property int maxRows: 8
    property int hoverCols: 3
    property int hoverRows: 3

    padding: 8
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: contentRoot.forceActiveFocus()

    background: Rectangle {
        color: theme.popupBackground
        border.color: theme.borderStrong
        border.width: 1
        radius: 6
    }

    contentItem: Column {
        id: contentRoot
        focus: true
        spacing: 6
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.sizePicked(root.hoverCols, root.hoverRows)
                root.close()
                event.accepted = true
            }
        }
        Grid {
            id: pickerGrid
            columns: root.maxCols
            spacing: 3
            Repeater {
                model: root.maxCols * root.maxRows
                delegate: Rectangle {
                    required property int index
                    readonly property int c: index % root.maxCols
                    readonly property int r: Math.floor(index / root.maxCols)
                    width: 18; height: 18; radius: 2
                    color: (c < root.hoverCols && r < root.hoverRows)
                        ? theme.accent : theme.chipBackground
                    border.width: 1
                    border.color: theme.border
                    HoverHandler {
                        onHoveredChanged: if (hovered) {
                            root.hoverCols = parent.c + 1
                            root.hoverRows = parent.r + 1
                        }
                    }
                    TapHandler {
                        onTapped: {
                            root.sizePicked(parent.c + 1, parent.r + 1)
                            root.close()
                        }
                    }
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.hoverCols + " × " + root.hoverRows
            color: theme.textMuted
            font.pixelSize: 12
        }
    }
}
