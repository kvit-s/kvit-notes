// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The results delegate nests a Column and a MouseArea, each its own
// scope. Binding them lets both address the delegate by id; its model
// roles were already declared as required properties.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The quick switcher: Ctrl+P opens a centered popup listing the
// collection's notes, fuzzy-filtered through the shared matcher in
// QuickSwitcherModel. Enter opens the highlighted note, Shift+Enter
// creates a note with the typed name in the current folder scope, Escape
// closes. Unlike the block menu this popup owns focus — the query lives in
// its own field, not in a block.
Popup {
    id: switcher
    objectName: "quickSwitcher"

    property var rows: []
    property int highlightIndex: 0

    signal noteChosen(string relPath)
    signal createRequested(string title)

    modal: true
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    parent: Overlay.overlay
    width: Math.min(520, parent ? parent.width - 80 : 520)
    padding: 8
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round(parent.height * 0.16) : 0

    function toggle() {
        if (visible)
            close()
        else
            openSwitcher()
    }

    function openSwitcher() {
        queryField.text = ""
        refilter()
        open()
        queryField.forceActiveFocus()
    }

    function refilter() {
        rows = QuickSwitcherModel.itemsFor(queryField.text)
        highlightIndex = rows.length > 0 ? 0 : -1
        if (resultsList)
            resultsList.positionViewAtBeginning()
    }

    function highlightStep(direction) {
        if (rows.length === 0)
            return
        highlightIndex =
            (highlightIndex + direction + rows.length) % rows.length
        resultsList.positionViewAtIndex(highlightIndex, ListView.Contain)
    }

    function applyHighlighted() {
        if (highlightIndex >= 0 && highlightIndex < rows.length) {
            var relPath = rows[highlightIndex].relPath
            close()
            noteChosen(relPath)
        }
    }

    function createFromQuery() {
        var title = queryField.text.trim()
        if (title === "")
            return
        close()
        createRequested(title)
    }

    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: 8
    }

    contentItem: Column {
        spacing: 6

        TextField {
            id: queryField
            objectName: "quickSwitcherField"
            width: parent.width
            placeholderText: qsTr("Find or create a note…")
            font.pixelSize: 14
            color: Theme.textPrimary
            placeholderTextColor: Theme.textFaint
            background: Rectangle {
                color: Theme.listBackground
                border.color: queryField.activeFocus
                              ? Theme.accent : Theme.borderStrong
                border.width: 1
                radius: 6
            }
            onTextChanged: switcher.refilter()
            Keys.onDownPressed: switcher.highlightStep(1)
            Keys.onUpPressed: switcher.highlightStep(-1)
            Keys.onReturnPressed: function(event) {
                if (event.modifiers & Qt.ShiftModifier)
                    switcher.createFromQuery()
                else
                    switcher.applyHighlighted()
            }
            Keys.onEnterPressed: function(event) {
                if (event.modifiers & Qt.ShiftModifier)
                    switcher.createFromQuery()
                else
                    switcher.applyHighlighted()
            }
        }

        Item {
            width: parent.width
            height: Math.min(switcher.rows.length * 44, 352)
            visible: switcher.rows.length > 0

            ListView {
                id: resultsList
                objectName: "quickSwitcherList"
                anchors.fill: parent
                clip: true
                interactive: contentHeight > height
                model: switcher.rows

                delegate: Rectangle {
                    // Named so the Column and MouseArea inside, each its own
                    // scope, address the row rather than relying on injection.
                    id: resultRow
                    required property var modelData
                    required property int index
                    width: resultsList.width
                    height: 44
                    radius: 6
                    color: resultRow.index === switcher.highlightIndex
                           ? Theme.hoverTint : "transparent"

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        spacing: 1

                        Text {
                            width: parent.width
                            text: resultRow.modelData.title
                            color: Theme.textPrimary
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            visible: resultRow.modelData.folder !== ""
                            text: resultRow.modelData.folder
                            color: Theme.textFaint
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: switcher.highlightIndex = resultRow.index
                        onClicked: switcher.applyHighlighted()
                    }
                }
            }
        }

        Text {
            width: parent.width
            visible: switcher.rows.length === 0
            text: queryField.text.trim() === ""
                  ? qsTr("No notes")
                  : qsTr("No matches — Shift+Enter creates “%1”")
                        .arg(queryField.text.trim())
            color: Theme.textFaint
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            padding: 10
        }
    }
}
