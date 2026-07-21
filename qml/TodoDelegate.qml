// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Delegates in this file read ids from the enclosing component scope,
// which qmllint reports as unqualified access. Binding those ids into
// the nested scopes resolves it; the delegates here already declare a
// required property for every model role they read, so nothing relied on
// the injection this turns off.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// Todo / checkbox block (features.md §1.2.3): a leading checkbox toggles the
// model's checked state; completed items render struck through. The block also
// carries the Obsidian Tasks metadata tail — a due-date chip and a priority
// flag rendered as chrome (excluded from the editable text via metaTail),
// edited through a date picker and a priority cycle — plus a sub-task
// progress badge computed from the deeper-indented todo children.
EditableBlock {
    id: root

    contentColor: root.checked ? Theme.textFaint : Theme.textPrimary
    contentStrikeout: root.checked

    // The metadata tail (📅 date / priority emoji) is chrome, not text.
    metaTail: TodoMeta.tail(content)
    readonly property var meta: TodoMeta.parse(content)
    // Sub-task progress. It is derived from the CHILD rows, so nothing this
    // delegate's own roles carry can signal a change: checking a child,
    // retyping it, reindenting it, or moving it all leave this row's data
    // untouched. derivedRevision is the model's counter for exactly that
    // class of change, and reading it here is what makes the badge update.
    readonly property var progress: {
        var dep = BlockModel.count            // structural dependency
        var derived = BlockModel.derivedRevision   // child-state dependency
        return BlockModel.todoProgress(root.index)
    }
    readonly property bool overdue: {
        if (meta.due === "") return false
        var today = Qt.formatDate(new Date(), "yyyy-MM-dd")
        return meta.due < today && !root.checked
    }

    function setDue(iso) {
        BlockModel.updateContent(root.index,
            TodoMeta.build(meta.text, iso, meta.priority))
    }
    function cyclePriority() {
        var p = meta.priority
        var next = p === 0 ? -1 : (p === -1 ? 1 : (p === 1 ? 2 : 0))
        BlockModel.updateContent(root.index,
            TodoMeta.build(meta.text, meta.due, next))
    }

    leadingChrome: Component {
        Item {
            implicitWidth: 20
            Rectangle {
                id: checkbox
                objectName: "todoCheckbox"
                width: 16; height: 16; y: 3
                anchors.horizontalCenter: parent.horizontalCenter
                radius: 3
                color: root.checked ? Theme.accent : "transparent"
                border.color: root.checked ? Theme.accent : Theme.borderStrong
                border.width: 1.5
                Text {
                    anchors.centerIn: parent
                    visible: root.checked
                    text: "✓"; color: Theme.onAccent; font.pixelSize: 11; font.bold: true
                }
                MouseArea {
                    anchors.fill: parent; anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: BlockModel.setChecked(root.index, !root.checked)
                }
            }
        }
    }

    // Chips row below the todo text (right-aligned): progress, priority, due.
    trailingChrome: (root.progress.total > 0 || root.meta.priority !== 0
                     || root.meta.due !== "") ? chipsComponent : null
    Component {
        id: chipsComponent
        Item {
            implicitHeight: 22
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                // Sub-task progress badge "2/5".
                Rectangle {
                    visible: root.progress.total > 0
                    height: 18
                    width: progressText.implicitWidth + 12
                    radius: 9
                    color: Theme.chipBackground
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        id: progressText
                        anchors.centerIn: parent
                        text: root.progress.done + "/" + root.progress.total
                        color: root.progress.done === root.progress.total
                               ? Theme.success : Theme.textMuted
                        font.pixelSize: 11
                    }
                }

                // Priority flag (click cycles none→low→med→high).
                Rectangle {
                    objectName: "todoPriorityChip"
                    visible: true
                    height: 18; width: 24; radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.meta.priority !== 0 ? Qt.alpha(prioColor, 0.18)
                                                    : Theme.chipBackground
                    property color prioColor: root.meta.priority === 2 ? Theme.danger
                        : root.meta.priority === 1 ? Theme.warning
                        : root.meta.priority === -1 ? Theme.accent : Theme.textFaint
                    Text {
                        anchors.centerIn: parent
                        text: root.meta.priority === 2 ? "▲▲"
                            : root.meta.priority === 1 ? "▲"
                            : root.meta.priority === -1 ? "▼" : "–"
                        color: parent.prioColor
                        font.pixelSize: 10
                    }
                    TapHandler { onTapped: root.cyclePriority() }
                }

                // Due-date chip (click opens the picker; red when overdue).
                Rectangle {
                    objectName: "todoDueChip"
                    height: 18
                    width: dueText.implicitWidth + 16
                    radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.overdue ? Qt.alpha(Theme.danger, 0.18)
                         : root.meta.due !== "" ? Theme.chipBackground : "transparent"
                    border.width: root.meta.due === "" ? 1 : 0
                    border.color: Theme.border
                    Text {
                        id: dueText
                        anchors.centerIn: parent
                        text: root.meta.due !== ""
                              ? "◷ " + Qt.formatDate(new Date(root.meta.due), "MMM d")
                              : "◷ Set date"
                        color: root.overdue ? Theme.danger : Theme.textMuted
                        font.pixelSize: 11
                    }
                    TapHandler { onTapped: dueDatePopup.open() }
                }
            }

            // A compact single-date picker.
            Popup {
                id: dueDatePopup
                y: parent.height
                x: parent.width - 240
                width: 240
                padding: 8
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                background: Rectangle {
                    color: Theme.popupBackground
                    border.color: Theme.borderStrong; border.width: 1; radius: 6
                }
                property date shown: root.meta.due !== "" ? new Date(root.meta.due) : new Date()
                contentItem: Column {
                    spacing: 6
                    Row {
                        width: parent.width
                        Button { text: "‹"; flat: true; focusPolicy: Qt.NoFocus
                            onClicked: dueDatePopup.shown = new Date(
                                dueDatePopup.shown.getFullYear(),
                                dueDatePopup.shown.getMonth() - 1, 1) }
                        Text {
                            width: 140
                            horizontalAlignment: Text.AlignHCenter
                            anchors.verticalCenter: parent.verticalCenter
                            text: Qt.formatDate(dueDatePopup.shown, "MMMM yyyy")
                            color: Theme.textPrimary; font.pixelSize: 12
                        }
                        Button { text: "›"; flat: true; focusPolicy: Qt.NoFocus
                            onClicked: dueDatePopup.shown = new Date(
                                dueDatePopup.shown.getFullYear(),
                                dueDatePopup.shown.getMonth() + 1, 1) }
                    }
                    MonthGrid {
                        id: monthGrid
                        width: 224
                        month: dueDatePopup.shown.getMonth()
                        year: dueDatePopup.shown.getFullYear()
                        delegate: Text {
                            required property var model
                            horizontalAlignment: Text.AlignHCenter
                            text: model.day
                            color: model.month === monthGrid.month
                                   ? Theme.textPrimary : Theme.textDisabled
                            font.pixelSize: 11
                            opacity: model.month === monthGrid.month ? 1 : 0.5
                        }
                        onClicked: function(date) {
                            root.setDue(Qt.formatDate(date, "yyyy-MM-dd"))
                            dueDatePopup.close()
                        }
                    }
                    Button {
                        text: qsTr("Clear date")
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: 11
                        visible: root.meta.due !== ""
                        onClicked: { root.setDue(""); dueDatePopup.close() }
                    }
                }
            }
        }
    }
}
