// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// A source file as text, never as a note and never as an editing surface.
// TextFileViewModel has already enforced the byte cap and UTF-8 check; this
// item supplies line numbers, whole-file selection/copy, syntax colour and an
// addressable highlighted line.
Rectangle {
    id: surface
    objectName: "readOnlyTextFile"
    color: Theme.windowBackground

    function revealRequestedLine() {
        if (TextFileViewModel.state !== "ready")
            return
        var y = codeArea.topPadding
              + (TextFileViewModel.requestedLine - 1) * metrics.lineSpacing
        sourceFlick.contentY = Math.max(
            0, Math.min(y - sourceFlick.height / 3,
                        sourceFlick.contentHeight - sourceFlick.height))
    }

    Connections {
        target: TextFileViewModel
        function onChanged() { Qt.callLater(surface.revealRequestedLine) }
    }
    Component.onCompleted: Qt.callLater(surface.revealRequestedLine)

    FontMetrics {
        id: metrics
        font.family: Typography.monoFamily
        font.pixelSize: Typography.monoSize
    }

    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Interface.px(36)
        color: Theme.panelBackground
        border.color: Theme.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Interface.px(12)
            anchors.rightMargin: Interface.px(8)
            spacing: Interface.px(8)
            Label {
                Layout.fillWidth: true
                text: TextFileViewModel.path
                elide: Text.ElideMiddle
                color: Theme.textSecondary
                font.pixelSize: Interface.small
            }
            Label {
                visible: TextFileViewModel.language !== ""
                text: TextFileViewModel.language
                color: Theme.textFaint
                font.pixelSize: Interface.caption
            }
            Button {
                objectName: "textFileDesktopButton"
                text: qsTr("Open with desktop")
                onClicked: UrlLauncher.open(
                    DocumentManager.toLocalFileUrl(
                        TextFileViewModel.path).toString())
            }
        }
    }

    Item {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        Flickable {
            id: sourceFlick
            objectName: "textFileFlickable"
            anchors.fill: parent
            visible: TextFileViewModel.state === "ready"
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: Math.max(width,
                                   codeArea.implicitWidth + gutter.width)
            contentHeight: Math.max(height, codeArea.implicitHeight)

            TextArea {
                id: codeArea
                objectName: "textFileTextArea"
                x: gutter.width
                width: Math.max(implicitWidth, sourceFlick.width - gutter.width)
                text: TextFileViewModel.text
                readOnly: true
                selectByMouse: true
                persistentSelection: true
                wrapMode: TextEdit.NoWrap
                font.family: Typography.monoFamily
                font.pixelSize: Typography.monoSize
                color: Theme.textPrimary
                padding: Interface.px(8)
                background: null
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Read-only text file")

                CodeHighlighter {
                    document: codeArea.textDocument
                    language: TextFileViewModel.language
                    theme: Theme
                }
            }

            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}
        }

        Rectangle {
            id: requestedLineMark
            objectName: "textFileRequestedLineMark"
            visible: sourceFlick.visible
            x: gutter.width
            width: Math.max(0, parent.width - gutter.width)
            height: metrics.lineSpacing
            y: codeArea.topPadding
               + (TextFileViewModel.requestedLine - 1) * metrics.lineSpacing
               - sourceFlick.contentY
            color: Theme.selectionTint
            opacity: 0.45
            z: -1
        }

        Rectangle {
            id: gutter
            objectName: "textFileLineNumberGutter"
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: Interface.px(52)
            visible: sourceFlick.visible
            color: Theme.codePanelBackground

            ListView {
                anchors.fill: parent
                interactive: false
                clip: true
                contentY: sourceFlick.contentY
                model: TextFileViewModel.lineCount
                topMargin: codeArea.topPadding
                delegate: Label {
                    required property int index
                    width: gutter.width - Interface.px(8)
                    height: metrics.lineSpacing
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    text: index + 1
                    color: index + 1 === TextFileViewModel.requestedLine
                           ? Theme.accent : Theme.textFaint
                    font.family: Typography.monoFamily
                    font.pixelSize: Typography.monoSize
                    Accessible.ignored: true
                }
            }
            Rectangle {
                anchors.right: parent.right
                width: Interface.px(1)
                height: parent.height
                color: Theme.border
            }
        }

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - Interface.px(40), Interface.px(520))
            spacing: Interface.px(12)
            visible: TextFileViewModel.state !== "ready"
                     && TextFileViewModel.state !== "empty"
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: TextFileViewModel.state === "tooLarge"
                      ? qsTr("File too large")
                      : TextFileViewModel.state === "binary"
                        ? qsTr("Not a text file") : qsTr("Cannot open file")
                font.bold: true
                font.pixelSize: Interface.title
                color: Theme.textPrimary
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: TextFileViewModel.message
                color: Theme.textMuted
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Open with desktop")
                onClicked: UrlLauncher.open(
                    DocumentManager.toLocalFileUrl(
                        TextFileViewModel.path).toString())
            }
        }
    }
}
