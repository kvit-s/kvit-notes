// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Nested delegates whose contents are separate scopes: the match rows
// sit inside a group Column, and their Labels and handlers reach both.
// Binding the scopes lets each address the right one by id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// Global-search results: grouped per note — a title row, then one row
// per match with its context snippet, the matched text bolded. Clicking
// a match opens the note and hands off to the find bar at that
// occurrence; clicking a title row opens the note plainly. Shown in
// place of the note list while a query is active.
Item {
    id: resultsView
    objectName: "searchResultsView"

    property var appWindow

    // Grouped results, live under the search revision.
    readonly property var groups: {
        var revision = CollectionSearch.revision
        return CollectionSearch.results()
    }

    function escapeHtml(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                   .replace(/>/g, "&gt;")
    }

    // The snippet with the matched range bolded, for Text.StyledText.
    function styledSnippet(match) {
        var s = match.snippet
        var at = match.snippetStart
        var len = match.snippetLength
        return escapeHtml(s.substring(0, at)) + "<b>"
            + escapeHtml(s.substring(at, at + len)) + "</b>"
            + escapeHtml(s.substring(at + len))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Summary and the date-preset filter; folder and tag filters
        // follow the sidebar's active scope (bound in main.qml) -----------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 4
            spacing: 4

            Label {
                objectName: "searchResultSummary"
                // While the index is still building, results may be incomplete;
                // the count is never presented as final.
                text: !CollectionSearch.complete
                      ? qsTr("Indexing… %1 match(es) so far — results may be "
                             + "incomplete")
                            .arg(CollectionSearch.matchCount)
                      : CollectionSearch.noteCount === 0
                        ? qsTr("No results")
                        : qsTr("%1 match(es) in %2 note(s)")
                              .arg(CollectionSearch.matchCount)
                              .arg(CollectionSearch.noteCount)
                font.pixelSize: 11
                color: Theme.textMuted
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            ComboBox {
                id: datePresetCombo
                objectName: "searchDatePresetCombo"
                implicitHeight: 24
                implicitWidth: 116
                font.pixelSize: 11
                readonly property var presets:
                    ["any", "today", "week", "month", "year", "custom"]
                model: [qsTr("Any time"), qsTr("Today"), qsTr("Last 7 days"),
                        qsTr("Last 30 days"), qsTr("Last year"),
                        qsTr("Custom range…")]
                currentIndex: Math.max(0,
                    presets.indexOf(CollectionSearch.datePreset))
                onActivated: function(index) {
                    if (presets[index] === "custom") {
                        dateRangePicker.openFor()
                        return
                    }
                    CollectionSearch.datePreset = presets[index]
                }

                // The calendar range picker for the custom date range
                // (features.md §8.4), anchored under the combo.
                DateRangePicker {
                    id: dateRangePicker
                    y: parent.height + 4
                    x: parent.width - width
                }
            }
        }

        // ---- Grouped results --------------------------------------------
        ListView {
            id: resultsList
            objectName: "searchResultsList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: resultsView.groups
            spacing: 2

            delegate: Column {
                id: groupColumn
                required property var modelData
                property var group: modelData

                width: resultsList.width

                // Note title row.
                Rectangle {
                    objectName: "searchTitleRow"
                    width: groupColumn.width
                    height: 26
                    color: titleHover.hovered ? Theme.hoverTint : "transparent"
                    HoverHandler { id: titleHover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 6
                        Label {
                            text: groupColumn.group.title
                            font.pixelSize: 12
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: groupColumn.group.matchCount > 0
                                  ? groupColumn.group.matchCount : ""
                            font.pixelSize: 11
                            color: Theme.textFaint
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: resultsView.appWindow
                                       .openNoteByPath(groupColumn.group.relPath)
                    }
                }

                // One row per match: click lands on that occurrence.
                Repeater {
                    model: groupColumn.group.matches
                    delegate: Rectangle {
                        id: matchRow
                        objectName: "searchMatchRow"
                        required property var modelData

                        width: groupColumn.width
                        height: 24
                        color: matchHover.hovered ? Theme.focusTint : "transparent"
                        HoverHandler { id: matchHover }

                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: 26
                            anchors.rightMargin: 12
                            verticalAlignment: Text.AlignVCenter
                            textFormat: Text.StyledText
                            text: resultsView.styledSnippet(matchRow.modelData)
                            font.pixelSize: 11
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: resultsView.appWindow.openSearchResult(
                                           groupColumn.group.relPath,
                                           matchRow.modelData.blockIndex,
                                           matchRow.modelData.start)
                        }
                    }
                }

                // Rows cap per note; the surplus is named, never silent.
                Label {
                    visible: groupColumn.group.moreMatches > 0
                    text: qsTr("… %1 more match(es) — open the note to see "
                               + "them all").arg(groupColumn.group.moreMatches)
                    font.pixelSize: 10
                    font.italic: true
                    color: Theme.textFaint
                    leftPadding: 26
                    height: visible ? 18 : 0
                }
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        }
    }
}
