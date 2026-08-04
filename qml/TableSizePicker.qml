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
import QtQuick.Controls
import Kvit 1.0

// Grid-size picker for inserting a table: a word-processor-style hover grid
// up to 8×8, the arrow keys moving the selection the same way the pointer
// does, Enter accepting it (3×3 until something moves it). Emits
// sizePicked(columns, rows).
Popup {
    id: root
    signal sizePicked(int columns, int rows)

    property int maxCols: 8
    property int maxRows: 8
    // The size the picker starts on, and what Enter inserts if nothing moves
    // the selection. Each opening starts here rather than wherever the last
    // one was left, so the keyboard route always begins from a known cell.
    property int defaultCols: 3
    property int defaultRows: 3
    property int hoverCols: defaultCols
    property int hoverRows: defaultRows

    padding: Interface.px(8)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {
        hoverCols = defaultCols
        hoverRows = defaultRows
    }
    onOpened: contentRoot.forceActiveFocus()

    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: Interface.px(6)
    }

    contentItem: Column {
        id: contentRoot
        focus: true
        spacing: Interface.px(6)

        // The grid is a picture: 64 identical squares, some tinted. Its name
        // says what the popup is, and the size under the caret is spoken as
        // it changes — arrow keys move a tint nobody using a screen reader
        // can see (accessibility.md Finding 2).
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Table size")
        Accessible.description: qsTr("%1 columns by %2 rows")
                                .arg(root.hoverCols).arg(root.hoverRows)

        Connections {
            target: root
            function onHoverColsChanged() { contentRoot.announceSize() }
            function onHoverRowsChanged() { contentRoot.announceSize() }
        }
        function announceSize() {
            if (root.opened)
                A11y.announce(qsTr("%1 columns by %2 rows")
                              .arg(root.hoverCols).arg(root.hoverRows))
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.sizePicked(root.hoverCols, root.hoverRows)
                root.close()
                event.accepted = true
            } else if (event.key === Qt.Key_Left) {
                root.hoverCols = Math.max(1, root.hoverCols - 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                root.hoverCols = Math.min(root.maxCols, root.hoverCols + 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                root.hoverRows = Math.max(1, root.hoverRows - 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                root.hoverRows = Math.min(root.maxRows, root.hoverRows + 1)
                event.accepted = true
            }
        }
        Grid {
            id: pickerGrid
            columns: root.maxCols
            spacing: Interface.px(3)
            Repeater {
                model: root.maxCols * root.maxRows
                delegate: Rectangle {
                    required property int index
                    readonly property int c: index % root.maxCols
                    readonly property int r: Math.floor(index / root.maxCols)
                    width: 18; height: 18; radius: 2
                    color: (c < root.hoverCols && r < root.hoverRows)
                        ? Theme.accent : Theme.chipBackground
                    border.width: 1
                    border.color: Theme.borderStrong
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
            color: Theme.textMuted
            font.pixelSize: Interface.body
        }
    }
}
