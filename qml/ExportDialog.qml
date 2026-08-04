// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Kvit 1.0

// Export dialog (features.md §12.5): choose a format
// (Markdown, HTML, PDF, plain text) and a scope (the open note, the note-list
// selection, or the whole collection), then a destination. The actual export
// runs through DocumentExporter, which builds one self-contained HTML string
// and prints PDF from it.
KvitDialog {
    id: exportDialog
    objectName: "exportDialog"

    property var appWindow

    title: qsTr("Export")
    modal: true
    anchors.centerIn: parent
    width: Interface.px(380)
    standardButtons: Dialog.Cancel

    // "markdown" | "html" | "pdf" | "text"
    readonly property var formats: ["markdown", "html", "pdf", "text"]
    readonly property var formatLabels: ["Markdown (.md)", "HTML (.html)",
                                         "PDF (.pdf)", "Plain text (.txt)"]
    property int formatIndex: 1  // HTML by default
    readonly property string format: formats[formatIndex]

    // "note" | "selection" | "collection" | "blocks"
    property string scope: "note"
    // The dialog is modal, so the selected indexes cannot be moved by editor
    // input while a destination is being chosen. blockMarkdown is the menu's
    // structural snapshot for display/tests; rich formats render from the
    // model so document-derived blocks retain their full-note context.
    property string blockMarkdown: ""
    property int blockCount: 0
    property var blockIndexes: []

    function openDialog() {
        // Default scope: the open note; collection/selection only when a
        // collection is open.
        blockMarkdown = ""
        blockCount = 0
        blockIndexes = []
        scope = "note"
        open()
    }

    function openForBlocks(indexes) {
        if (!indexes || indexes.length === 0)
            return
        DocumentManager.flushPendingEdits()
        blockMarkdown = DocumentSerializer.serializeBlocks(BlockModel, indexes)
        blockCount = indexes.length
        blockIndexes = indexes
        scope = "blocks"
        open()
    }

    function selectedPaths() {
        return appWindow && appWindow.noteListSelectedPaths
            ? appWindow.noteListSelectedPaths() : []
    }

    function currentTitle() {
        if (!appWindow || appWindow.currentNoteRelPath === "")
            return "Document"
        return NoteCollection.noteInfo(appWindow.currentNoteRelPath).title
    }

    function prepareContext() {
        DocumentManager.flushPendingEdits()
        // currentNoteRelPath is empty for a loose file, but relative media is
        // still relative to that file's folder. currentNoteDir handles both
        // loose files and notes inside a collection.
        var noteDir = appWindow && appWindow.currentNoteDir
            ? appWindow.currentNoteDir() : ""
        var root = NoteCollection.isOpen ? NoteCollection.rootPath : ""
        // The single-note scope renders the live model directly. A collection
        // or selection export instead reads each note from disk, where the
        // note being edited may be out of date, so hand the exporter the
        // editor's current markdown for that one note. Exporting snapshots
        // rather than saving: it must not write to the user's notes.
        if (appWindow && appWindow.currentNoteRelPath !== "")
            DocumentExporter.setLiveNote(appWindow.currentNoteRelPath, BlockModel)
        else
            DocumentExporter.clearLiveNote()
        DocumentExporter.setImageContext(noteDir, root)
    }

    // Kick off the correct destination picker for the chosen scope.
    function runExport() {
        prepareContext()
        if (scope === "note" || scope === "blocks") {
            saveFileDialog.open()
        } else {
            destFolderDialog.open()
        }
    }

    contentItem: ColumnLayout {
        spacing: Interface.px(10)

        Label { text: qsTr("Format"); font.bold: true }
        ComboBox {
            id: formatCombo
            objectName: "exportFormatCombo"
            Layout.fillWidth: true
            model: exportDialog.formatLabels
            currentIndex: exportDialog.formatIndex
            onActivated: exportDialog.formatIndex = currentIndex
        }

        Label { text: qsTr("Scope"); font.bold: true }
        Column {
            spacing: Interface.px(4)
            RadioButton {
                objectName: "exportScopeBlocks"
                text: exportDialog.blockCount === 1
                    ? qsTr("This block")
                    : qsTr("Selected blocks (%1)").arg(exportDialog.blockCount)
                visible: exportDialog.scope === "blocks"
                checked: visible
            }
            RadioButton {
                objectName: "exportScopeNote"
                text: qsTr("This note")
                visible: exportDialog.scope !== "blocks"
                checked: exportDialog.scope === "note"
                onClicked: exportDialog.scope = "note"
            }
            RadioButton {
                objectName: "exportScopeSelection"
                text: qsTr("Selected notes (%1)").arg(
                          exportDialog.selectedPaths().length)
                visible: exportDialog.appWindow
                    && exportDialog.scope !== "blocks"
                    && exportDialog.appWindow.collectionOpen
                    && exportDialog.selectedPaths().length > 0
                checked: exportDialog.scope === "selection"
                onClicked: exportDialog.scope = "selection"
            }
            RadioButton {
                objectName: "exportScopeCollection"
                text: qsTr("Whole collection")
                visible: exportDialog.appWindow
                    && exportDialog.scope !== "blocks"
                    && exportDialog.appWindow.collectionOpen
                checked: exportDialog.scope === "collection"
                onClicked: exportDialog.scope = "collection"
            }
        }

        CheckBox {
            id: singleFileCheck
            objectName: "exportSingleFileCheck"
            text: qsTr("Combine into a single file")
            visible: exportDialog.scope !== "note"
                && exportDialog.scope !== "blocks"
        }

        Button {
            objectName: "exportRunButton"
            Layout.fillWidth: true
            text: qsTr("Choose destination…")
            highlighted: true
            onClicked: exportDialog.runExport()
        }
    }

    // Single-note destination.
    FileDialog {
        id: saveFileDialog
        objectName: "exportSaveFileDialog"
        // Owned by the application window, and where the platform has no
        // file chooser of its own, shown inside it: the dialog Qt builds in
        // that case is a top-level window, and an unowned one never gives
        // the keyboard focus back on Wayland when it closes.
        parentWindow: exportDialog.appWindow
        popupType: Popup.Item
        fileMode: FileDialog.SaveFile
        defaultSuffix: DocumentExporter.extensionFor(exportDialog.format)
        onAccepted: {
            var path = DocumentManager.toLocalPath(selectedFile)
            var ok = exportDialog.scope === "blocks"
                ? DocumentExporter.writeModelBlocks(
                      BlockModel, exportDialog.blockIndexes,
                      exportDialog.currentTitle(), exportDialog.format, path)
                : DocumentExporter.writeModel(
                      BlockModel, exportDialog.currentTitle(),
                      exportDialog.format, path)
            exportDialog.appWindow.showTransientStatus(
                ok ? qsTr("Exported to ") + path
                   : qsTr("Export failed"))
            exportDialog.close()
        }
    }

    // Multi-note / collection destination directory. The export itself runs
    // as a job in DocumentExporter — one note per turn of the event loop —
    // because a whole vault of notes with images used to render, Base64-expand
    // and accumulate inside this handler, with no way to repaint, report
    // progress or stop.
    FolderDialog {
        id: destFolderDialog
        objectName: "exportFolderDialog"
        // As above: owned by the window, and inside it when Qt has to build
        // the dialog itself.
        parentWindow: exportDialog.appWindow
        popupType: Popup.Item
        onAccepted: {
            exportDialog.destination = DocumentManager.toLocalPath(selectedFolder)
            var started = exportDialog.scope === "selection"
                ? DocumentExporter.startExportNotes(
                      NoteCollection, exportDialog.selectedPaths(),
                      exportDialog.destination, exportDialog.format,
                      singleFileCheck.checked)
                : DocumentExporter.startExportCollection(
                      NoteCollection, exportDialog.destination,
                      exportDialog.format, singleFileCheck.checked)
            if (started)
                progressDialog.open()
        }
    }

    // Where the running job is writing, for the status message it ends with.
    property string destination: ""

    Connections {
        target: DocumentExporter

        // Refused before anything was written: an unsafe plan, or a scope too
        // large to combine. Nothing to undo, only something to say.
        function onExportRefused(reason) {
            progressDialog.close()
            DocumentExporter.clearLiveNote()
            if (exportDialog.appWindow)
                exportDialog.appWindow.showTransientStatus(reason)
        }

        function onExportProgress(done, total, relPath) {
            progressLabel.text = qsTr("Exporting %1 of %2: %3")
                .arg(done).arg(total).arg(relPath)
        }

        function onExportFinished(written, total, cancelled, error) {
            progressDialog.close()
            DocumentExporter.clearLiveNote()
            var message
            if (cancelled)
                message = qsTr("Export stopped after %1 of %2 notes")
                    .arg(written).arg(total)
            else if (error !== "" && written > 0)
                // Some notes landed and some did not. Reporting only the
                // error hides what was written; reporting only the count
                // hides what was lost.
                message = qsTr("Exported %1 of %2 notes to %3. %4")
                    .arg(written).arg(total).arg(exportDialog.destination)
                    .arg(error)
            else if (error !== "")
                message = error
            else if (written > 0)
                message = qsTr("Exported %1 notes to %2")
                    .arg(written).arg(exportDialog.destination)
            else
                message = qsTr("Export failed")
            if (exportDialog.appWindow)
                exportDialog.appWindow.showTransientStatus(message)
            exportDialog.close()
        }
    }

    KvitDialog {
        id: progressDialog
        objectName: "exportProgressDialog"
        // Parented explicitly to the item the outer dialog is parented
        // to, rather than being left to default into it. A Popup declared
        // inside a dialog that both derives from a composite type and
        // assigns its own `contentItem` lands on the content item the base
        // created and the derived assignment then replaced — an orphan with
        // no window — and `open()` on a popup with no window does nothing at
        // all, silently. See the note in KvitDialog.qml.
        parent: exportDialog.parent
        modal: true
        closePolicy: Popup.NoAutoClose
        title: qsTr("Exporting")
        anchors.centerIn: parent
        width: Interface.px(340)
        standardButtons: Dialog.Cancel
        onRejected: DocumentExporter.cancelExport()

        contentItem: ColumnLayout {
            spacing: Interface.px(8)
            Label {
                id: progressLabel
                objectName: "exportProgressLabel"
                text: qsTr("Preparing…")
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            ProgressBar {
                objectName: "exportProgressBar"
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, DocumentExporter.total)
                value: DocumentExporter.progress
            }
        }
    }
}
