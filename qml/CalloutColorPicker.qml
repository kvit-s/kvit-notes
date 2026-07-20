// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

// Callout custom-color picker (features.md §1.2.10). A small
// popup of accent swatches plus a custom-color dialog and a "Reset to type
// color" action. It only reports the choice; the callout applies it as one undo
// step through setBlockAttributes. The chosen color overrides the callout's
// typed accent, from which the panel tint, border, and bar all derive.
Popup {
    id: root

    // The callout's current custom color ("" when it uses the typed default),
    // so the matching swatch shows a ring and "Reset" enables.
    property string currentColor: ""

    signal colorPicked(string value)
    signal resetRequested()

    // Accent-strength colors legible as a callout bar/border; the panel tint is
    // derived at 10% alpha in the delegate. A spread across the hue wheel.
    readonly property var swatches: [
        "#2f81f7", "#e3b341", "#3fb950", "#f85149",
        "#a371f7", "#db61a2", "#1f9e8b", "#8b949e"]

    padding: 8
    modal: false
    focus: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        color: theme.popupBackground
        border.color: theme.borderStrong
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
                        ? theme.accent : theme.border
                    HoverHandler { id: swHover }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        radius: 6
                        color: "transparent"
                        border.color: theme.accent
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
                objectName: "calloutColorCustom"
                text: qsTr("Custom…")
                focusPolicy: Qt.NoFocus
                font.pixelSize: 12
                onClicked: colorDialog.open()
            }
            Button {
                objectName: "calloutColorReset"
                text: qsTr("Reset")
                focusPolicy: Qt.NoFocus
                font.pixelSize: 12
                enabled: root.currentColor !== ""
                onClicked: { root.resetRequested(); root.close() }
            }
        }
    }

    ColorDialog {
        id: colorDialog
        onAccepted: {
            var s = selectedColor.toString()
            if (s.length === 9)   // "#aarrggbb" → "#rrggbb"
                s = "#" + s.substr(3)
            root.colorPicked(s)
            root.close()
        }
    }
}
