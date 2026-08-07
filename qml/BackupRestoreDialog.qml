// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The backup list's rows are a delegate, which is its own component scope.
// Binding it lets a row address the dialog it belongs to by id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// Restoring the open note from one of its backups.
//
// The collection rotates a copy of a note before overwriting it, and this
// lists those copies with their timestamps above a rendered preview of the
// version under the cursor. The preview is a ReadOnlyDocument
// (selection.md "A document drawn read-only"): the stored markdown drawn as
// blocks, with its inline markers hidden and its pictures drawn as pictures,
// which the reader can sweep across and copy out as markdown, and whose links
// open what they name, all without restoring anything. It is there because a
// timestamp and a fragment of a first line do not tell two edits of the same
// afternoon apart, and because wanting one paragraph out of an old version is
// commoner than wanting the whole of it back.
//
// Restoring applies the chosen body through the block model as one undo step,
// so a restore that turns out to be wrong costs one Ctrl+Z, which is why it
// asks no second question.
KvitDialog {
    id: backupDialog
    objectName: "backupDialog"

    // Wired by main.qml.
    property var appWindow

    modal: true
    anchors.centerIn: parent
    width: Interface.px(560)
    title: qsTr("Restore from Backup")

    property var backups: []
    property int selectedRow: 0

    // The markdown of the version under the cursor, which is what the preview
    // draws. Read from the stored copy on every change of the cursor; the
    // file is opened for reading only, and nothing here writes anywhere.
    readonly property string selectedBody: {
        if (!backupDialog.visible || backupDialog.selectedRow < 0
            || backupDialog.selectedRow >= backupDialog.backups.length)
            return ""
        var current = backupDialog.appWindow
            ? backupDialog.appWindow.currentNoteRelPath : ""
        if (current === "")
            return ""
        return NoteCollection.backupBody(
            current, backupDialog.backups[backupDialog.selectedRow].fileName)
    }

    function openForCurrentNote() {
        if (backupDialog.appWindow.currentNoteRelPath === "")
            return
        backups = NoteCollection.backupsFor(
            backupDialog.appWindow.currentNoteRelPath)
        selectedRow = 0
        open()
    }

    onAccepted: {
        if (selectedRow < 0 || selectedRow >= backups.length)
            return
        var body = NoteCollection.backupBody(
            backupDialog.appWindow.currentNoteRelPath,
            backups[selectedRow].fileName)
        if (DocumentManager.restoreBody(body))
            DocumentManager.save()
    }

    contentItem: ColumnLayout {
        spacing: Interface.px(4)
        Label {
            visible: backupDialog.backups.length === 0
            text: qsTr("No backups yet — they appear as the note is edited over time.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            padding: Interface.px(8)
        }
        ListView {
            objectName: "backupDialogList"
            visible: backupDialog.backups.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(count * 44, 220)
            clip: true
            model: backupDialog.backups
            delegate: Rectangle {
                id: backupRow
                required property int index
                required property var modelData
                width: parent ? parent.width : 0
                height: Interface.px(44)
                color: backupRow.index === backupDialog.selectedRow
                       ? Theme.selectionTint : "transparent"
                Column {
                    anchors.fill: parent
                    anchors.margins: Interface.px(6)
                    spacing: Interface.px(2)
                    Label {
                        text: Qt.formatDateTime(backupRow.modelData.timestamp,
                                                "MMM d, yyyy hh:mm:ss")
                        font.pixelSize: Interface.body
                        font.bold: true
                    }
                    Label {
                        text: backupRow.modelData.preview !== ""
                              ? backupRow.modelData.preview
                              : qsTr("(empty)")
                        font.pixelSize: Interface.small
                        color: Theme.textFaint
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: Qt.formatDateTime(
                    backupRow.modelData.timestamp, "MMMM d, yyyy, hh:mm:ss")
                Accessible.description: backupRow.modelData.preview !== ""
                                        ? backupRow.modelData.preview
                                        : qsTr("(empty)")
                Accessible.selected: backupDialog.selectedRow === backupRow.index
                Accessible.onPressAction:
                    backupDialog.selectedRow = backupRow.index
                MouseArea {
                    anchors.fill: parent
                    onClicked: backupDialog.selectedRow = backupRow.index
                }
            }
        }

        // The version under the cursor, drawn.
        Rectangle {
            objectName: "backupPreviewPane"
            visible: backupDialog.backups.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Interface.px(240)
            color: Theme.listBackground
            border.color: Theme.border
            border.width: 1
            radius: 4
            clip: true

            Flickable {
                id: previewScroll
                anchors.fill: parent
                anchors.margins: Interface.px(8)
                contentWidth: width
                contentHeight: previewLoader.height
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                // Behind a Loader: a dialog's content item is built with the
                // shell whether the dialog is ever opened or not, and a
                // surface holds a document model, a selection and an editing
                // engine per block. Nothing exists until somebody asks to
                // restore something.
                Loader {
                    id: previewLoader
                    width: previewScroll.width
                    height: item ? (item as Item).implicitHeight : 0
                    active: backupDialog.visible
                            && backupDialog.backups.length > 0
                    sourceComponent: previewComponent
                }
            }

            Label {
                anchors.centerIn: parent
                visible: backupDialog.selectedBody === ""
                text: qsTr("(empty)")
                color: Theme.textFaint
                font.pixelSize: Interface.small
            }

            Component {
                id: previewComponent
                ReadOnlyDocument {
                    objectName: "backupPreviewDocument"
                    // A stored version is read at a glance rather than at
                    // reading length, so the blank rhythm between its blocks
                    // is tighter than the editor's.
                    blockSpacing: Math.max(4, Math.round(
                        Typography.paragraphSpacing * 0.6))
                    markdown: backupDialog.selectedBody
                    // `baseDir` is left at the surface's default, the open
                    // note's directory, which is the right answer here and
                    // not the obvious one: the stored copy sits under
                    // .kvit/backups, while the picture paths inside it are
                    // still written against the folder the note itself is in.
                }
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "backupDialogRestoreButton"
            text: qsTr("Restore")
            enabled: backupDialog.backups.length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
}
