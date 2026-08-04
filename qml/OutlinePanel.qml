// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The outline rows nest a RowLayout and buttons, each its own scope,
// reading model roles that arrive by injection. Binding the scopes
// means the roles are declared on the row and addressed through it.
pragma ComponentBehavior: Bound

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
        width: Interface.px(1)
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
        anchors.leftMargin: Interface.px(1)
        spacing: 0

        // Header: title + level-filter menu.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Interface.px(32)
            color: Theme.panelBackground
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width; height: 1; color: Theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Interface.px(10)
                anchors.rightMargin: Interface.px(4)
                Text {
                    text: qsTr("Outline")
                    font.pixelSize: Interface.body
                    font.bold: true
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                }
                ToolButton {
                    objectName: "outlineLevelButton"
                    text: "H…"
                    font.pixelSize: Interface.small
                    focusPolicy: Qt.NoFocus
                    implicitWidth: Interface.px(30)
                    ToolTip.visible: hovered || visualFocus
                    ToolTip.text: qsTr("Heading levels shown")
                    onClicked: levelMenu.open()
                    Menu {
                        id: levelMenu
                        y: parent.height
                        Repeater {
                            model: 4
                            MenuItem {
                                required property int index
                                text: MenuText.label(qsTr("Heading &%1").arg(index + 1))
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
                    Accessible.name: qsTr("Hide outline")
                    font.pixelSize: Interface.small
                    focusPolicy: Qt.NoFocus
                    implicitWidth: Interface.px(26)
                    ToolTip.visible: hovered || visualFocus
                    ToolTip.text: qsTr("Hide outline")
                    onClicked: if (outline.appWindow) outline.appWindow.outlineVisible = false
                }
            }
        }

        // Empty state.
        Text {
            visible: !DocumentOutline.hasHeadings
            Layout.fillWidth: true
            Layout.margins: Interface.px(12)
            text: qsTr("No headings yet. Add a heading to build the outline.")
            wrapMode: Text.WordWrap
            font.pixelSize: Interface.small
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
                // The roles DocumentOutline exposes, declared rather than
                // injected, so the nested RowLayout and its buttons can read
                // them through the row's id.
                required property int index
                required property int level
                required property string text
                required property int blockIndex
                required property int depth
                required property bool collapsed
                required property bool hasChildren
                required property bool isCurrent
                width: outlineList.width
                height: Interface.px(26)
                color: row.isCurrent ? Theme.selectionTint
                                 : (hover.hovered ? Theme.hoverTint
                                                  : "transparent")

                // Current-section accent bar.
                Rectangle {
                    visible: row.isCurrent
                    anchors.left: parent.left
                    width: 2; height: parent.height
                    color: Theme.accent
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8 + row.depth * 14
                    anchors.rightMargin: Interface.px(6)
                    spacing: Interface.px(2)

                    // Collapse chevron (only for headings with a subtree).
                    ToolButton {
                        objectName: "outlineChevron"
                        visible: row.hasChildren
                        implicitWidth: Interface.px(16)
                        implicitHeight: Interface.px(16)
                        focusPolicy: Qt.NoFocus
                        text: row.collapsed ? "▸" : "▾"
                        font.pixelSize: Interface.caption
                        onClicked: DocumentOutline.toggleCollapsed(row.index)
                    }
                    // Indent placeholder when there is no chevron, so text
                    // aligns with siblings that have one.
                    Item {
                        visible: !row.hasChildren
                        implicitWidth: 16; implicitHeight: 16
                    }

                    Text {
                        Layout.fillWidth: true
                        text: row.text
                        elide: Text.ElideRight
                        font.pixelSize: row.level === 1 ? 12 : 11
                        font.bold: row.level === 1
                        color: row.isCurrent ? Theme.textPrimary : Theme.textSecondary
                    }
                }

                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("Heading level %1: %2")
                                 .arg(row.level).arg(row.text)
                Accessible.selected: row.isCurrent
                Accessible.onPressAction: {
                    if (outline.appWindow)
                        outline.appWindow.scrollToBlock(row.blockIndex)
                }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        if (outline.appWindow)
                            outline.appWindow.scrollToBlock(row.blockIndex)
                    }
                }
            }
        }
    }
}
