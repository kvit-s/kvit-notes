// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Kvit 1.0

// Import dialog (features.md §12.6): bring markdown/text
// files or a whole folder tree into the collection. A dry-run summary ("N
// files into folder X, M collisions") precedes the copy. Import is a
// collection operation (the trash is the safety net), not an editor undo step.
Dialog {
    id: importDialog
    objectName: "importDialog"

    property var appWindow

    title: qsTr("Import into collection")
    modal: true
    anchors.centerIn: parent
    width: 380
    standardButtons: Dialog.Close

    // Pending operation, resolved after the picker: "files" | "folder".
    property string pendingKind: ""
    property var pendingPaths: []
    property string pendingDir: ""

    function targetFolder() {
        return NoteListModel && NoteListModel.scope === "folder"
            ? NoteListModel.folderPath : ""
    }

    function openDialog() {
        open()
    }

    function showSummary() {
        var dry = pendingKind === "files"
            ? DocumentImporter.dryRunFiles(pendingPaths, targetFolder())
            : DocumentImporter.dryRunFolder(pendingDir, targetFolder())
        var msg = qsTr("Import ") + dry.files + qsTr(" file(s)")
        if (pendingKind === "folder" && dry.folders > 0)
            msg += qsTr(" in ") + dry.folders + qsTr(" folder(s)")
        var tf = targetFolder()
        msg += qsTr(" into ") + (tf === "" ? qsTr("the top level") : tf) + "."
        if (dry.collisions > 0)
            msg += "\n" + dry.collisions
                 + qsTr(" name collision(s) will be suffixed.")
        summaryLabel.text = msg
        summaryDialog.open()
    }

    function performImport() {
        var n = pendingKind === "files"
            ? DocumentImporter.importFiles(pendingPaths, targetFolder())
            : DocumentImporter.importFolder(pendingDir, targetFolder())
        if (appWindow)
            appWindow.showTransientStatus(
                n > 0 ? (qsTr("Imported ") + n + qsTr(" note(s)"))
                      : qsTr("Nothing imported"))
        importDialog.close()
    }

    contentItem: ColumnLayout {
        spacing: 10
        Label {
            text: qsTr("Destination: ")
                + (importDialog.targetFolder() === ""
                    ? qsTr("the top level")
                    : importDialog.targetFolder())
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Button {
            objectName: "importFilesButton"
            Layout.fillWidth: true
            text: qsTr("Choose files…")
            onClicked: importFileDialog.open()
        }
        Button {
            objectName: "importFolderButton"
            Layout.fillWidth: true
            text: qsTr("Choose folder…")
            onClicked: importFolderDialog.open()
        }
    }

    FileDialog {
        id: importFileDialog
        objectName: "importFileDialog"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Markdown/Text (*.md *.markdown *.txt)", "All files (*)"]
        onAccepted: {
            var paths = []
            for (var i = 0; i < selectedFiles.length; i++)
                paths.push(importDialog.appWindow.urlToLocalPath(selectedFiles[i]))
            importDialog.pendingKind = "files"
            importDialog.pendingPaths = paths
            importDialog.showSummary()
        }
    }

    FolderDialog {
        id: importFolderDialog
        objectName: "importFolderDialog"
        onAccepted: {
            importDialog.pendingKind = "folder"
            importDialog.pendingDir =
                importDialog.appWindow.urlToLocalPath(selectedFolder)
            importDialog.showSummary()
        }
    }

    // Dry-run confirmation.
    Dialog {
        id: summaryDialog
        objectName: "importSummaryDialog"
        modal: true
        title: qsTr("Confirm import")
        anchors.centerIn: parent
        width: 340
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: importDialog.performImport()
        contentItem: Label {
            id: summaryLabel
            objectName: "importSummaryLabel"
            wrapMode: Text.WordWrap
            padding: 8
        }
    }
}
