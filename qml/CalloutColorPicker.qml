// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The GridView delegate and the handlers inside it are separate
// component scopes, so their reads of `root` and of the delegate's own
// role are unqualified access. The delegate already declares that role
// as a required property, so binding the scopes changes nothing it
// depended on.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Window
import Kvit 1.0

// Callout custom-color picker (features.md §1.2.10). A small
// popup of accent swatches plus a custom-color dialog and a "Reset to type
// color" action. It only reports the choice; the callout applies it as one undo
// step through setBlockAttributes. The chosen color overrides the callout's
// typed accent, from which the panel tint, border, and bar all derive.
//
// Keyboard and screen-reader behaviour follows ColorPicker.qml, which carries
// the explanation of why a choice popup needs focus, a name, named choices
// and a focus hand-back (accessibility.md Finding 2).
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
    // What each of those is called, in the same order. These are the
    // callout's own accents rather than the content palette, so the names
    // live here beside the values instead of in Theme.colorName.
    readonly property var swatchNames: [
        qsTr("Blue"), qsTr("Yellow"), qsTr("Green"), qsTr("Red"),
        qsTr("Purple"), qsTr("Pink"), qsTr("Teal"), qsTr("Grey")]

    // What had the keyboard before this opened, so closing can hand it back.
    property Item openedFrom: null

    padding: Interface.px(8)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // Qt leaves a popup outside its window unless it is given a margin, and
    // a callout can sit at the bottom of the page (see CalloutTypePicker).
    margins: 6

    function choose(value) {
        root.colorPicked(value)
        root.close()
    }

    onAboutToShow: {
        const w = root.parent ? root.parent.Window.window : null
        root.openedFrom = w ? w.activeFocusItem : null
    }
    onOpened: swatchGrid.forceActiveFocus()
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

    contentItem: Column {
        spacing: Interface.px(8)
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Callout colour")

        GridView {
            id: swatchGrid
            objectName: "calloutColorSwatches"
            cellWidth: Interface.px(28)
            cellHeight: Interface.px(28)
            width: cellWidth * 4
            height: cellHeight * Math.ceil(root.swatches.length / 4)
            interactive: false
            keyNavigationEnabled: true
            focus: true
            model: root.swatches
            currentIndex: Math.max(0, root.swatches.indexOf(root.currentColor))
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Colours")

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    || event.key === Qt.Key_Space) {
                    root.choose(root.swatches[swatchGrid.currentIndex])
                    event.accepted = true
                }
            }

            delegate: Item {
                id: swatchCell
                required property var modelData
                required property int index
                width: swatchGrid.cellWidth
                height: swatchGrid.cellHeight

                readonly property bool isCurrent:
                    root.currentColor === swatchCell.modelData
                readonly property bool isFocused:
                    swatchGrid.currentIndex === swatchCell.index

                Accessible.role: Accessible.RadioButton
                Accessible.name: root.swatchNames[swatchCell.index]
                Accessible.checkable: true
                Accessible.checked: swatchCell.isCurrent
                Accessible.focused: swatchCell.isFocused
                Accessible.onPressAction: root.choose(swatchCell.modelData)

                Rectangle {
                    anchors.centerIn: parent
                    width: Interface.px(22)
                    height: Interface.px(22)
                    radius: Interface.px(4)
                    color: swatchCell.modelData
                    border.width: swatchCell.isCurrent ? 2 : 1
                    border.color: swatchCell.isCurrent ? Theme.accent : Theme.borderStrong

                    HoverHandler { id: swHover; cursorShape: Qt.PointingHandCursor }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: Interface.px(6)
                        color: "transparent"
                        border.color: swatchGrid.activeFocus && swatchCell.isFocused
                                      ? Theme.focusRing : Theme.accent
                        border.width: (swatchGrid.activeFocus && swatchCell.isFocused)
                                      ? 2 : (swHover.hovered ? 1 : 0)
                    }
                    TapHandler {
                        onTapped: {
                            swatchGrid.currentIndex = swatchCell.index
                            root.choose(swatchCell.modelData)
                        }
                    }
                }
            }
        }

        Row {
            spacing: Interface.px(6)
            width: parent.width
            Button {
                objectName: "calloutColorCustom"
                text: qsTr("Custom…")
                focusPolicy: Qt.TabFocus
                font.pixelSize: Interface.body
                onClicked: colorDialog.open()
            }
            Button {
                objectName: "calloutColorReset"
                text: qsTr("Reset")
                focusPolicy: Qt.TabFocus
                font.pixelSize: Interface.body
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
            root.choose(s)
        }
    }
}
