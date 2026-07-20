// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// Template management dialog (features.md §18): CRUD over
// the .kvit/templates/ markdown files, plus "save current note as template".
// The pick-on-create flow lives in the toolbar's Templates menu; this dialog
// edits the templates themselves. All state is in NoteTemplates (files on
// disk); this dialog reads and forwards.
Dialog {
    id: dlg
    objectName: "templateDialog"

    property var appWindow

    title: qsTr("Manage templates")
    modal: true
    anchors.centerIn: parent
    width: 580
    height: 440
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
        spacing: 10

        // Template list + new/delete.
        ColumnLayout {
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            spacing: 6

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
            spacing: 6

            Label {
                text: dlg.selected !== ""
                    ? qsTr("Editing: ") + dlg.selected
                    : qsTr("No template selected")
                font.bold: true
            }
            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 2
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
    Dialog {
        id: saveNoteDialog
        objectName: "saveNoteAsTemplateDialog"
        modal: true
        title: qsTr("Save note as template")
        anchors.centerIn: parent
        width: 320
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: saveNoteName.text = dlg.appWindow
            ? noteCollection.noteInfo(dlg.appWindow.currentNoteRelPath).title : ""
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
