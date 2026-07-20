// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// The backlinks pane: referring notes for the open note, with the context
// lines their links appear on. Rows come from NoteCollection::backlinksTo,
// re-queried on every collection revision — which the FileWatcher bumps on
// external edits — and on note switches. Clicking a row (or a context line)
// opens the referring note.
Rectangle {
    id: panel
    objectName: "backlinksPanel"

    property var appWindow

    // [{relPath, title, count, contexts}] for the open note.
    property var rows: []

    color: Theme.panelBackground

    function refresh() {
        var current = appWindow ? appWindow.currentNoteRelPath : ""
        rows = (visible && current !== "")
            ? NoteCollection.backlinksTo(current) : []
    }

    onVisibleChanged: refresh()
    Connections {
        target: NoteCollection
        function onRevisionChanged() { panel.refresh() }
        function onRootChanged() { panel.refresh() }
    }
    Connections {
        target: appWindow
        function onCurrentNoteRelPathChanged() { panel.refresh() }
    }

    // Left divider.
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: Theme.panelBackground
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width; height: 1; color: Theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                Text {
                    text: qsTr("Backlinks")
                    font.pixelSize: 12
                    font.bold: true
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                }
                Text {
                    objectName: "backlinksCount"
                    text: {
                        var total = 0
                        for (var i = 0; i < panel.rows.length; ++i)
                            total += panel.rows[i].count
                        return total > 0 ? String(total) : ""
                    }
                    font.pixelSize: 11
                    color: Theme.textFaint
                }
            }
        }

        ListView {
            id: backlinksList
            objectName: "backlinksList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.rows
            spacing: 2

            delegate: Column {
                id: entryColumn
                required property var modelData
                // Captured under a distinct name: the context Repeater's
                // own modelData shadows this one in inner scopes.
                readonly property var entryData: modelData
                width: backlinksList.width
                padding: 0

                Rectangle {
                    width: parent.width
                    height: titleRow.implicitHeight + 12
                    color: rowHover.hovered ? Theme.hoverTint : "transparent"

                    RowLayout {
                        id: titleRow
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        Text {
                            text: modelData.title
                            font.pixelSize: 13
                            font.bold: true
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: modelData.count
                            font.pixelSize: 11
                            color: Theme.textFaint
                        }
                    }
                    HoverHandler { id: rowHover }
                    TapHandler {
                        onTapped: if (panel.appWindow)
                            panel.appWindow.openNoteByPath(modelData.relPath)
                    }
                }

                Repeater {
                    model: modelData.contexts
                    Rectangle {
                        required property var modelData
                        width: backlinksList.width
                        height: contextText.implicitHeight + 8
                        color: ctxHover.hovered ? Theme.hoverTint
                                                : "transparent"
                        Text {
                            id: contextText
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 22
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            text: modelData
                            font.pixelSize: 11
                            color: Theme.textMuted
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                        HoverHandler { id: ctxHover }
                        TapHandler {
                            onTapped: if (panel.appWindow)
                                panel.appWindow.openNoteByPath(
                                    entryColumn.entryData.relPath)
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: panel.rows.length === 0
                text: qsTr("No backlinks")
                font.pixelSize: 12
                color: Theme.textFaint
            }
        }
    }
}
