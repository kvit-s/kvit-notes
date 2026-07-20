// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The preview list's delegate builds a row out of separate Labels, each
// its own scope. Binding those scopes lets them address the row by id
// instead of reaching a model role by injection.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// The floating find bar (features.md §7.1). A panel overlaying the
// editor's top-right corner — never docked, so opening it reflows
// nothing. All search state lives in the DocumentSearch context
// property; this file is rendering, focus, and key routing. The bar's
// fields take keyboard focus, which correctly blurs any focused block
// (reveals collapse, menus dismiss). Escape closes and returns focus to
// the current match; the query text survives a close so F3 can resume
// the search.
Rectangle {
    id: findBar
    objectName: "findBar"

    // Injected by main.qml.
    property var appWindow: null
    property var listView: null

    // Ctrl+H mode: the replace row shows.
    property bool replaceMode: false

    property alias queryField: queryField
    property alias replaceField: replaceField

    // The replace-all snapshot: rows computed when the
    // preview opens; any observable search change dismisses the panel,
    // so a confirm always applies exactly what was previewed.
    property var previewRows: []

    visible: false
    z: 500
    radius: 6
    color: Theme.popupBackground
    border.color: Theme.borderStrong
    border.width: 1
    implicitWidth: barColumn.implicitWidth + 16
    implicitHeight: barColumn.implicitHeight + 12

    // Read the persisted option states back from the settings store;
    // called at startup and from tests.
    function applyPersistedOptions() {
        caseButton.checked = AppSettings.value("find.caseSensitive", false)
        wordButton.checked = AppSettings.value("find.wholeWord", false)
        regexButton.checked = AppSettings.value("find.useRegex", false)
        preserveCaseButton.checked =
            AppSettings.value("find.preserveCase", false)
    }

    // Opening seeds the active cursor from the focused block, arms the
    // in-selection domain from a live document-level selection, or
    // prefills the query from an in-block selection (decisions 4, 6, 9).
    function open(withReplace) {
        replaceMode = withReplace

        var idx = appWindow ? appWindow.lastFocusedBlock : 0
        var item = listView ? listView.itemAtIndex(idx) : null
        var mdPos = (item && item.markdownCursor) ? item.markdownCursor() : 0
        DocumentSearch.setActiveCursor(idx, mdPos)

        if (DocumentSelection.hasBlockSelection) {
            DocumentSearch.setBlockDomain(DocumentSelection.selectedIndexes())
            inSelectionButton.checked = true
        } else if (DocumentSelection.hasTextSelection) {
            var range = DocumentSelection.orderedTextRange()
            DocumentSearch.setTextDomain(range.startIndex, range.startPos,
                                         range.endIndex, range.endPos)
            inSelectionButton.checked = true
        } else {
            DocumentSearch.clearDomain()
            inSelectionButton.checked = false
            if (item && item.selectionDisplayText) {
                var selected = item.selectionDisplayText()
                if (selected.length > 0 && selected.indexOf("\n") < 0)
                    queryField.text = selected
            }
        }

        visible = true
        queryDebounceTimer.stop()
        DocumentSearch.active = true
        DocumentSearch.query = queryField.text
        queryField.forceActiveFocus()
        queryField.selectAll()
        scrollToCurrent()
    }

    // Open seeded from a global-search result (features.md §8.4): the query
    // is given and the cursor seeds to the clicked occurrence, which the
    // at-or-after rule makes the current match.
    function openAt(query, blockIndex, mdPos) {
        replaceMode = false
        previewPanel.close()
        DocumentSearch.clearDomain()
        inSelectionButton.checked = false
        queryField.text = query
        DocumentSearch.setActiveCursor(blockIndex, mdPos)
        visible = true
        queryDebounceTimer.stop()
        DocumentSearch.active = true
        DocumentSearch.query = query
        DocumentSearch.recomputeNow()
        scrollToCurrent()
    }

    // Close and return focus to the document: to the
    // current match's block at the match start when there is one, else
    // to the last focused block.
    function close() {
        if (!visible)
            return
        previewPanel.close()
        var info = DocumentSearch.currentMatchInfo()
        visible = false
        DocumentSearch.active = false
        DocumentSearch.clearDomain()
        inSelectionButton.checked = false

        var idx = info.found ? info.blockIndex
                             : (appWindow ? appWindow.lastFocusedBlock : 0)
        var mdPos = info.found ? info.mdStart : -1
        if (!listView || idx < 0 || idx >= BlockModel.count)
            return
        listView.positionViewAtIndex(idx, ListView.Contain)
        Qt.callLater(function() {
            var item = listView.itemAtIndex(idx)
            if (!item)
                return
            if (mdPos >= 0 && item.focusAtPosition)
                item.focusAtPosition(mdPos)
            else if (item.focusAtEnd)
                item.focusAtEnd()
        })
    }

    function stepNext() {
        applyPendingQuery()
        DocumentSearch.next()
        scrollToCurrent()
    }

    function stepPrevious() {
        applyPendingQuery()
        DocumentSearch.previous()
        scrollToCurrent()
    }

    // F3 / Shift+F3: navigate while open; closed, they
    // reopen the bar with the kept query — "resume searching" — and do
    // nothing only when no query has been typed this session.
    function findNextShortcut() {
        if (visible)
            stepNext()
        else if (queryField.text.length > 0)
            open(false)
    }

    function findPreviousShortcut() {
        if (visible) {
            stepPrevious()
        } else if (queryField.text.length > 0) {
            open(false)
            stepPrevious()
        }
    }

    // Scroll the view to the current match (§7.1 "scroll to and
    // highlight"): position at the block, then nudge contentY for
    // blocks taller than the viewport slice so the match's line shows.
    function scrollToCurrent() {
        if (!visible || !listView)
            return
        var info = DocumentSearch.currentMatchInfo()
        if (!info.found)
            return
        listView.positionViewAtIndex(info.blockIndex, ListView.Contain)
        Qt.callLater(function() {
            var item = listView.itemAtIndex(info.blockIndex)
            if (!item || !item.rectForMarkdownPosition)
                return
            var rect = item.rectForMarkdownPosition(info.mdStart)
            var yInContent = item.y + rect.y
            var top = yInContent - listView.contentY
            var bottom = top + rect.height
            if (top < 0) {
                listView.contentY = Math.max(0, yInContent - 8)
            } else if (bottom > listView.height) {
                listView.contentY = Math.min(
                    Math.max(0, listView.contentHeight - listView.height),
                    yInContent + rect.height + 8 - listView.height)
            }
        })
    }

    function handleFieldKeys(event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (event.modifiers & Qt.ShiftModifier)
                stepPrevious()
            else
                stepNext()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            close()
            event.accepted = true
        }
    }

    // ---- Replace (features.md §7.2; decisions 8–10) ----

    function replaceOne() {
        applyPendingQuery()
        if (DocumentSearch.replaceCurrent(replaceField.text))
            scrollToCurrent()
    }

    // Replace All goes through the preview: the panel
    // lists every pending replacement; Confirm applies them as one undo
    // step, Cancel leaves the document untouched.
    function requestReplaceAll() {
        applyPendingQuery()
        if (DocumentSearch.matchCount === 0 || DocumentSearch.patternError)
            return
        previewRows = DocumentSearch.previewReplacements(replaceField.text)
        previewPanel.open()
    }

    function confirmReplaceAll() {
        applyPendingQuery()
        previewPanel.close()
        DocumentSearch.replaceAll(replaceField.text)
    }

    function applyPendingQuery() {
        if (!queryDebounceTimer.running)
            return
        queryDebounceTimer.stop()
        DocumentSearch.query = queryField.text
    }

    Timer {
        id: queryDebounceTimer
        interval: 30
        repeat: false
        onTriggered: {
            DocumentSearch.query = queryField.text
            findBar.scrollToCurrent()
        }
    }

    ColumnLayout {
        id: barColumn
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 6
        spacing: 4

        RowLayout {
            spacing: 3

            TextField {
                id: queryField
                objectName: "findQueryField"
                Layout.preferredWidth: 190
                Layout.preferredHeight: 28
                placeholderText: qsTr("Find")
                selectByMouse: true
                color: DocumentSearch.patternError ? Theme.danger : Theme.textPrimary
                Keys.onPressed: function(event) { findBar.handleFieldKeys(event) }
                onTextChanged: {
                    if (findBar.visible)
                        queryDebounceTimer.restart()
                    else
                        DocumentSearch.query = text
                }
            }

            Label {
                objectName: "findCountLabel"
                Layout.minimumWidth: 78
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                // matchCount/currentNumber/patternError all notify via
                // revisionChanged, so this re-evaluates on every change.
                text: DocumentSearch.patternError
                      ? qsTr("Invalid pattern")
                      : (DocumentSearch.matchCount > 0
                         ? DocumentSearch.currentNumber + qsTr(" of ")
                           + DocumentSearch.matchCount
                         : (DocumentSearch.query.length > 0
                            ? qsTr("No results") : ""))
                color: DocumentSearch.patternError ? Theme.danger : Theme.textMuted
                font.pixelSize: 12
            }

            ToolButton {
                objectName: "findPrevButton"
                text: "▲"
                implicitWidth: 28
                implicitHeight: 28
                font.pixelSize: 10
                enabled: DocumentSearch.matchCount > 0
                onClicked: findBar.stepPrevious()
            }
            ToolButton {
                objectName: "findNextButton"
                text: "▼"
                implicitWidth: 28
                implicitHeight: 28
                font.pixelSize: 10
                enabled: DocumentSearch.matchCount > 0
                onClicked: findBar.stepNext()
            }

            // Option toggles (§7.1). The buttons own the state; the
            // Binding elements below push it into the search object one
            // way, so user toggling never breaks a binding. Persisted
            // through the settings store — inSelectionOnly deliberately
            // not: its domain is armed from the selection present when
            // the bar opens.
            ToolButton {
                id: caseButton
                objectName: "findCaseButton"
                text: "Aa"
                checkable: true
                implicitWidth: 30
                implicitHeight: 28
                font.pixelSize: 12
                onToggled: findBar.scrollToCurrent()
                onCheckedChanged:
                    AppSettings.setValue("find.caseSensitive", checked)
            }
            ToolButton {
                id: wordButton
                objectName: "findWordButton"
                text: "ab"
                checkable: true
                implicitWidth: 30
                implicitHeight: 28
                font.pixelSize: 12
                font.underline: true
                onToggled: findBar.scrollToCurrent()
                onCheckedChanged:
                    AppSettings.setValue("find.wholeWord", checked)
            }
            ToolButton {
                id: regexButton
                objectName: "findRegexButton"
                text: ".*"
                checkable: true
                implicitWidth: 30
                implicitHeight: 28
                font.pixelSize: 12
                onToggled: findBar.scrollToCurrent()
                onCheckedChanged:
                    AppSettings.setValue("find.useRegex", checked)
            }

            ToolButton {
                objectName: "findCloseButton"
                text: "✕"
                implicitWidth: 28
                implicitHeight: 28
                font.pixelSize: 11
                onClicked: findBar.close()
            }
        }

        RowLayout {
            spacing: 3
            visible: findBar.replaceMode

            TextField {
                id: replaceField
                objectName: "replaceField"
                Layout.preferredWidth: 190
                Layout.preferredHeight: 28
                placeholderText: qsTr("Replace")
                selectByMouse: true
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        findBar.replaceOne()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        findBar.close()
                        event.accepted = true
                    }
                }
            }

            Button {
                objectName: "replaceOneButton"
                text: qsTr("Replace")
                implicitHeight: 28
                font.pixelSize: 12
                enabled: DocumentSearch.matchCount > 0
                onClicked: findBar.replaceOne()
            }
            Button {
                objectName: "replaceAllButton"
                text: qsTr("All")
                implicitHeight: 28
                font.pixelSize: 12
                enabled: DocumentSearch.matchCount > 0
                onClicked: findBar.requestReplaceAll()
            }

            // Preserve case (§7.2): adapt the replacement's casing to
            // each match's.
            ToolButton {
                id: preserveCaseButton
                objectName: "preserveCaseButton"
                text: "AB"
                checkable: true
                implicitWidth: 30
                implicitHeight: 28
                font.pixelSize: 12
                onCheckedChanged:
                    AppSettings.setValue("find.preserveCase", checked)
            }

            // Replace in selection only (§7.2): shows while a domain
            // was armed from a document-level selection at open time.
            ToolButton {
                id: inSelectionButton
                objectName: "inSelectionButton"
                text: qsTr("In selection")
                checkable: true
                visible: DocumentSearch.hasDomain
                implicitHeight: 28
                font.pixelSize: 11
            }
        }
    }

    Binding { target: DocumentSearch; property: "caseSensitive"; value: caseButton.checked }
    Binding { target: DocumentSearch; property: "wholeWord"; value: wordButton.checked }
    Binding { target: DocumentSearch; property: "useRegex"; value: regexButton.checked }
    Binding { target: DocumentSearch; property: "preserveCase"; value: preserveCaseButton.checked }
    Binding { target: DocumentSearch; property: "inSelectionOnly"; value: inSelectionButton.checked }

    // The replace-all preview: every pending replacement
    // as its match line with the matched text struck through and the
    // replacement inlined after it; nothing lands until Confirm.
    Popup {
        id: previewPanel
        objectName: "replacePreviewPanel"
        parent: findBar
        x: findBar.width - width
        y: findBar.height + 4
        width: 480
        padding: 10
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // A stale preview must never apply: any observable search
        // change (an edit recomputing matches, an option flip)
        // invalidates the snapshot and dismisses the panel.
        Connections {
            target: DocumentSearch
            enabled: previewPanel.visible
            function onRevisionChanged() { previewPanel.close() }
        }

        contentItem: ColumnLayout {
            spacing: 8

            Label {
                objectName: "previewSummaryLabel"
                text: {
                    var blocks = {}
                    for (var i = 0; i < findBar.previewRows.length; i++)
                        blocks[findBar.previewRows[i].blockIndex] = true
                    return qsTr("Replace %1 match(es) in %2 block(s)?")
                        .arg(findBar.previewRows.length)
                        .arg(Object.keys(blocks).length)
                }
                font.bold: true
                font.pixelSize: 13
            }

            ListView {
                objectName: "previewList"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(260, contentHeight)
                clip: true
                spacing: 2
                model: findBar.previewRows
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Row {
                    // The Labels below are each their own scope, so the row
                    // is named and its role declared rather than injected.
                    id: previewRow
                    required property var modelData
                    spacing: 0
                    Label {
                        text: (previewRow.modelData.blockIndex + 1) + ":  "
                        color: Theme.textFaint
                        font.pixelSize: 12
                    }
                    Label {
                        text: previewRow.modelData.prefix
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    Label {
                        text: previewRow.modelData.matched
                        color: Theme.danger
                        font.strikeout: true
                        font.pixelSize: 12
                    }
                    Label {
                        text: previewRow.modelData.replacement
                        color: Theme.success
                        font.bold: true
                        font.pixelSize: 12
                    }
                    Label {
                        text: previewRow.modelData.suffix
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 6
                Button {
                    objectName: "previewCancelButton"
                    text: qsTr("Cancel")
                    implicitHeight: 28
                    font.pixelSize: 12
                    onClicked: previewPanel.close()
                }
                Button {
                    objectName: "previewConfirmButton"
                    text: qsTr("Replace All")
                    implicitHeight: 28
                    font.pixelSize: 12
                    highlighted: true
                    onClicked: findBar.confirmReplaceAll()
                }
            }
        }
    }
}
