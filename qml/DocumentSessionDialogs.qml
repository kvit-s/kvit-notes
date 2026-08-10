// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// Opening, starting and closing a document, and every question that has to be
// asked first (features.md §12.2, §12.6).
//
// Each of these transitions replaces the block model, which holds the only
// copy of the current document's unsaved content and undo history. So each
// one asks before it proceeds, and treats a cancelled Save-As or a failed
// write as a reason to stay where it is rather than a step to skip: the
// discard path is reached only when the user chooses it. A document that has
// never been saved has no file to fall back on and no recovery journal, which
// is why quitting with one open asks rather than closing.
//
// The error dialog is here because a failed save or open is the same
// conversation continued; the collection reports its own failures through it
// as well.
Item {
    id: dialogs

    // Wired by main.qml.
    property var appWindow

    // Importing is the collection's business, so the choice offered on Ctrl+O
    // is passed back to the window that owns the import dialog.
    signal importRequested()

    // The window's own close handler asks for this when a never-saved
    // document is still dirty on the way out.
    function confirmCloseUnsaved() { closeConfirmDialog.open() }

    // The status bar's create-a-vault line (§12.6): single-file mode, one
    // click from making this file's folder a vault.
    function offerVaultFromCurrentFolder() { createVaultDialog.open() }

    // A message the user has to see. The collection's failures arrive here
    // too, so there is one error dialog rather than two.
    function showError(message) {
        errorDialog.errorMessage = message
        errorDialog.open()
    }

    // The recovery banner's Restore, clicked while the note it names is open
    // and holds unsaved edits. Both versions are real work, so the choice is
    // the user's.
    function confirmRecoveryOverwrite(relPath) {
        recoveryOverwriteDialog.relPath = relPath
        recoveryOverwriteDialog.open()
    }

    // Ctrl+N and Ctrl+O decide in AppShortcuts.qml and call in here only on
    // the branch that has a question to ask, which is what keeps this whole
    // component off the startup path. The keys themselves must NOT live here:
    // main.qml builds this on first use, so a Shortcut inside it does not
    // exist until something else has already opened a dialog, and the key
    // does nothing until then.
    function confirmNewWithUnsavedChanges() {
        unsavedChangesBeforeNewDialog.open()
    }

    function confirmOpenWithUnsavedChanges() {
        unsavedChangesBeforeOpenDialog.open()
    }

    function confirmVaultSwitch(path) {
        vaultSwitchDialog.rootPath = path
        vaultSwitchDialog.open()
    }

    function chooseOpenOrImport() { openOrImportChoiceDialog.open() }

    KvitDialog {
        id: recoveryOverwriteDialog
        objectName: "recoveryOverwriteDialog"
        modal: true
        title: qsTr("Recovered version")
        anchors.centerIn: parent
        width: Interface.px(400)
        closePolicy: Popup.CloseOnEscape
        property string relPath: ""

        contentItem: Label {
            text: qsTr("This note has unsaved changes that are newer than the "
                + "recovered version. Using the recovered version replaces "
                + "them; a copy of the current version is kept as a backup.")
            wrapMode: Text.WordWrap
            padding: Interface.px(12)
        }

        footer: DialogButtonBox {
            Button {
                objectName: "recoveryCancelButton"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: recoveryOverwriteDialog.close()
            }
            Button {
                objectName: "recoveryKeepEditsButton"
                text: qsTr("Keep my changes")
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: {
                    var path = recoveryOverwriteDialog.relPath
                    recoveryOverwriteDialog.close()
                    dialogs.appWindow.keepEditsOverRecovery(path)
                }
            }
            Button {
                objectName: "recoveryReplaceButton"
                text: qsTr("Use recovered version")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: {
                    var path = recoveryOverwriteDialog.relPath
                    recoveryOverwriteDialog.close()
                    dialogs.appWindow.replaceEditsWithRecovery(path)
                }
            }
        }
    }

    // Ctrl+O in collection mode: open a file standalone, or import it into the
    // collection (§12.6).
    KvitDialog {
        id: openOrImportChoiceDialog
        objectName: "openOrImportChoiceDialog"
        modal: true
        title: qsTr("Open a file")
        anchors.centerIn: parent
        width: Interface.px(320)
        contentItem: Label {
            text: qsTr("Open the file on its own, or import it into your "
                + "collection?")
            wrapMode: Text.WordWrap
            padding: Interface.px(10)
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Open standalone")
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: {
                    openOrImportChoiceDialog.close()
                    dialogs.appWindow.openFileFromDialog()
                }
            }
            Button {
                objectName: "chooseImportButton"
                text: qsTr("Import…")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    openOrImportChoiceDialog.close()
                    dialogs.importRequested()
                }
            }
        }
    }

    // Single-file mode → vault upgrade: confirms, then opens the current
    // file's folder as the collection root. The open file stays open and
    // becomes a note of the new vault.
    KvitDialog {
        id: createVaultDialog
        objectName: "createVaultDialog"
        modal: true
        title: qsTr("Create a vault")
        anchors.centerIn: parent
        width: Interface.px(360)
        contentItem: Label {
            text: qsTr("Use this file's folder as a vault? The sidebar, "
                + "tags, global search, and wiki links will then work "
                + "across every markdown file in it. The folder's files "
                + "are left exactly as they are.")
            wrapMode: Text.WordWrap
            padding: Interface.px(10)
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: createVaultDialog.close()
            }
            Button {
                objectName: "createVaultConfirmButton"
                text: qsTr("Create vault")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    createVaultDialog.close()
                    var dir = dialogs.appWindow.currentNoteDir()
                    // Through AppActions rather than straight to the
                    // collection: the application's openVaultRoot() releases
                    // the outgoing vault's search index first, and opening the
                    // root directly leaves a switch waiting on the old vault's
                    // worker queue on the GUI thread.
                    if (dir !== "")
                        AppActions.requestOpenVault(dir)
                }
            }
        }
    }

    // Quitting with a document that has never been saved. Closing used to
    // discard it silently: there is no file to fall back on and the recovery
    // journal only covers crashes, so this was an unrecoverable loss on an
    // ordinary quit.
    KvitDialog {
        id: closeConfirmDialog
        title: "Unsaved Changes"
        modal: true
        anchors.centerIn: parent

        contentItem: Item {
            implicitWidth: Interface.px(360)
            implicitHeight: closeDialogText.implicitHeight + 40

            Text {
                id: closeDialogText
                anchors.fill: parent
                anchors.margins: Interface.px(20)
                text: "This document has never been saved. Save it before closing?"
                wrapMode: Text.WordWrap
            }
        }

        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel

        onAccepted: {
            // Only close once the document is actually on disk. If the Save-As
            // dialog is cancelled or the write fails, stay open.
            if (DocumentManager.saveFileDialog())
                dialogs.appWindow.close()
            else
                dialogs.appWindow.forceActualClose = false
        }
        // Discard is the user deciding to lose it, which is their call to make.
        onDiscarded: {
            DocumentManager.newDocument()
            dialogs.appWindow.close()
        }
        // Cancel: nothing happens, the window stays open.
        onRejected: dialogs.appWindow.forceActualClose = false
    }

    // Unsaved changes dialog before opening a new file
    KvitDialog {
        id: unsavedChangesBeforeOpenDialog
        title: "Unsaved Changes"
        modal: true
        anchors.centerIn: parent

        contentItem: Item {
            implicitWidth: Interface.px(360)
            implicitHeight: openDialogText.implicitHeight + 40

            Text {
                id: openDialogText
                anchors.fill: parent
                anchors.margins: Interface.px(20)
                text: "You have unsaved changes. Do you want to save them before opening another file?"
                wrapMode: Text.WordWrap
            }
        }

        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel

        onAccepted: {
            // Save button clicked. Opening another document replaces the model,
            // so it may only proceed once this one is genuinely on disk. A
            // cancelled Save-As and a failed write both mean it is not, and in
            // both cases the right thing is to leave the document alone rather
            // than continue into an action that discards it.
            var saved = DocumentManager.hasFile
                        ? DocumentManager.save()
                        : DocumentManager.saveFileDialog()
            if (!saved)
                return
            dialogs.appWindow.openFileFromDialog()
        }

        onDiscarded: {
            // Discard button clicked - open without saving
            dialogs.appWindow.openFileFromDialog()
        }

        // Cancel - do nothing
    }

    KvitDialog {
        id: vaultSwitchDialog
        objectName: "vaultSwitchDialog"
        title: qsTr("Unsaved Changes")
        modal: true
        anchors.centerIn: parent
        property string rootPath: ""

        contentItem: Label {
            width: Interface.px(360)
            padding: Interface.px(20)
            wrapMode: Text.WordWrap
            text: qsTr("This note has unsaved changes. Save them before switching folders?")
        }
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        onAccepted: {
            var saved = DocumentManager.hasFile
                ? DocumentManager.save() : DocumentManager.saveFileDialog()
            if (saved)
                AppActions.confirmOpenVault(rootPath)
        }
        onDiscarded: {
            DocumentManager.newDocument()
            AppActions.confirmOpenVault(rootPath)
        }
    }

    // Unsaved changes dialog before creating new document
    KvitDialog {
        id: unsavedChangesBeforeNewDialog
        title: "Unsaved Changes"
        modal: true
        anchors.centerIn: parent

        contentItem: Item {
            implicitWidth: Interface.px(360)
            implicitHeight: newDialogText.implicitHeight + 40

            Text {
                id: newDialogText
                anchors.fill: parent
                anchors.margins: Interface.px(20)
                text: "You have unsaved changes. Do you want to save them before creating a new document?"
                wrapMode: Text.WordWrap
            }
        }

        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel

        onAccepted: {
            // Save button clicked. Same rule as the Open confirmation: starting
            // a new document throws this one away, so it has to be safely
            // stored first.
            var saved = DocumentManager.hasFile
                        ? DocumentManager.save()
                        : DocumentManager.saveFileDialog()
            if (!saved)
                return
            DocumentManager.newDocument()
        }

        onDiscarded: {
            // Discard button clicked - create new without saving
            DocumentManager.newDocument()
        }

        // Cancel - do nothing
    }

    KvitDialog {
        id: errorDialog
        objectName: "errorDialog"
        title: "Error"
        modal: true
        anchors.centerIn: parent
        property string errorMessage: ""

        contentItem: Item {
            implicitWidth: Interface.px(360)
            implicitHeight: errorDialogText.implicitHeight + 40

            Text {
                id: errorDialogText
                anchors.fill: parent
                anchors.margins: Interface.px(20)
                text: errorDialog.errorMessage
                wrapMode: Text.WordWrap
            }
        }

        standardButtons: Dialog.Ok
    }

    // Handle user-facing save/open results. Repository indexing, backup
    // rotation, watcher guards, and metadata synchronization are wired by the
    // C++ application composition rather than ordered here.
    Connections {
        target: DocumentManager

        function onSaveFailed(error) {
            errorDialog.errorMessage = "Failed to save: " + error
            errorDialog.open()
        }

        function onOpenFailed(error) {
            errorDialog.errorMessage = "Failed to open: " + error
            errorDialog.open()
        }

        // Oversized-file guard: the file was refused before any read;
        // show the placeholder with an "Open anyway".
        function onOpenRejectedTooLarge(filePath, sizeBytes, capBytes) {
            dialogs.appWindow.oversizedFilePath = filePath
            dialogs.appWindow.oversizedFileBytes = sizeBytes
            dialogs.appWindow.oversizedFileCap = capBytes
        }
        function onOpenSucceeded(filePath) {
            dialogs.appWindow.oversizedFilePath = ""
        }
    }
}
