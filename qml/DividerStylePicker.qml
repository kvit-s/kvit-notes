// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Kvit 1.0

// Divider style picker (features.md §1.2.9): choose the rule's
// style, thickness, width, and color. It reports a canonical attribute payload
// through `applied`; the divider writes it via setBlockAttributes (one undo
// step per change). Defaults (solid / 2px / full / typed color) are omitted so
// a fully-default divider carries no tag and stays a bare `---`.
Popup {
    id: root

    property string currentStyle: "solid"
    property int currentThickness: 2
    property string currentColor: ""     // "" = the default rule color
    property string currentWidth: "full"

    signal applied(string payload)

    readonly property var styles: ["solid", "dashed", "dotted", "decorative"]
    readonly property var widths: ["full", "75%", "50%", "25%"]
    readonly property var colorSwatches: [
        "", "#8b949e", "#2f81f7", "#3fb950", "#e3b341", "#f85149", "#a371f7"]

    padding: 10
    modal: false
    focus: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: 6
    }

    // Assemble the canonical payload from the four controls, omitting defaults.
    function emitPayload() {
        var p = ""
        if (root.currentStyle !== "solid")
            p = BlockAttributes.withValue(p, "style", root.currentStyle)
        if (root.currentThickness !== 2)
            p = BlockAttributes.withValue(p, "thickness", String(root.currentThickness))
        if (root.currentWidth !== "full")
            p = BlockAttributes.withValue(p, "width", root.currentWidth)
        if (root.currentColor !== "")
            p = BlockAttributes.withValue(p, "color", root.currentColor)
        root.applied(p)
    }

    contentItem: Column {
        spacing: 10

        // ---- Style row ----
        Column {
            spacing: 4
            Text { text: qsTr("Style"); color: Theme.textMuted; font.pixelSize: 11 }
            Row {
                spacing: 4
                Repeater {
                    model: root.styles
                    delegate: Rectangle {
                        required property string modelData
                        width: 62; height: 26; radius: 4
                        color: root.currentStyle === modelData
                            ? Theme.selectionTint
                            : (styleHover.hovered ? Theme.hoverTint : "transparent")
                        border.width: 1
                        border.color: root.currentStyle === modelData
                            ? Theme.accent : Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: 10
                            color: Theme.textPrimary
                        }
                        HoverHandler { id: styleHover }
                        TapHandler {
                            onTapped: { root.currentStyle = modelData; root.emitPayload() }
                        }
                    }
                }
            }
        }

        // ---- Thickness ----
        Row {
            spacing: 8
            Text {
                text: qsTr("Thickness")
                color: Theme.textMuted; font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
            Slider {
                id: thicknessSlider
                objectName: "dividerThickness"
                from: 1; to: 8; stepSize: 1
                value: root.currentThickness
                width: 110
                anchors.verticalCenter: parent.verticalCenter
                onMoved: { root.currentThickness = Math.round(value); root.emitPayload() }
            }
            Text {
                text: root.currentThickness + "px"
                color: Theme.textPrimary; font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // ---- Width ----
        Column {
            spacing: 4
            Text { text: qsTr("Width"); color: Theme.textMuted; font.pixelSize: 11 }
            Row {
                spacing: 4
                Repeater {
                    model: root.widths
                    delegate: Rectangle {
                        required property string modelData
                        width: 48; height: 24; radius: 4
                        color: root.currentWidth === modelData
                            ? Theme.selectionTint
                            : (widthHover.hovered ? Theme.hoverTint : "transparent")
                        border.width: 1
                        border.color: root.currentWidth === modelData
                            ? Theme.accent : Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: modelData === "full" ? qsTr("Full") : modelData
                            font.pixelSize: 10
                            color: Theme.textPrimary
                        }
                        HoverHandler { id: widthHover }
                        TapHandler {
                            onTapped: { root.currentWidth = modelData; root.emitPayload() }
                        }
                    }
                }
            }
        }

        // ---- Color ----
        Column {
            spacing: 4
            Text { text: qsTr("Color"); color: Theme.textMuted; font.pixelSize: 11 }
            Row {
                spacing: 6
                Repeater {
                    model: root.colorSwatches
                    delegate: Rectangle {
                        required property string modelData
                        width: 20; height: 20; radius: 4
                        // The "" swatch is the default rule color.
                        color: modelData === "" ? "transparent" : modelData
                        border.width: root.currentColor === modelData ? 2 : 1
                        border.color: root.currentColor === modelData
                            ? Theme.accent : Theme.border
                        Text {
                            anchors.centerIn: parent
                            visible: modelData === ""
                            text: "∅"; font.pixelSize: 12; color: Theme.textMuted
                        }
                        TapHandler {
                            onTapped: { root.currentColor = modelData; root.emitPayload() }
                        }
                    }
                }
                Button {
                    text: qsTr("Custom…")
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 11
                    height: 22
                    onClicked: dividerColorDialog.open()
                }
            }
        }
    }

    ColorDialog {
        id: dividerColorDialog
        onAccepted: {
            var s = selectedColor.toString()
            if (s.length === 9)
                s = "#" + s.substr(3)
            root.currentColor = s
            root.emitPayload()
        }
    }
}
