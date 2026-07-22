// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The line-number gutter is a Repeater whose delegate is its own component
// scope, and it reads the gutter width and font from this file's root by id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// Everything a code block draws around its text: the panel, the header with
// the language selector and the copy button, the optional line-number gutter,
// and the horizontal scrollbar long lines need because code does not wrap.
//
// The chrome measures nothing about the text. A code block's layout numbers —
// how wide the gutter is, where the text starts, how far it can scroll — are
// derived on the delegate, because the editor's own geometry depends on them
// and the two have to agree. They arrive here as values, and the gutter reads
// the editor's line count and content height the same way, so this file needs
// no reference to the editor at all.
//
// The scroll offset is the one value that travels in both directions: dragging
// the scrollbar reports a new offset, and the delegate feeds the result back
// as `hScroll` after applying it to the editor's inset.
Item {
    id: root

    // The block's language, shown on the selector button. Empty means the
    // block has no language set, which the button labels "plain text".
    property string language: ""
    // The block's markdown, which the copy button puts on the clipboard.
    property string copyText: ""

    property bool lineNumbers: false
    property int gutterWidth: 0
    property int headerHeight: 0
    // Left inset of the code text, past the gutter.
    property int contentLeft: 0
    // Visible width of the text, and how far it can scroll beyond it.
    property int viewportWidth: 1
    property int maxScroll: 0
    property int hScroll: 0

    property string fontFamily: ""
    property int fontPixelSize: 0
    // Top of the editor's first text line, which the gutter aligns to.
    property real textTop: 0
    // The editor's line count and total text height, which together give the
    // gutter its per-line row height.
    property int lineCount: 0
    property real textContentHeight: 0
    property real textContentWidth: 0

    signal languageChosen(string language)
    signal hScrollRequested(int offset)

    Rectangle {
        id: panel
        objectName: "codePanel"
        anchors.fill: parent
        color: Theme.codePanelBackground
        radius: 4
        border.width: 1
        border.color: Theme.border
    }

    // One Text holding every line number, not one Text per line. The gutter
    // used to be a Repeater over lineCount, so a 10,000-line fence built
    // 10,000 QML items — each with its own geometry, font and scene-graph
    // node — to draw 10,000 short numbers that share one font and one column.
    // A single Text with a fixed line height puts them at the same positions
    // for one item.
    Item {
        id: gutter
        objectName: "codeGutter"
        visible: root.lineNumbers
        width: root.gutterWidth
        height: root.textContentHeight
        x: 0
        y: root.textTop
        z: 2

        // The row height the numbers are spaced on, which is what the
        // per-line Text heights used to be set to.
        readonly property real rowHeight:
            root.lineCount > 0 ? root.textContentHeight / root.lineCount : 0

        Text {
            objectName: "codeGutterNumbers"
            width: root.gutterWidth - 8
            horizontalAlignment: Text.AlignRight
            color: Theme.textFaint
            font.family: root.fontFamily
            font.pixelSize: root.fontPixelSize
            lineHeightMode: Text.FixedHeight
            lineHeight: gutter.rowHeight
            // Built only while the gutter is shown, so a fence with numbers
            // turned off does not pay for the string either.
            text: {
                if (!root.lineNumbers || root.lineCount <= 0)
                    return ""
                var parts = []
                for (var i = 1; i <= root.lineCount; i++)
                    parts.push(i)
                return parts.join("\n")
            }
        }
    }
    // Opaque backing for the gutter column: the text scrolls under it, and
    // without this the first characters of a scrolled long line show through
    // between the numbers.
    Rectangle {
        visible: root.lineNumbers
        x: 0
        y: 0
        width: root.gutterWidth
        height: root.height
        color: Theme.codePanelBackground
        z: 1
    }

    Item {
        id: header
        objectName: "codeHeader"
        x: 0
        y: 0
        z: 3
        width: root.width
        height: root.headerHeight

        Rectangle {
            id: langButton
            objectName: "codeLanguageButton"
            height: 20
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: langLabel.implicitWidth + 20
            radius: 3
            color: langHover.hovered ? Theme.hoverTint : "transparent"
            Text {
                id: langLabel
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: (root.language && root.language.length > 0)
                      ? root.language : "plain text"
                color: Theme.textMuted
                font.pixelSize: Math.max(10, root.fontPixelSize - 3)
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 5
                anchors.verticalCenter: parent.verticalCenter
                text: "▾"
                color: Theme.textFaint
                font.pixelSize: 9
            }
            HoverHandler { id: langHover }
            TapHandler { onTapped: languagePicker.open() }
        }

        Rectangle {
            id: copyButton
            objectName: "codeCopyButton"
            height: 20
            width: copyLabel.implicitWidth + 14
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            radius: 3
            color: copyHover.hovered ? Theme.hoverTint : "transparent"
            Text {
                id: copyLabel
                anchors.centerIn: parent
                text: copyButton.copied ? "Copied" : "Copy"
                color: Theme.textMuted
                font.pixelSize: Math.max(10, root.fontPixelSize - 3)
            }
            property bool copied: false
            HoverHandler { id: copyHover }
            TapHandler {
                onTapped: {
                    Clipboard.text = root.copyText
                    copyButton.copied = true
                    copyResetTimer.restart()
                }
            }
            Timer {
                id: copyResetTimer
                interval: 1200
                onTriggered: copyButton.copied = false
            }
        }
    }

    // A QQC2 Menu owns a `delegate` property, and inside its instantiation
    // scope attributes outrank document ids — so a binding written in the
    // picker would resolve names against the Menu rather than this file.
    // Binding and Connections from sibling scope keep the ids resolving here.
    LanguagePicker {
        id: languagePicker
        objectName: "languagePicker"
    }
    Binding {
        target: languagePicker
        property: "currentLanguage"
        value: root.language || ""
    }
    Connections {
        target: languagePicker
        function onLanguageChosen(lang) {
            root.languageChosen(lang)
        }
    }

    ScrollBar {
        id: hScrollBar
        objectName: "codeHScrollBar"
        visible: root.maxScroll > 0
        orientation: Qt.Horizontal
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.contentLeft
        height: 8
        z: 2
        size: root.viewportWidth / Math.max(1, root.textContentWidth)
        position: root.maxScroll > 0
            ? root.hScroll / (root.maxScroll + root.viewportWidth)
            : 0
        onPositionChanged: {
            if (pressed) {
                root.hScrollRequested(Math.round(
                    position * (root.maxScroll + root.viewportWidth)))
            }
        }
    }
}
