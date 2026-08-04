// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The style swatches are a Repeater delegate whose Text and TapHandler
// are separate scopes, so the swatch is named and addressed by id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Window
import Kvit 1.0

// Divider style picker (features.md §1.2.9): choose the rule's
// style, thickness, width, and color. It reports a canonical attribute payload
// through `applied`; the divider writes it via setBlockAttributes (one undo
// step per change). Defaults (solid / 2px / full / typed color) are omitted so
// a fully-default divider carries no tag and stays a bare `---`.
//
// Unlike the colour and type pickers this one stays open while it is used —
// the four groups are read against each other — so its choices are tab stops
// rather than one arrow-key list. Each is a real choice with a name and a
// selected state; the shared ChoiceSwatch below is what carries that
// (accessibility.md Finding 2).
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
    // In the same order as colorSwatches. The rule accents are this
    // picker's own rather than the content palette, so they are named here.
    readonly property var colorNames: [
        qsTr("Default colour"), qsTr("Grey"), qsTr("Blue"), qsTr("Green"),
        qsTr("Yellow"), qsTr("Red"), qsTr("Purple")]

    // What had the keyboard before this opened, so closing can hand it back.
    property Item openedFrom: null

    padding: Interface.px(10)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // Qt leaves a popup outside its window unless it is given a margin, and
    // a divider can sit at the bottom of the page.
    margins: 6

    onAboutToShow: {
        const w = root.parent ? root.parent.Window.window : null
        root.openedFrom = w ? w.activeFocusItem : null
    }
    onClosed: {
        if (root.openedFrom)
            root.openedFrom.forceActiveFocus()
        root.openedFrom = null
    }

    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: Interface.px(6)
    }

    // One of a group of mutually exclusive choices: selected or not, named,
    // reachable by Tab and taken with Space or Return.
    component ChoiceSwatch: Rectangle {
        id: swatchRoot
        // What a screen reader calls it, and which group it belongs to.
        property string name: ""
        property string groupName: ""
        property bool selected: false
        signal chosen()

        radius: Interface.px(4)
        color: swatchRoot.selected ? Theme.selectionTint
             : (swatchHover.hovered ? Theme.hoverTint : "transparent")
        border.width: swatchRoot.activeFocus ? 2 : 1
        border.color: swatchRoot.activeFocus ? Theme.focusRing
                    : (swatchRoot.selected ? Theme.accent : Theme.borderStrong)

        activeFocusOnTab: true
        Accessible.role: Accessible.RadioButton
        Accessible.name: swatchRoot.name
        Accessible.description: swatchRoot.groupName
        Accessible.checkable: true
        Accessible.checked: swatchRoot.selected
        Accessible.onPressAction: swatchRoot.chosen()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
                swatchRoot.chosen()
                event.accepted = true
            }
        }
        HoverHandler { id: swatchHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: swatchRoot.chosen() }
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
        spacing: Interface.px(10)
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Divider style")

        // ---- Style row ----
        Column {
            spacing: Interface.px(4)
            Text { text: qsTr("Style"); color: Theme.textMuted; font.pixelSize: Interface.small }
            Row {
                spacing: Interface.px(4)
                Repeater {
                    model: root.styles
                    delegate: ChoiceSwatch {
                        id: styleSwatch
                        required property string modelData
                        width: 62; height: 26
                        name: styleSwatch.modelData
                        groupName: qsTr("Style")
                        selected: root.currentStyle === styleSwatch.modelData
                        onChosen: {
                            root.currentStyle = styleSwatch.modelData
                            root.emitPayload()
                        }
                        Text {
                            anchors.centerIn: parent
                            text: styleSwatch.modelData
                            font.pixelSize: Interface.caption
                            color: Theme.textPrimary
                        }
                    }
                }
            }
        }

        // ---- Thickness ----
        Row {
            spacing: Interface.px(8)
            Text {
                text: qsTr("Thickness")
                color: Theme.textMuted; font.pixelSize: Interface.small
                anchors.verticalCenter: parent.verticalCenter
            }
            Slider {
                id: thicknessSlider
                objectName: "dividerThickness"
                from: 1; to: 8; stepSize: 1
                value: root.currentThickness
                width: Interface.px(110)
                anchors.verticalCenter: parent.verticalCenter
                // A Slider reports its own role and value; what it has no way
                // to know is what the number means.
                Accessible.name: qsTr("Thickness in pixels")
                onMoved: { root.currentThickness = Math.round(value); root.emitPayload() }
            }
            Text {
                text: root.currentThickness + "px"
                color: Theme.textPrimary; font.pixelSize: Interface.small
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // ---- Width ----
        Column {
            spacing: Interface.px(4)
            Text { text: qsTr("Width"); color: Theme.textMuted; font.pixelSize: Interface.small }
            Row {
                spacing: Interface.px(4)
                Repeater {
                    model: root.widths
                    delegate: ChoiceSwatch {
                        id: widthSwatch
                        required property string modelData
                        width: 48; height: 24
                        name: widthSwatch.modelData === "full" ? qsTr("Full")
                                                               : widthSwatch.modelData
                        groupName: qsTr("Width")
                        selected: root.currentWidth === widthSwatch.modelData
                        onChosen: {
                            root.currentWidth = widthSwatch.modelData
                            root.emitPayload()
                        }
                        Text {
                            anchors.centerIn: parent
                            text: widthSwatch.modelData === "full" ? qsTr("Full")
                                                                   : widthSwatch.modelData
                            font.pixelSize: Interface.caption
                            color: Theme.textPrimary
                        }
                    }
                }
            }
        }

        // ---- Color ----
        Column {
            spacing: Interface.px(4)
            Text { text: qsTr("Color"); color: Theme.textMuted; font.pixelSize: Interface.small }
            Row {
                spacing: Interface.px(6)
                Repeater {
                    model: root.colorSwatches
                    delegate: ChoiceSwatch {
                        id: colorSwatch
                        required property string modelData
                        required property int index
                        width: 20; height: 20
                        name: root.colorNames[colorSwatch.index]
                        groupName: qsTr("Colour")
                        selected: root.currentColor === colorSwatch.modelData
                        // The "" swatch is the default rule color; the rest
                        // paint themselves rather than taking the shared
                        // selection tint.
                        color: colorSwatch.modelData === "" ? "transparent"
                                                            : colorSwatch.modelData
                        onChosen: {
                            root.currentColor = colorSwatch.modelData
                            root.emitPayload()
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: colorSwatch.modelData === ""
                            text: "∅"; font.pixelSize: Interface.body; color: Theme.textMuted
                        }
                    }
                }
                Button {
                    text: qsTr("Custom…")
                    focusPolicy: Qt.TabFocus
                    font.pixelSize: Interface.small
                    height: Interface.px(22)
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
