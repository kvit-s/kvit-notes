// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// The document outline pane (features.md §17.1): a
// collapsible heading tree projected by DocumentOutline. Clicking a heading
// scrolls to it; the section containing the caret lights up live; a level
// filter chooses which heading levels appear. All state lives in
// DocumentOutline; this pane renders and forwards.
Rectangle {
    id: outline
    objectName: "outlinePanel"

    property var appWindow

    color: Theme.panelBackground

    // Left divider.
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    // Keep the current section visible as the caret moves between headings.
    Connections {
        target: DocumentOutline
        function onCurrentRowChanged() {
            if (DocumentOutline.currentRow >= 0)
                outlineList.positionViewAtIndex(
                    DocumentOutline.currentRow, ListView.Contain)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        // Header: title + level-filter menu.
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
                anchors.rightMargin: 4
                Text {
                    text: qsTr("Outline")
                    font.pixelSize: 12
                    font.bold: true
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                }
                ToolButton {
                    objectName: "outlineLevelButton"
                    text: "H…"
                    font.pixelSize: 11
                    focusPolicy: Qt.NoFocus
                    implicitWidth: 30
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Heading levels shown")
                    onClicked: levelMenu.open()
                    Menu {
                        id: levelMenu
                        y: parent.height
                        Repeater {
                            model: 4
                            MenuItem {
                                required property int index
                                text: qsTr("Heading ") + (index + 1)
                                checkable: true
                                checked: (DocumentOutline.levelMask
                                          & (1 << index)) !== 0
                                onTriggered: {
                                    var bit = 1 << index
                                    var mask = DocumentOutline.levelMask
                                    // Never allow an all-empty mask; keep at
                                    // least this level on.
                                    var next = checked ? (mask | bit)
                                                       : (mask & ~bit)
                                    DocumentOutline.levelMask =
                                        next === 0 ? bit : next
                                }
                            }
                        }
                    }
                }
                ToolButton {
                    objectName: "outlineCloseButton"
                    text: "✕"
                    font.pixelSize: 11
                    focusPolicy: Qt.NoFocus
                    implicitWidth: 26
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Hide outline")
                    onClicked: if (appWindow) appWindow.outlineVisible = false
                }
            }
        }

        // Empty state.
        Text {
            visible: !DocumentOutline.hasHeadings
            Layout.fillWidth: true
            Layout.margins: 12
            text: qsTr("No headings yet. Add a heading to build the outline.")
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: Theme.textFaint
        }

        ListView {
            id: outlineList
            objectName: "outlineList"
            visible: DocumentOutline.hasHeadings
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: DocumentOutline
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: row
                width: outlineList.width
                height: 26
                color: isCurrent ? Theme.selectionTint
                                 : (hover.hovered ? Theme.hoverTint
                                                  : "transparent")

                // Current-section accent bar.
                Rectangle {
                    visible: isCurrent
                    anchors.left: parent.left
                    width: 2; height: parent.height
                    color: Theme.accent
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8 + depth * 14
                    anchors.rightMargin: 6
                    spacing: 2

                    // Collapse chevron (only for headings with a subtree).
                    ToolButton {
                        objectName: "outlineChevron"
                        visible: hasChildren
                        implicitWidth: 16
                        implicitHeight: 16
                        focusPolicy: Qt.NoFocus
                        text: collapsed ? "▸" : "▾"
                        font.pixelSize: 10
                        onClicked: DocumentOutline.toggleCollapsed(index)
                    }
                    // Indent placeholder when there is no chevron, so text
                    // aligns with siblings that have one.
                    Item {
                        visible: !hasChildren
                        implicitWidth: 16; implicitHeight: 16
                    }

                    Text {
                        Layout.fillWidth: true
                        text: model.text
                        elide: Text.ElideRight
                        font.pixelSize: level === 1 ? 12 : 11
                        font.bold: level === 1
                        color: isCurrent ? Theme.textPrimary : Theme.textSecondary
                    }
                }

                HoverHandler { id: hover }
                TapHandler {
                    onTapped: {
                        if (appWindow)
                            appWindow.scrollToBlock(blockIndex)
                    }
                }
            }
        }
    }
}
