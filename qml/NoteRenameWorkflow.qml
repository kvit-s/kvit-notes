// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// Renaming or moving a note, and the wiki-links in other notes that point at
// it (features.md §3.3).
//
// A rename is planned before anything is written: the collection reports how
// many links in how many notes would have to change, and this is the only UI
// path that authorizes those edits. When the plan touches no links it applies
// straight away; otherwise the dialog asks, and "Rename only" is a real
// choice rather than a warning to click through. The open note is rewritten
// through DocumentManager as one undoable body replacement, so an unwanted
// rewrite costs one Ctrl+Z.
//
// Notes changed on disk since the plan was made are skipped rather than
// overwritten, and unwritable ones are reported; the second dialog lists both
// and can retry against a fresh plan.
Item {
    id: workflow

    // Wired by main.qml.
    property var appWindow

    // The plan awaiting the dialog's answer, and the callback the folder tree
    // passes so it can follow the rename it asked for.
    property var pendingPlan: null
    property var pendingAfterApply: null

    function requestNoteRename(relPath, newTitle) {
        workflow.beginRenamePlan(
            NoteCollection.planNoteRename(relPath, newTitle), null)
    }

    function requestNoteMove(relPath, targetFolder) {
        workflow.beginRenamePlan(
            NoteCollection.planNoteMove(relPath, targetFolder), null)
    }

    function requestFolderRename(relPath, newName, afterApply) {
        workflow.beginRenamePlan(
            NoteCollection.planFolderRename(relPath, newName), afterApply)
    }

    function beginRenamePlan(plan, afterApply) {
        if (!plan || !plan.ok)
            return
        workflow.pendingPlan = plan
        workflow.pendingAfterApply = afterApply
        if (plan.linkCount > 0)
            renameLinksDialog.open()
        else
            workflow.finishRenamePlan(false)
    }

    function executeRenamePlan(planId, updateLinks) {
        DocumentManager.flushPendingEdits()
        var openRelPath = workflow.appWindow.currentNoteRelPath
        var openBody = openRelPath !== ""
            ? DocumentSerializer.serialize(BlockModel) : ""
        var wasDirty = DocumentManager.isDirty
        var result = NoteCollection.applyRenamePlan(
            planId, updateLinks, openRelPath, openBody)
        if (!result.ok)
            return result
        if (result.openRewriteCount > 0
                && DocumentManager.restoreBody(result.openBody)
                && !wasDirty)
            DocumentManager.save()
        return result
    }

    function finishRenamePlan(updateLinks) {
        var plan = workflow.pendingPlan
        var after = workflow.pendingAfterApply
        if (!plan)
            return
        var result = workflow.executeRenamePlan(plan.id, updateLinks)
        workflow.pendingPlan = null
        workflow.pendingAfterApply = null
        if (result && result.ok && after)
            after(result)
        if (result && ((result.skipped && result.skipped.length > 0)
                       || (result.failed && result.failed.length > 0))) {
            rewriteResultDialog.planId = plan.id
            rewriteResultDialog.skipped = result.skipped
            rewriteResultDialog.failed = result.failed
            rewriteResultDialog.open()
        }
    }

    // The authorization step: how many links in how many notes, and the
    // three answers. It does not close on an outside click, because
    // dismissing it would leave a plan the collection is still holding.
    Dialog {
        id: renameLinksDialog
        objectName: "renameLinksDialog"
        title: qsTr("Update wiki-links?")
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: 440

        contentItem: Label {
            width: 390
            padding: 18
            wrapMode: Text.WordWrap
            text: workflow.pendingPlan
                ? qsTr("Update %1 links in %2 notes?")
                    .arg(workflow.pendingPlan.linkCount)
                    .arg(workflow.pendingPlan.noteCount)
                : ""
        }
        footer: DialogButtonBox {
            Button {
                objectName: "renameUpdateLinksButton"
                text: qsTr("Update links")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    renameLinksDialog.close()
                    workflow.finishRenamePlan(true)
                }
            }
            Button {
                objectName: "renameOnlyButton"
                text: qsTr("Rename only")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: {
                    renameLinksDialog.close()
                    workflow.finishRenamePlan(false)
                }
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: {
                    if (workflow.pendingPlan)
                        NoteCollection.cancelRenamePlan(workflow.pendingPlan.id)
                    workflow.pendingPlan = null
                    workflow.pendingAfterApply = null
                    renameLinksDialog.close()
                }
            }
        }
    }

    Dialog {
        id: rewriteResultDialog
        objectName: "rewriteResultDialog"
        title: qsTr("Some links were not updated")
        modal: true
        anchors.centerIn: parent
        width: 460
        property string planId: ""
        property var skipped: []
        property var failed: []

        contentItem: Label {
            width: 420
            padding: 18
            wrapMode: Text.WordWrap
            text: {
                var lines = []
                if (rewriteResultDialog.skipped.length > 0)
                    lines.push(qsTr("Changed externally (skipped):\n%1")
                               .arg(rewriteResultDialog.skipped.join("\n")))
                if (rewriteResultDialog.failed.length > 0)
                    lines.push(qsTr("Could not write:\n%1")
                               .arg(rewriteResultDialog.failed.join("\n")))
                return lines.join("\n\n")
            }
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Retry")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    var result = workflow.executeRenamePlan(
                        rewriteResultDialog.planId, true)
                    if (result && result.ok
                            && result.skipped.length === 0
                            && result.failed.length === 0)
                        rewriteResultDialog.close()
                    else if (result) {
                        rewriteResultDialog.skipped = result.skipped || []
                        rewriteResultDialog.failed = result.failed || []
                    }
                }
            }
            Button {
                text: qsTr("Close")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: rewriteResultDialog.close()
            }
        }
    }
}
