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

// The text-color control, shared by the toolbar, the
// formatting bar, and the text context menu. A small popup of theme-palette
// swatches plus a custom-color picker and a "Remove color" action. It only
// reports the choice; the caller applies it to the focused block as one undo
// step (EditableBlock.applyColor / removeColor).
//
// This is the model the other choice popups follow (accessibility.md
// Finding 2). Four things make a popup of this kind usable without a
// pointer:
//
//   1. it takes focus and closes on Escape;
//   2. it names itself, so opening it is announced rather than silent;
//   3. the choices are focusable items with names, so a reader can hear
//      "Red" instead of a hex value, and the arrow keys move between them;
//   4. focus goes back to whatever opened it when it closes.
//
// Taking focus is what it costs. The block this recolours drops its
// selection when it loses focus, so the popup raises the shell's
// selectionHolders count for as long as it is open — see KvitShell.
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

    // Popup is not an Item and has no Window attached property of its own;
    // the item it was declared in is the way to the window it belongs to.
    readonly property KvitShell shell: root.parent ? root.parent.Window.window as KvitShell : null
    // What had the keyboard before this opened, so closing can hand it back.
    property Item openedFrom: null

    padding: Interface.px(8)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // Qt leaves a popup outside its window unless it is given a margin, and
    // the bar this drops out of follows the caret down the page.
    margins: 6

    function choose(value) {
        root.colorPicked(value)
        root.close()
    }

    // aboutToShow, not opened: by the time the popup is open it already holds
    // the focus this is trying to record.
    onAboutToShow: {
        const w = root.parent ? root.parent.Window.window : null
        root.openedFrom = w ? w.activeFocusItem : null
        if (root.shell)
            root.shell.selectionHolders += 1
    }
    onOpened: swatchGrid.forceActiveFocus()
    onClosed: {
        if (root.shell)
            root.shell.selectionHolders -= 1
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
        // Accessible attaches to Items, not to the Popup, so the name that
        // announces the popup on open lives on its content.
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Text colour")

        // A GridView rather than a Grid of Repeater rectangles: it gives
        // arrow-key movement between the swatches for nothing, and a current
        // item to draw the keyboard ring on.
        GridView {
            id: swatchGrid
            objectName: "colorPickerSwatches"
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

                Accessible.role: Accessible.RadioButton
                Accessible.name: Theme.colorName(swatchCell.modelData)
                Accessible.checkable: true
                Accessible.checked: swatchCell.isCurrent
                Accessible.focused: swatchGrid.currentIndex === swatchCell.index
                Accessible.onPressAction: root.choose(swatchCell.modelData)

                Rectangle {
                    id: swatch
                    anchors.centerIn: parent
                    width: Interface.px(22)
                    height: Interface.px(22)
                    radius: Interface.px(4)
                    color: swatchCell.modelData
                    border.width: swatchCell.isCurrent ? 2 : 1
                    border.color: swatchCell.isCurrent ? Theme.accent : Theme.borderStrong

                    HoverHandler { id: swHover; cursorShape: Qt.PointingHandCursor }
                    Rectangle {   // hover and keyboard ring
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: Interface.px(6)
                        color: "transparent"
                        border.color: swatchGrid.activeFocus
                                      && swatchGrid.currentIndex === swatchCell.index
                                      ? Theme.focusRing : Theme.accent
                        border.width: (swatchGrid.activeFocus
                                       && swatchGrid.currentIndex === swatchCell.index)
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
                objectName: "colorPickerCustom"
                text: qsTr("Custom…")
                focusPolicy: Qt.TabFocus
                font.pixelSize: Interface.body
                onClicked: colorDialog.open()
            }
            Button {
                objectName: "colorPickerRemove"
                text: qsTr("Remove")
                focusPolicy: Qt.TabFocus
                font.pixelSize: Interface.body
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
            root.choose(s)
        }
    }
}
