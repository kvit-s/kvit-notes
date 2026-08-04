// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The tag chips are a Repeater delegate whose Row and handlers are
// separate scopes, so the chip is named and its role declared rather
// than injected.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The open note's tag strip: existing tags as removable chips, plus an
// add-field with autocomplete over the registry (features.md §8.2 —
// autocomplete, create on the fly). Sits at the top of the editor pane in
// collection mode.
Item {
    id: tagStrip
    objectName: "tagStrip"

    property var appWindow

    // Tags of the open note, live under the collection revision.
    readonly property var noteTags: {
        var revision = NoteCollection.revision // dependency
        if (!appWindow || appWindow.currentNoteRelPath === "")
            return []
        var info = NoteCollection.noteInfo(appWindow.currentNoteRelPath)
        return info.tags !== undefined ? info.tags : []
    }

    // The registry as [{name, count, color}], same revision dependency.
    readonly property var registry: {
        var revision = NoteCollection.revision
        return NoteCollection.isOpen ? NoteCollection.tagListing() : []
    }

    // Suggestions: registry names matching the typed text (case-
    // insensitive substring), minus tags already on the note.
    readonly property var suggestions: {
        var typed = addField.text.toLowerCase()
        var used = noteTags
        var out = []
        for (var i = 0; i < registry.length; i++) {
            var name = registry[i].name
            if (used.indexOf(name) !== -1)
                continue
            if (typed === "" || name.toLowerCase().indexOf(typed) !== -1)
                out.push(name)
        }
        return out
    }

    // Looked up by position in the model rather than by reading a property
    // off the item: itemAt() is typed QQuickItem, so `item.tagName` cannot be
    // resolved statically. The repeater's items correspond to noteTags
    // one-for-one and each chip's tagName IS its modelData, so the index is
    // the same answer by a route that types.
    function chipFor(name) {
        var idx = tagStrip.noteTags.indexOf(name)
        return idx < 0 ? null : chipRepeater.itemAt(idx)
    }

    function colorOf(name) {
        for (var i = 0; i < registry.length; i++) {
            if (registry[i].name === name)
                return registry[i].color !== "" ? registry[i].color : Theme.mutedGlyph
        }
        return Theme.mutedGlyph
    }

    function applyTag(name) {
        if (name.trim() === "" || appWindow.currentNoteRelPath === "")
            return
        NoteCollection.addTag(appWindow.currentNoteRelPath, name.trim())
        addField.text = ""
        suggestionsPopup.close()
    }

    Flow {
        id: chipFlow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Interface.px(6)

        Repeater {
            id: chipRepeater
            model: tagStrip.noteTags

            Rectangle {
                id: chip
                objectName: "tagChip"
                required property string modelData
                property string tagName: chip.modelData

                width: chipRow.width + 16
                height: Interface.px(22)
                radius: Interface.px(11)
                color: Qt.alpha(tagStrip.colorOf(chip.modelData), 0.18)
                border.color: Qt.alpha(tagStrip.colorOf(chip.modelData), 0.5)

                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("Tag %1").arg(chip.modelData)

                Row {
                    id: chipRow
                    anchors.centerIn: parent
                    spacing: Interface.px(4)
                    Text {
                        text: chip.modelData
                        font.pixelSize: Interface.small
                        color: Theme.textPrimary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        objectName: "tagChipRemove"
                        text: "×"
                        font.pixelSize: Interface.body
                        color: Theme.textMuted
                        anchors.verticalCenter: parent.verticalCenter
                        // A bare "×" is read out as whatever the screen
                        // reader calls that character, which says nothing
                        // about what it removes (accessibility.md Finding 1).
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Remove tag %1").arg(chip.tagName)
                        Accessible.onPressAction: NoteCollection.removeTag(
                            tagStrip.appWindow.currentNoteRelPath, chip.tagName)
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4 // a comfortable hit target
                            onClicked: NoteCollection.removeTag(
                                           tagStrip.appWindow.currentNoteRelPath,
                                           chip.tagName)
                        }
                    }
                }
            }
        }

        TextField {
            id: addField
            objectName: "tagAddField"
            width: Interface.px(110)
            implicitHeight: Interface.px(22)
            font.pixelSize: Interface.small
            placeholderText: qsTr("+ Tag")
            background: Rectangle {
                color: "transparent"
                border.color: addField.activeFocus ? Theme.accent : Theme.borderStrong
                radius: Interface.px(11)
            }
            leftPadding: Interface.px(10)

            property int highlighted: -1

            onTextEdited: {
                highlighted = -1
                if (text !== "")
                    suggestionsPopup.open()
                else
                    suggestionsPopup.close()
            }
            onActiveFocusChanged: {
                if (!activeFocus)
                    suggestionsPopup.close()
            }

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Down && suggestionsPopup.visible) {
                    highlighted = Math.min(highlighted + 1,
                                           tagStrip.suggestions.length - 1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Up && suggestionsPopup.visible) {
                    highlighted = Math.max(highlighted - 1, -1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape) {
                    suggestionsPopup.close()
                    text = ""
                    event.accepted = true
                }
            }
            onAccepted: {
                var name = (highlighted >= 0
                            && highlighted < tagStrip.suggestions.length)
                    ? tagStrip.suggestions[highlighted] : text
                tagStrip.applyTag(name)
            }

            // A passive popup listing the matching registry tags.
            Popup {
                id: suggestionsPopup
                objectName: "tagSuggestionsPopup"
                y: addField.height + 2
                width: Interface.px(160)
                padding: Interface.px(2)
                focus: false
                closePolicy: Popup.CloseOnPressOutsideParent
                visible: false
                // Qt leaves a popup outside its window unless it is
                // given a margin, and this list is as long as the matches.
                margins: 6

                // Without a themed background the popup fell back to the light
                // Fusion default, so in dark mode the panel stayed light.
                background: Rectangle {
                    color: Theme.popupBackground
                    border.color: Theme.borderStrong
                    border.width: 1
                    radius: Interface.px(6)
                }

                // Deliberately not focusable, unlike the choice popups in
                // accessibility.md Finding 2: this is a completion list for
                // the field beside it, the field keeps the keyboard and drives
                // it with Up and Down, and taking focus here would close it.
                // What it needed was to be described rather than to be
                // reachable.
                contentItem: ListView {
                    id: suggestionList
                    implicitHeight: Math.min(contentHeight, 140)
                    clip: true
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Tag suggestions")
                    model: tagStrip.suggestions
                    delegate: Rectangle {
                        id: suggestionRow
                        required property string modelData
                        required property int index
                        width: suggestionList.width
                        height: Interface.px(22)
                        color: suggestionRow.index === addField.highlighted
                               ? Theme.selectionTint
                               : (suggestionHover.hovered ? Theme.hoverTint
                                                          : "transparent")
                        Accessible.role: Accessible.ListItem
                        Accessible.name: suggestionRow.modelData
                        Accessible.selected:
                            suggestionRow.index === addField.highlighted
                        Accessible.onPressAction:
                            tagStrip.applyTag(suggestionRow.modelData)
                        HoverHandler { id: suggestionHover; cursorShape: Qt.PointingHandCursor }
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 6
                            spacing: Interface.px(6)
                            Rectangle {
                                width: Interface.px(8)
                                height: Interface.px(8)
                                radius: Interface.px(4)
                                anchors.verticalCenter: parent.verticalCenter
                                color: tagStrip.colorOf(suggestionRow.modelData)
                            }
                            Text {
                                text: suggestionRow.modelData
                                font.pixelSize: Interface.small
                                color: Theme.textPrimary
                            }
                        }
                        TapHandler {
                            onTapped: tagStrip.applyTag(suggestionRow.modelData)
                        }
                    }
                }
            }
        }
    }
}
