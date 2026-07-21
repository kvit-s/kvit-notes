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
// lists those copies with their timestamps and a first line to recognize them
// by. Restoring applies the chosen body through the block model as one undo
// step, so a restore that turns out to be wrong costs one Ctrl+Z, which is
// why it asks no second question.
Dialog {
    id: backupDialog
    objectName: "backupDialog"

    // Wired by main.qml.
    property var appWindow

    modal: true
    anchors.centerIn: parent
    width: 420
    title: qsTr("Restore from Backup")

    property var backups: []
    property int selectedRow: 0

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
        spacing: 4
        Label {
            visible: backupDialog.backups.length === 0
            text: qsTr("No backups yet — they appear as the note is edited over time.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            padding: 8
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
                height: 44
                color: backupRow.index === backupDialog.selectedRow
                       ? Theme.selectionTint : "transparent"
                Column {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 2
                    Label {
                        text: Qt.formatDateTime(backupRow.modelData.timestamp,
                                                "MMM d, yyyy hh:mm:ss")
                        font.pixelSize: 12
                        font.bold: true
                    }
                    Label {
                        text: backupRow.modelData.preview !== ""
                              ? backupRow.modelData.preview
                              : qsTr("(empty)")
                        font.pixelSize: 11
                        color: Theme.textFaint
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: backupDialog.selectedRow = backupRow.index
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
