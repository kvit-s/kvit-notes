// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Window

// Quick-capture window (features.md §15.1). A small always-on-top window the
// global hotkey (or the tray menu) opens for jotting a note without switching to
// the main window. Save writes the text as a new note in the collection through
// noteCollection.captureNote and reports it; Escape cancels. Reachable regardless
// of whether the OS delivers a system-wide hotkey.
Window {
    id: root
    objectName: "quickCaptureWindow"

    signal captured(string relPath)

    width: 460
    height: 240
    minimumWidth: 320
    minimumHeight: 160
    flags: Qt.Dialog
    title: qsTr("Quick capture")
    color: theme.windowBackground

    // Open centered and focused on the text field.
    function openCapture() {
        captureArea.text = ""
        root.show()
        root.raise()
        root.requestActivate()
        captureArea.forceActiveFocus()
    }

    function save() {
        var body = captureArea.text.trim()
        if (body === "") { root.close(); return }
        var rp = noteCollection.captureNote(body)
        if (rp !== "") {
            root.captured(rp)
            if (typeof a11y !== "undefined")
                a11y.announce(qsTr("Note captured"))
        }
        root.close()
    }

    Rectangle {
        anchors.fill: parent
        color: theme.windowBackground

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            Label {
                text: qsTr("Quick capture")
                font.pixelSize: 15
                font.bold: true
                color: theme.textPrimary
            }

            ScrollView {
                width: parent.width
                height: parent.height - 88
                clip: true
                TextArea {
                    id: captureArea
                    objectName: "quickCaptureText"
                    wrapMode: TextArea.Wrap
                    placeholderText: qsTr("Jot a note… (Ctrl+Enter to save, Esc to cancel)")
                    color: theme.textPrimary
                    background: Rectangle {
                        color: theme.panelBackground
                        border.color: captureArea.activeFocus ? theme.focusRing
                                                              : theme.border
                        border.width: captureArea.activeFocus ? 2 : 1
                        radius: 4
                    }
                    Keys.onPressed: function(event) {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                            && (event.modifiers & Qt.ControlModifier)) {
                            root.save()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape) {
                            root.close()
                            event.accepted = true
                        }
                    }
                }
            }

            Row {
                spacing: 8
                anchors.right: parent.right
                Button {
                    objectName: "quickCaptureCancel"
                    text: qsTr("Cancel")
                    onClicked: root.close()
                }
                Button {
                    objectName: "quickCaptureSave"
                    text: qsTr("Save note")
                    highlighted: true
                    enabled: captureArea.text.trim() !== ""
                    onClicked: root.save()
                }
            }
        }
    }
}
