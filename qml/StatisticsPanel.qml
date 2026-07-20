// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Document statistics popover (features.md §19.1; phase11 decision 7): the six
// counts and reading time for the document (and the selection when one is
// active), plus the session word delta. Reads documentStats live behind a
// 200 ms coalescing timer while open — off the keystroke path — mirroring the
// status bar's counting discipline.
Popup {
    id: statsPopup
    objectName: "statisticsPanel"

    property var appWindow
    // The focused block delegate (for an in-block selection's display text),
    // bound by main.qml from the toolbar's target.
    property var targetBlock

    modal: false
    focus: false
    padding: 0
    width: 240

    property var docStats: ({})
    property var selStats: null
    readonly property string inBlockSelDep:
        targetBlock && targetBlock.selectedDisplayText !== undefined
            ? targetBlock.selectedDisplayText : ""

    function recompute() {
        docStats = documentStats.documentStats()
        // Selection stats: a block selection, cross-block text range, or an
        // in-block selection, assembled as display text like the status bar.
        var selText = statsPopup.selectionText()
        selStats = selText === null ? null
                                    : documentStats.statsForText(selText)
    }

    // The selected text as DISPLAY text (markers stripped), or null when
    // nothing is selected — the same representation documentStats counts.
    function selectionText() {
        if (documentSelection.hasBlockSelection) {
            var idx = documentSelection.selectedIndexes()
            var parts = []
            for (var i = 0; i < idx.length; i++)
                parts.push(blockModel.displayTextAt(idx[i]))
            return parts.join("\n")
        }
        if (documentSelection.hasTextSelection) {
            // A cross-block text range: partial edge blocks contribute their
            // covered slice; counting the raw slice is close enough for a
            // selection readout (whole-block portions strip markers).
            var range = documentSelection.orderedTextRange()
            var out = []
            for (var b = range.startIndex; b <= range.endIndex; b++) {
                var content = blockModel.getContent(b)
                var from = b === range.startIndex ? range.startPos : 0
                var to = b === range.endIndex ? range.endPos : content.length
                var slice = content.substring(from, to)
                out.push((from === 0 && to === content.length)
                    ? blockModel.displayTextAt(b) : slice)
            }
            return out.join("\n")
        }
        var target = statsPopup.targetBlock
        if (target && target.selectedDisplayText !== undefined
            && target.selectedDisplayText.length > 0)
            return target.selectedDisplayText
        return null
    }

    function scheduleRecompute() {
        if (visible)
            liveTimer.restart()
    }

    onAboutToShow: recompute()
    onInBlockSelDepChanged: scheduleRecompute()

    Timer {
        id: liveTimer
        interval: 200
        repeat: false
        onTriggered: statsPopup.recompute()
    }
    Connections {
        target: blockModel
        function onDataChanged() { statsPopup.scheduleRecompute() }
        function onCountChanged() { statsPopup.scheduleRecompute() }
        function onDocumentCountsChanged() { statsPopup.scheduleRecompute() }
    }
    Connections {
        target: documentSelection
        function onRevisionChanged() { statsPopup.scheduleRecompute() }
    }

    background: Rectangle {
        color: theme.popupBackground
        border.color: theme.borderStrong
        border.width: 1
        radius: 6
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: "transparent"
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: statsPopup.selStats ? qsTr("Selection") : qsTr("Document")
                font.pixelSize: 12
                font.bold: true
                color: theme.textSecondary
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

        // The active stat set: selection when present, else document.
        Repeater {
            model: {
                var s = statsPopup.selStats ? statsPopup.selStats
                                            : statsPopup.docStats
                if (!s || s.words === undefined)
                    return []
                var rows = [
                    { k: qsTr("Words"), v: s.words },
                    { k: qsTr("Characters"), v: s.charsWithSpaces },
                    { k: qsTr("Characters (no spaces)"), v: s.charsNoSpaces },
                    { k: qsTr("Paragraphs"), v: s.paragraphs },
                ]
                if (!statsPopup.selStats)
                    rows.push({ k: qsTr("Blocks"), v: s.blocks })
                rows.push({ k: qsTr("Reading time"),
                            v: s.readingMinutes <= 0 ? qsTr("—")
                               : (s.readingMinutes + qsTr(" min")) })
                return rows
            }
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: 5
                Layout.bottomMargin: 5
                Text {
                    text: modelData.k
                    font.pixelSize: 12
                    color: theme.textMuted
                    Layout.fillWidth: true
                }
                Text {
                    text: modelData.v
                    font.pixelSize: 12
                    font.bold: true
                    color: theme.textPrimary
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; height: 1; color: theme.border
            visible: appWindow && appWindow.collectionOpen
        }
        // Session tracker (ephemeral): words added since the note opened.
        RowLayout {
            visible: appWindow && appWindow.collectionOpen
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 5
            Layout.bottomMargin: 8
            Text {
                text: qsTr("This session")
                font.pixelSize: 12
                color: theme.textMuted
                Layout.fillWidth: true
            }
            Text {
                property int delta: appWindow
                    ? (statsPopup.docStats.words || 0) - appWindow.sessionStartWords
                    : 0
                text: (delta >= 0 ? "+" : "") + delta
                font.pixelSize: 12
                font.bold: true
                color: delta >= 0 ? theme.success : theme.textMuted
            }
        }
    }
}
