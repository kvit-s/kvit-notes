// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Export dialog (features.md §12.5): choose a format
// (Markdown, HTML, PDF, plain text) and a scope (the open note, the note-list
// selection, or the whole collection), then a destination. The actual export
// runs through documentExporter, which builds one self-contained HTML string
// and prints PDF from it.
Dialog {
    id: exportDialog
    objectName: "exportDialog"

    property var appWindow

    title: qsTr("Export")
    modal: true
    anchors.centerIn: parent
    width: 380
    standardButtons: Dialog.Cancel

    // "markdown" | "html" | "pdf" | "text"
    readonly property var formats: ["markdown", "html", "pdf", "text"]
    readonly property var formatLabels: ["Markdown (.md)", "HTML (.html)",
                                         "PDF (.pdf)", "Plain text (.txt)"]
    property int formatIndex: 1  // HTML by default
    readonly property string format: formats[formatIndex]

    // "note" | "selection" | "collection"
    property string scope: "note"

    function openDialog() {
        // Default scope: the open note; collection/selection only when a
        // collection is open.
        scope = "note"
        open()
    }

    function selectedPaths() {
        return appWindow && appWindow.noteListSelectedPaths
            ? appWindow.noteListSelectedPaths() : []
    }

    function currentTitle() {
        if (!appWindow || appWindow.currentNoteRelPath === "")
            return "Document"
        return noteCollection.noteInfo(appWindow.currentNoteRelPath).title
    }

    function prepareContext() {
        var noteDir = ""
        var root = noteCollection.isOpen ? noteCollection.rootPath : ""
        if (appWindow && appWindow.currentNoteRelPath !== "") {
            var abs = noteCollection.absolutePath(appWindow.currentNoteRelPath)
            noteDir = abs.substring(0, abs.lastIndexOf("/"))
        }
        // The single-note scope renders the live model directly. A collection
        // or selection export instead reads each note from disk, where the
        // note being edited may be out of date, so hand the exporter the
        // editor's current markdown for that one note. Exporting snapshots
        // rather than saving: it must not write to the user's notes.
        if (appWindow && appWindow.currentNoteRelPath !== "")
            documentExporter.setLiveNote(appWindow.currentNoteRelPath, blockModel)
        else
            documentExporter.clearLiveNote()
        documentExporter.setImageContext(noteDir, root)
    }

    // Kick off the correct destination picker for the chosen scope.
    function runExport() {
        prepareContext()
        if (scope === "note") {
            saveFileDialog.open()
        } else {
            destFolderDialog.open()
        }
    }

    contentItem: ColumnLayout {
        spacing: 10

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
            spacing: 4
            RadioButton {
                objectName: "exportScopeNote"
                text: qsTr("This note")
                checked: exportDialog.scope === "note"
                onClicked: exportDialog.scope = "note"
            }
            RadioButton {
                objectName: "exportScopeSelection"
                text: qsTr("Selected notes (%1)").arg(
                          exportDialog.selectedPaths().length)
                visible: exportDialog.appWindow
                    && exportDialog.appWindow.collectionOpen
                    && exportDialog.selectedPaths().length > 0
                checked: exportDialog.scope === "selection"
                onClicked: exportDialog.scope = "selection"
            }
            RadioButton {
                objectName: "exportScopeCollection"
                text: qsTr("Whole collection")
                visible: exportDialog.appWindow
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
        fileMode: FileDialog.SaveFile
        defaultSuffix: documentExporter.extensionFor(exportDialog.format)
        onAccepted: {
            var path = exportDialog.appWindow.urlToLocalPath(selectedFile)
            var ok = documentExporter.writeModel(
                blockModel, exportDialog.currentTitle(),
                exportDialog.format, path)
            exportDialog.appWindow.showTransientStatus(
                ok ? qsTr("Exported to ") + path
                   : qsTr("Export failed"))
            exportDialog.close()
        }
    }

    // Multi-note / collection destination directory.
    FolderDialog {
        id: destFolderDialog
        objectName: "exportFolderDialog"
        onAccepted: {
            var dir = exportDialog.appWindow.urlToLocalPath(selectedFolder)
            var count = 0
            if (exportDialog.scope === "selection")
                count = documentExporter.exportNotes(
                    noteCollection, exportDialog.selectedPaths(), dir,
                    exportDialog.format, singleFileCheck.checked)
            else
                count = documentExporter.exportCollection(
                    noteCollection, dir, exportDialog.format,
                    singleFileCheck.checked)
            documentExporter.clearLiveNote()
            exportDialog.appWindow.showTransientStatus(
                count > 0 ? (qsTr("Exported ") + count + qsTr(" notes to ") + dir)
                          : qsTr("Export failed"))
            exportDialog.close()
        }
    }
}
