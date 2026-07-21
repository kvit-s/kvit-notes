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

    // A run in progress: the files still to copy, how far it has got, how
    // many landed, how many the importer declined, and whether the reader
    // asked to stop.
    property var importQueue: []
    property int importedCount: 0
    property int skippedCount: 0
    property int progressDone: 0
    property int importTotal: 0
    property bool importCancelled: false

    // How many files each turn of the event loop imports, for a FILE LIST.
    //
    // Not one: DocumentImporter.importFiles() rescans the collection once per
    // call, so a file at a time would rescan the whole vault for every file
    // imported. Not all of them either, which is the shape that froze the
    // window. Twenty-five is the compromise — the window repaints and Cancel
    // becomes pressable about that often, and a five-hundred-file import
    // rescans twenty times rather than five hundred.
    readonly property int importChunk: 25

    // A file list is driven in chunks from here; a folder is handed to the
    // importer's own asynchronous run.
    //
    // The two are not the same shape on purpose. A folder import mirrors each
    // source subdirectory under the target, and importFiles() puts every file
    // it is given directly in the target, so driving a folder as a flat list
    // of paths would quietly flatten the tree. startImportFolder() keeps the
    // structure, enumerates once, and yields between short bursts of copying.
    function performImport() {
        // A second start while a run is in progress is ignored by the
        // importer, and importFinished would then never arrive for this call,
        // leaving the progress dialog up with nothing behind it.
        if (DocumentImporter.importInProgress) {
            if (appWindow)
                appWindow.showTransientStatus(
                    qsTr("An import is already running."))
            return
        }
        importDialog.importedCount = 0
        importDialog.skippedCount = 0
        importDialog.progressDone = 0
        importDialog.importCancelled = false
        progressLabel.text = qsTr("Preparing…")

        if (pendingKind === "folder") {
            importDialog.importQueue = []
            // Sizes the progress bar before the first file lands, so the bar
            // does not jump from empty to full on a small import.
            importDialog.importTotal =
                DocumentImporter.listImportableFiles(pendingDir).length
            progressDialog.open()
            DocumentImporter.startImportFolder(pendingDir, targetFolder())
            return
        }

        importDialog.importQueue = pendingPaths.slice()
        importDialog.importTotal = pendingPaths.length
        progressDialog.open()
        importStep.start()
    }

    // The single end of a run, whichever flow produced it.
    function reportImport(imported, skipped, cancelled) {
        importStep.stop()
        progressDialog.close()
        var message
        if (cancelled)
            message = qsTr("Import stopped after %1 note(s)").arg(imported)
        else if (imported > 0)
            message = qsTr("Imported %1 note(s)").arg(imported)
        else
            message = qsTr("Nothing imported")
        // Files declined as too large or unreadable are part of the outcome:
        // silently importing fewer notes than were chosen is the thing the
        // reader would otherwise discover much later.
        if (skipped > 0)
            message += qsTr(" (%1 skipped)").arg(skipped)
        if (appWindow)
            appWindow.showTransientStatus(message)
        importDialog.close()
    }

    Timer {
        id: importStep
        objectName: "importStepTimer"
        interval: 0
        repeat: false
        onTriggered: {
            if (importDialog.importCancelled
                    || importDialog.importQueue.length === 0) {
                importDialog.reportImport(importDialog.importedCount,
                                          importDialog.skippedCount,
                                          importDialog.importCancelled)
                return
            }
            var batch = importDialog.importQueue.splice(
                0, importDialog.importChunk)
            importDialog.importedCount +=
                DocumentImporter.importFiles(batch, importDialog.targetFolder())
            // lastSkippedCount describes the CALL, and each chunk is its own
            // call, so the run's total is accumulated here.
            importDialog.skippedCount += DocumentImporter.lastSkippedCount
            importDialog.progressDone =
                importDialog.importTotal - importDialog.importQueue.length
            progressLabel.text = qsTr("Importing %1 of %2")
                .arg(importDialog.progressDone).arg(importDialog.importTotal)
            importStep.start()
        }
    }

    Connections {
        target: DocumentImporter

        // importProgress is emitted by both flows, but only the folder run's
        // totals describe the whole import: the chunked one reports per call,
        // and the loop above already keeps the running count.
        function onImportProgress(done, total) {
            if (importDialog.pendingKind !== "folder")
                return
            importDialog.progressDone = done
            importDialog.importTotal = total
            progressLabel.text = qsTr("Importing %1 of %2")
                .arg(done).arg(total)
        }

        // Emitted once per startImportFolder() run, after its single
        // end-of-run collection refresh.
        function onImportFinished(imported, skipped, cancelled) {
            importDialog.reportImport(imported, skipped, cancelled)
        }
    }

    Dialog {
        id: progressDialog
        objectName: "importProgressDialog"
        modal: true
        closePolicy: Popup.NoAutoClose
        title: qsTr("Importing")
        anchors.centerIn: parent
        width: 340
        standardButtons: Dialog.Cancel
        onRejected: {
            importDialog.importCancelled = true
            DocumentImporter.requestCancel()
        }

        contentItem: ColumnLayout {
            spacing: 8
            Label {
                id: progressLabel
                objectName: "importProgressLabel"
                text: qsTr("Preparing…")
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            ProgressBar {
                objectName: "importProgressBar"
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, importDialog.importTotal)
                value: importDialog.progressDone
            }
        }
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
                paths.push(DocumentManager.toLocalPath(selectedFiles[i]))
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
                DocumentManager.toLocalPath(selectedFolder)
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
