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
            implicitWidth: Math.max(20, checkbox.width + 4)
            // Deliberately not `checkable`. An AbstractButton's own
            // `checked` flips on click, which would break the binding to the
            // model the first time anyone pressed it; the model stays the one
            // source of truth and the accessible state is published from it.
            IconButton {
                id: checkbox
                objectName: "todoCheckbox"
                // Sized from the content font and centred on the first text
                // line, rather than a fixed box pinned near the block top —
                // which left it riding above its own label.
                width: Math.max(14, Math.round(root.contentFontSize * 1.05))
                height: width
                y: root.contentTextTop
                   + Math.round((root.contentAscent - height) / 2)
                anchors.horizontalCenter: parent.horizontalCenter

                label: qsTr("Done")
                help: qsTr("Done (Ctrl+Return)")
                Accessible.role: Accessible.CheckBox
                Accessible.checkable: true
                Accessible.checked: root.checked
                Accessible.onToggleAction: checkbox.clicked()
                onClicked: BlockModel.setChecked(root.index, !root.checked)

                background: Rectangle {
                    radius: 3
                    color: root.checked ? Theme.accent : "transparent"
                    border.color: checkbox.activeFocus ? Theme.focusRing
                        : root.checked ? Theme.accent : Theme.borderStrong
                    border.width: checkbox.activeFocus ? 2 : 1.5
                }
                contentItem: Text {
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    visible: root.checked
                    text: "✓"; color: Theme.onAccent
                    font.pixelSize: Math.round(checkbox.height * 0.7)
                    font.bold: true
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
                        font.pixelSize: Interface.small
                    }
                }

                // Priority flag (click cycles none→low→med→high). The glyph
                // is a triangle count, which says nothing out loud, so the
                // name spells the level out and says what pressing does.
                IconButton {
                    id: priorityChip
                    objectName: "todoPriorityChip"
                    height: 18; width: 24
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property color prioColor:
                          root.meta.priority === 2 ? Theme.danger
                        : root.meta.priority === 1 ? Theme.warning
                        : root.meta.priority === -1 ? Theme.accent : Theme.textFaint
                    readonly property string levelName:
                          root.meta.priority === 2 ? qsTr("high")
                        : root.meta.priority === 1 ? qsTr("medium")
                        : root.meta.priority === -1 ? qsTr("low") : qsTr("none")
                    label: qsTr("Priority: %1").arg(levelName)
                    help: qsTr("Priority: %1 — press to cycle").arg(levelName)
                    onClicked: root.cyclePriority()
                    background: Rectangle {
                        radius: 4
                        color: root.meta.priority !== 0
                               ? Qt.alpha(priorityChip.prioColor, 0.18)
                               : Theme.chipBackground
                        border.width: priorityChip.activeFocus ? 2 : 0
                        border.color: Theme.focusRing
                    }
                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: root.meta.priority === 2 ? "▲▲"
                            : root.meta.priority === 1 ? "▲"
                            : root.meta.priority === -1 ? "▼" : "–"
                        color: priorityChip.prioColor
                        font.pixelSize: Interface.caption
                    }
                }

                // Due-date chip (click opens the picker; red when overdue).
                IconButton {
                    id: dueChip
                    objectName: "todoDueChip"
                    height: 18
                    // Measured off to the side rather than read back off the
                    // content item: a control sizes its content item from its
                    // own width, so taking the width from the content item
                    // would close the loop.
                    readonly property string chipText: root.meta.due !== ""
                        ? "◷ " + Qt.formatDate(new Date(root.meta.due), "MMM d")
                        : "◷ " + qsTr("Set date")
                    TextMetrics {
                        id: dueMetrics
                        font.pixelSize: Interface.small
                        text: dueChip.chipText
                    }
                    width: dueMetrics.width + 16
                    anchors.verticalCenter: parent.verticalCenter
                    label: root.meta.due !== ""
                           ? qsTr("Due %1").arg(
                               Qt.formatDate(new Date(root.meta.due), "MMMM d"))
                           : qsTr("Set a due date")
                    help: root.overdue
                          ? qsTr("Overdue: %1").arg(
                              Qt.formatDate(new Date(root.meta.due), "MMMM d"))
                          : dueChip.label
                    onClicked: dueDatePopup.open()
                    background: Rectangle {
                        radius: 4
                        color: root.overdue ? Qt.alpha(Theme.danger, 0.18)
                             : root.meta.due !== "" ? Theme.chipBackground
                                                    : "transparent"
                        border.width: dueChip.activeFocus ? 2
                                    : root.meta.due === "" ? 1 : 0
                        border.color: dueChip.activeFocus ? Theme.focusRing
                                                          : Theme.borderStrong
                    }
                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: dueChip.chipText
                        color: root.overdue ? Theme.danger : Theme.textMuted
                        font.pixelSize: Interface.small
                    }
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
                // Qt leaves a popup outside its window unless it is
                // given a margin, and a to-do can sit at the foot of a note.
                margins: 6
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
                            color: Theme.textPrimary; font.pixelSize: Interface.body
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
                            font.pixelSize: Interface.small
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
                        font.pixelSize: Interface.small
                        visible: root.meta.due !== ""
                        onClicked: { root.setDue(""); dueDatePopup.close() }
                    }
                }
            }
        }
    }
}
