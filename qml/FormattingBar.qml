// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The BarButton component's background is a separate scope, and its
// checked state is computed from an id declared outside it. Binding the
// scope is what lets both resolve.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The floating formatting bar (features.md §9.3): appears over a
// completed in-block text selection — after a mouse selection ends or
// a Shift+arrow run pauses, both covered by the settle timer —
// positioned above the selection (below when clipped at the top), and
// dismissed by collapse, typing (which
// collapses), scrolling, or Escape (the block's deselect). Cross-block
// selections never show it: formatting is deliberately inert there.
// A plain high-z item, not a Popup: it must never touch focus.
Rectangle {
    id: bar
    objectName: "formattingBar"

    // Wired by main.qml: the caret's block and the block list.
    property var target
    property var listView

    z: 600
    width: buttonRow.implicitWidth + 8
    height: Interface.px(34)
    radius: Interface.px(6)
    color: Theme.popupBackground
    border.color: Theme.borderStrong
    border.width: 1

    readonly property bool selectionActive:
        target !== null && target !== undefined
        && target.selectedDisplayText !== undefined
        && target.selectionEndDoc > target.selectionStartDoc
        && !target.verbatimEditing
        && !DocumentSelection.hasTextSelection

    // One number that changes with any selection movement; every change
    // disarms and restarts the settle timer, so the bar only appears
    // once the selection has been stable for a beat.
    readonly property int selectionStamp: {
        if (!selectionActive)
            return -1
        return target.selectionStartDoc * 65536 + target.selectionEndDoc
    }
    property bool armed: false
    onSelectionStampChanged: {
        armed = false
        if (selectionStamp >= 0)
            settleTimer.restart()
        else
            settleTimer.stop()
    }

    Timer {
        id: settleTimer
        interval: 350
        onTriggered: {
            if (bar.selectionActive) {
                bar.reposition()
                bar.armed = true
            }
        }
    }

    // Scrolling moves the selection out from under the bar: dismiss.
    Connections {
        target: bar.listView
        function onContentYChanged() { bar.armed = false }
    }

    visible: armed && selectionActive

    // Above the selection, never under the pointer's press point;
    // below it when the top would clip (§9.3 "without obscuring").
    function reposition() {
        if (!target || !parent)
            return
        var rect = target.selectionRectangle()
        var topLeft = target.mapToItem(parent, rect.x, rect.y)
        var x = topLeft.x + rect.width / 2 - width / 2
        bar.x = Math.max(4, Math.min(x, parent.width - width - 4))
        var above = topLeft.y - height - 6
        bar.y = above >= 4 ? above : topLeft.y + rect.height + 6
    }

    component BarButton: ToolButton {
        // Named so the background, which is its own scope, can read the
        // button's state instead of reaching it through an untyped `parent`.
        id: barButton
        property int flagBit: 0
        // What the button is called. The visible text is a single styled
        // letter or a glyph, which a screen reader would otherwise read out
        // as that character (accessibility.md Finding 1).
        property string label: ""
        // This bar deliberately never takes focus — it hovers over a live
        // text selection, and focusing it would collapse the selection it
        // formats. So the label reaches a screen reader through the name
        // and a pointer user through the tooltip, and there is no keyboard
        // route through the bar itself; every command here also has a chord.
        focusPolicy: Qt.NoFocus
        implicitWidth: Interface.px(28)
        implicitHeight: Interface.px(26)
        font.pixelSize: Interface.body
        checked: flagBit !== 0
                 && bar.target && bar.target.cursorFormatFlags !== undefined
                 && (bar.target.cursorFormatFlags & flagBit) !== 0
        Accessible.role: barButton.flagBit !== 0 ? Accessible.CheckBox
                                                 : Accessible.Button
        Accessible.name: barButton.label !== "" ? barButton.label : barButton.text
        Accessible.checkable: barButton.flagBit !== 0
        Accessible.checked: barButton.checked
        ToolTip.text: barButton.label
        ToolTip.visible: barButton.hovered && barButton.label !== ""
        ToolTip.delay: 500
        background: Rectangle {
            radius: Interface.px(4)
            color: barButton.checked ? Theme.selectionTint
                 : barButton.hovered ? Theme.hoverTint : "transparent"
        }
    }

    Row {
        id: buttonRow
        anchors.centerIn: parent
        spacing: Interface.px(1)

        BarButton {
            objectName: "fbBoldButton"
            text: "B"; label: qsTr("Bold"); font.bold: true; flagBit: 0x2
            onClicked: bar.target.toggleSpanType("bold")
        }
        BarButton {
            objectName: "fbItalicButton"
            text: "I"; label: qsTr("Italic"); font.italic: true; flagBit: 0x4
            onClicked: bar.target.toggleSpanType("italic")
        }
        BarButton {
            objectName: "fbUnderlineButton"
            text: "U"; label: qsTr("Underline"); font.underline: true; flagBit: 0x10
            onClicked: bar.target.toggleSpanType("underline")
        }
        BarButton {
            objectName: "fbStrikeButton"
            text: "S"; label: qsTr("Strikethrough"); font.strikeout: true; flagBit: 0x8
            onClicked: bar.target.toggleSpanType("strike")
        }
        BarButton {
            objectName: "fbCodeButton"
            text: "<>"; label: qsTr("Inline code"); flagBit: 0x20; font.pixelSize: Interface.small
            implicitWidth: Interface.px(32)
            onClicked: bar.target.toggleSpanType("code")
        }
        BarButton {
            id: highlightButton
            objectName: "fbHighlightButton"
            text: "H"; label: qsTr("Highlight"); flagBit: 0x40
            // This one overrides the shared background to tint with the
            // highlight colour, so it repeats the pattern and needs its own
            // id for the same reason.
            background: Rectangle {
                radius: Interface.px(4)
                color: highlightButton.checked ? Theme.highlightBackground
                     : highlightButton.hovered ? Theme.hoverTint : "transparent"
            }
            onClicked: bar.target.toggleSpanType("highlight")
        }
        BarButton {
            objectName: "fbSuperscriptButton"
            text: "x²"; label: qsTr("Superscript"); flagBit: 0x100; font.pixelSize: Interface.small
            onClicked: bar.target.toggleSpanType("superscript")
        }
        BarButton {
            objectName: "fbSubscriptButton"
            text: "x₂"; label: qsTr("Subscript"); flagBit: 0x200; font.pixelSize: Interface.small
            onClicked: bar.target.toggleSpanType("subscript")
        }
        BarButton {
            objectName: "fbLinkButton"
            text: qsTr("Link"); label: qsTr("Insert link"); flagBit: 0x80; font.pixelSize: Interface.small
            implicitWidth: 36; font.underline: true
            onClicked: bar.target.openLinkDialog()
        }
        BarButton {
            objectName: "fbColorButton"
            text: "A"; label: qsTr("Text colour"); flagBit: 0x400
            onClicked: fbColorPicker.open()
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Interface.px(3)
                anchors.horizontalCenter: parent.horizontalCenter
                width: 14; height: 3; radius: 1
                color: (bar.target && bar.target.currentColor)
                    ? bar.target.currentColor : Theme.textPrimary
            }
            ColorPicker {
                id: fbColorPicker
                y: parent.height
                currentColor: (bar.target && bar.target.currentColor !== undefined)
                    ? bar.target.currentColor : ""
                onColorPicked: function(v) {
                    if (bar.target) bar.target.applyColor(v)
                }
                onRemoveRequested: {
                    if (bar.target) bar.target.removeColor()
                }
            }
        }
    }
}
