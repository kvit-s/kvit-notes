// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

Item {
    id: pane
    objectName: "fileTreePane"
    property var appWindow

    function focusPane() {
        if (tree.currentIndex < 0 && tree.count > 0)
            tree.currentIndex = 0
        tree.forceActiveFocus(Qt.TabFocusReason)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Interface.px(8)
            Label {
                Layout.fillWidth: true
                text: qsTr("Files")
                font.bold: true
                color: Theme.textSecondary
            }
            ToolButton {
                objectName: "showNotesTreeButton"
                text: qsTr("Notes")
                Accessible.name: qsTr("Show notes sidebar")
                onClicked: if (pane.appWindow)
                               pane.appWindow.sidebarView = "notes"
            }
            ToolButton {
                text: "«"
                Accessible.name: qsTr("Collapse sidebar")
                onClicked: if (pane.appWindow)
                               pane.appWindow.sidebarCollapsed = true
            }
        }

        ListView {
            id: tree
            objectName: "fileTreeView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: FileTreeModel
            activeFocusOnTab: true
            keyNavigationEnabled: true
            highlightMoveDuration: 0
            Accessible.role: Accessible.Tree
            Accessible.name: qsTr("Files in this folder")
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    || event.key === Qt.Key_Space) {
                    FileTreeModel.activate(currentIndex)
                    event.accepted = true
                } else if (event.key === Qt.Key_Right) {
                    var item = FileTreeModel.entryAt(currentIndex)
                    if (item.directory && !item.expanded)
                        FileTreeModel.toggleExpanded(currentIndex)
                    event.accepted = true
                } else if (event.key === Qt.Key_Left) {
                    var current = FileTreeModel.entryAt(currentIndex)
                    if (current.directory && current.expanded)
                        FileTreeModel.toggleExpanded(currentIndex)
                    event.accepted = true
                }
            }

            delegate: Rectangle {
                id: row
                required property int index
                required property string relativePath
                required property string name
                required property int depth
                required property bool directory
                required property bool expanded
                required property string kind
                width: tree.width
                height: Interface.px(27)
                color: tree.currentIndex === row.index && tree.activeFocus
                       ? Theme.focusTint
                       : (hover.hovered ? Theme.hoverTint : "transparent")
                Accessible.role: row.directory ? Accessible.TreeItem
                                                : Accessible.ListItem
                Accessible.name: row.name
                Accessible.description: row.directory ? qsTr("Folder")
                    : row.kind === "markdown" ? qsTr("Markdown file")
                    : row.kind === "text" ? qsTr("Text file")
                    : row.kind === "image" ? qsTr("Image file")
                    : row.kind === "media" ? qsTr("Media file")
                    : qsTr("File opened by the desktop")
                Accessible.onPressAction: FileTreeModel.activate(row.index)

                HoverHandler { id: hover }
                TapHandler {
                    onTapped: {
                        tree.currentIndex = row.index
                        FileTreeModel.activate(row.index)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Interface.px(8 + row.depth * 14)
                    anchors.rightMargin: Interface.px(6)
                    spacing: Interface.px(5)
                    Label {
                        text: row.directory ? (row.expanded ? "▾" : "▸")
                              : row.kind === "markdown" ? "M"
                              : row.kind === "text" ? "#"
                              : row.kind === "image" ? "▧"
                              : row.kind === "media" ? "▶" : "•"
                        color: Theme.textFaint
                        font.family: Typography.monoFamily
                        font.pixelSize: Interface.small
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.name
                        elide: Text.ElideMiddle
                        color: Theme.textPrimary
                        font.pixelSize: Interface.body
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
