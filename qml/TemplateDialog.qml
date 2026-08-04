// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The ListView delegate is its own component scope, so its reads of
// `templateList` and `dlg` are unqualified access. Binding the enclosing
// component's ids into the delegate resolves them, and is safe because the
// delegate already declares `required property string modelData` rather than
// relying on injection.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// Template management dialog (features.md §18): CRUD over
// the .kvit/templates/ markdown files, plus "save current note as template".
// The pick-on-create flow lives in the toolbar's Templates menu; this dialog
// edits the templates themselves. All state is in NoteTemplates (files on
// disk); this dialog reads and forwards.
KvitDialog {
    id: dlg
    objectName: "templateDialog"

    property var appWindow

    title: qsTr("Manage templates")
    modal: true
    anchors.centerIn: parent
    width: Interface.px(580)
    height: Interface.px(440)
    standardButtons: Dialog.Close

    property string selected: ""

    function openManage() {
        NoteTemplates.seedBuiltinsIfEmpty()
        var names = NoteTemplates.templateNames()
        selected = names.length > 0 ? names[0] : ""
        loadSelected()
        open()
    }

    function loadSelected() {
        editor.text = selected !== "" ? NoteTemplates.readTemplate(selected) : ""
    }

    onSelectedChanged: loadSelected()

    contentItem: RowLayout {
        spacing: Interface.px(10)

        // Template list + new/delete.
        ColumnLayout {
            Layout.preferredWidth: Interface.px(180)
            Layout.fillHeight: true
            spacing: Interface.px(6)

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 0
                ListView {
                    id: templateList
                    objectName: "templateList"
                    anchors.fill: parent
                    clip: true
                    // Re-read the names on any template-set change.
                    model: {
                        var r = NoteTemplates.revision  // dependency only
                        return NoteTemplates.templateNames()
                    }
                    delegate: ItemDelegate {
                        required property string modelData
                        width: templateList.width
                        text: modelData
                        highlighted: modelData === dlg.selected
                        onClicked: dlg.selected = modelData
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: newNameField
                    objectName: "newTemplateNameField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("New template name")
                }
                Button {
                    text: qsTr("Add")
                    enabled: newNameField.text.trim().length > 0
                    onClicked: {
                        if (NoteTemplates.writeTemplate(
                                newNameField.text.trim(),
                                "# {{title}}\n\n")) {
                            dlg.selected = newNameField.text.trim()
                            newNameField.text = ""
                        }
                    }
                }
            }
            Button {
                objectName: "deleteTemplateButton"
                Layout.fillWidth: true
                text: qsTr("Delete selected")
                enabled: dlg.selected !== ""
                onClicked: {
                    NoteTemplates.deleteTemplate(dlg.selected)
                    var names = NoteTemplates.templateNames()
                    dlg.selected = names.length > 0 ? names[0] : ""
                }
            }
            Button {
                objectName: "saveNoteAsTemplateButton"
                Layout.fillWidth: true
                text: qsTr("Save current note…")
                enabled: dlg.appWindow && dlg.appWindow.collectionOpen
                    && dlg.appWindow.currentNoteRelPath !== ""
                onClicked: saveNoteDialog.open()
            }
        }

        // Content editor.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Interface.px(6)

            Label {
                text: dlg.selected !== ""
                    ? qsTr("Editing: ") + dlg.selected
                    : qsTr("No template selected")
                font.bold: true
            }
            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: Interface.px(2)
                ScrollView {
                    anchors.fill: parent
                    TextArea {
                        id: editor
                        objectName: "templateEditor"
                        enabled: dlg.selected !== ""
                        wrapMode: TextArea.Wrap
                        font.family: Typography.monoFamily
                        placeholderText: qsTr("Template markdown — use {{date}}, "
                            + "{{time}}, {{title}}, {{date:FORMAT}}")
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "saveTemplateButton"
                    text: qsTr("Save template")
                    enabled: dlg.selected !== ""
                    onClicked: NoteTemplates.writeTemplate(dlg.selected, editor.text)
                }
            }
        }
    }

    // "Save current note as template" name prompt.
    KvitDialog {
        id: saveNoteDialog
        // Opens on the field it is about, so a screen reader
        // announces something to type into and a keyboard user
        // does not have to guess how many tabs reach it.
        initialFocusItem: saveNoteName
        objectName: "saveNoteAsTemplateDialog"
        // Parented explicitly to the item the outer dialog is parented
        // to, rather than being left to default into it. A Popup declared
        // inside a dialog that both derives from a composite type and
        // assigns its own `contentItem` lands on the content item the base
        // created and the derived assignment then replaced — an orphan with
        // no window — and `open()` on a popup with no window does nothing at
        // all, silently. See the note in KvitDialog.qml.
        parent: dlg.parent
        modal: true
        title: qsTr("Save note as template")
        anchors.centerIn: parent
        width: Interface.px(320)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: saveNoteName.text = dlg.appWindow
            ? NoteCollection.noteInfo(dlg.appWindow.currentNoteRelPath).title : ""
        onAccepted: {
            if (dlg.appWindow
                && dlg.appWindow.saveCurrentNoteAsTemplate(saveNoteName.text.trim()))
                dlg.selected = saveNoteName.text.trim()
        }
        contentItem: ColumnLayout {
            Label { text: qsTr("Template name:") }
            TextField {
                id: saveNoteName
                objectName: "saveNoteTemplateNameField"
                Layout.fillWidth: true
            }
        }
    }
}
