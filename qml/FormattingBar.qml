// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls

// The floating formatting bar (features.md §9.3, phase9-plan.md
// decision 7): appears over a completed in-block text selection —
// after a mouse selection ends or a Shift+arrow run pauses, both
// covered by the settle timer — positioned above the selection (below
// when clipped at the top), and dismissed by collapse, typing (which
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
    height: 34
    radius: 6
    color: theme.popupBackground
    border.color: theme.borderStrong
    border.width: 1

    readonly property bool selectionActive:
        target !== null && target !== undefined
        && target.selectedDisplayText !== undefined
        && target.selectionEndDoc > target.selectionStartDoc
        && !target.verbatimEditing
        && !documentSelection.hasTextSelection

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
        property int flagBit: 0
        focusPolicy: Qt.NoFocus
        implicitWidth: 28
        implicitHeight: 26
        font.pixelSize: 12
        checked: flagBit !== 0
                 && bar.target && bar.target.cursorFormatFlags !== undefined
                 && (bar.target.cursorFormatFlags & flagBit) !== 0
        background: Rectangle {
            radius: 4
            color: parent.checked ? theme.selectionTint
                 : parent.hovered ? theme.hoverTint : "transparent"
        }
    }

    Row {
        id: buttonRow
        anchors.centerIn: parent
        spacing: 1

        BarButton {
            objectName: "fbBoldButton"
            text: "B"; font.bold: true; flagBit: 0x2
            onClicked: bar.target.toggleSpanType("bold")
        }
        BarButton {
            objectName: "fbItalicButton"
            text: "I"; font.italic: true; flagBit: 0x4
            onClicked: bar.target.toggleSpanType("italic")
        }
        BarButton {
            objectName: "fbUnderlineButton"
            text: "U"; font.underline: true; flagBit: 0x10
            onClicked: bar.target.toggleSpanType("underline")
        }
        BarButton {
            objectName: "fbStrikeButton"
            text: "S"; font.strikeout: true; flagBit: 0x8
            onClicked: bar.target.toggleSpanType("strike")
        }
        BarButton {
            objectName: "fbCodeButton"
            text: "<>"; flagBit: 0x20; font.pixelSize: 11
            implicitWidth: 32
            onClicked: bar.target.toggleSpanType("code")
        }
        BarButton {
            objectName: "fbHighlightButton"
            text: "H"; flagBit: 0x40
            background: Rectangle {
                radius: 4
                color: parent.checked ? theme.highlightBackground
                     : parent.hovered ? theme.hoverTint : "transparent"
            }
            onClicked: bar.target.toggleSpanType("highlight")
        }
        BarButton {
            objectName: "fbSuperscriptButton"
            text: "x²"; flagBit: 0x100; font.pixelSize: 11
            onClicked: bar.target.toggleSpanType("superscript")
        }
        BarButton {
            objectName: "fbSubscriptButton"
            text: "x₂"; flagBit: 0x200; font.pixelSize: 11
            onClicked: bar.target.toggleSpanType("subscript")
        }
        BarButton {
            objectName: "fbLinkButton"
            text: qsTr("Link"); flagBit: 0x80; font.pixelSize: 11
            implicitWidth: 36; font.underline: true
            onClicked: bar.target.openLinkDialog()
        }
        BarButton {
            objectName: "fbColorButton"
            text: "A"; flagBit: 0x400
            onClicked: fbColorPicker.open()
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 3
                anchors.horizontalCenter: parent.horizontalCenter
                width: 14; height: 3; radius: 1
                color: (bar.target && bar.target.currentColor)
                    ? bar.target.currentColor : theme.textPrimary
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
