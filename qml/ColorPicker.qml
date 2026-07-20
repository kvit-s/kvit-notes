// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Kvit 1.0

// The text-color control, shared by the toolbar, the
// formatting bar, and the text context menu. A small popup of theme-palette
// swatches plus a custom-color picker and a "Remove color" action. It only
// reports the choice; the caller applies it to the focused block as one undo
// step (EditableBlock.applyColor / removeColor).
Popup {
    id: root

    // The color currently under the caret ("" when none), so the matching
    // swatch shows a ring and "Remove color" enables.
    property string currentColor: ""

    signal colorPicked(string value)
    signal removeRequested()

    // Saturated, text-legible swatches: the theme's content palette (folder/
    // tag colors) plus a dark and a mid-gray for prose.
    readonly property var swatches: Theme.colorPalette.concat(
        ["#333333", "#888888"])

    padding: 8
    modal: false
    focus: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: 6
    }

    contentItem: Column {
        spacing: 8

        Grid {
            columns: 4
            spacing: 6
            Repeater {
                model: root.swatches
                delegate: Rectangle {
                    required property var modelData
                    width: 22
                    height: 22
                    radius: 4
                    color: modelData
                    border.width: root.currentColor === modelData ? 2 : 1
                    border.color: root.currentColor === modelData
                        ? Theme.accent : Theme.border
                    HoverHandler { id: swHover }
                    Rectangle {   // hover ring
                        anchors.fill: parent
                        anchors.margins: -2
                        radius: 6
                        color: "transparent"
                        border.color: Theme.accent
                        border.width: swHover.hovered ? 1 : 0
                    }
                    TapHandler {
                        onTapped: { root.colorPicked(modelData); root.close() }
                    }
                }
            }
        }

        Row {
            spacing: 6
            width: parent.width
            Button {
                objectName: "colorPickerCustom"
                text: qsTr("Custom…")
                focusPolicy: Qt.NoFocus
                font.pixelSize: 12
                onClicked: colorDialog.open()
            }
            Button {
                objectName: "colorPickerRemove"
                text: qsTr("Remove")
                focusPolicy: Qt.NoFocus
                font.pixelSize: 12
                enabled: root.currentColor !== ""
                onClicked: { root.removeRequested(); root.close() }
            }
        }
    }

    ColorDialog {
        id: colorDialog
        onAccepted: {
            var s = selectedColor.toString()
            // Normalize "#aarrggbb" → "#rrggbb" (opaque), matching the grammar.
            if (s.length === 9)
                s = "#" + s.substr(3)
            root.colorPicked(s)
            root.close()
        }
    }
}
