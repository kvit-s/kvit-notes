// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Deeply nested delegates — table cells, board groups, cards, card
// rows — each hold Texts and handlers in separate scopes.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Kvit 1.0

// Collection query block: a `query` code fence whose body is a small spec,
// rendered as a live read-only table or board over the front-matter of all
// notes in the collection. Results re-evaluate on every collection
// revision — in-app saves and external edits alike (the FileWatcher feeds
// refreshPaths) — and are never written to the file, so
// round-trip fidelity is untouched. Editing is plain fence editing of the
// spec, the DiagramBlock pattern: focus shows the source, blur writes it
// back as one undo step; a parse error shows in the read view.
BlockDelegateBase {
    id: root

    // The editor window this row is in, typed. Null for any other window,
    // so the guards below still mean what they meant.
    readonly property KvitShell shell: Window.window as KvitShell


    required property string blockId
    required property int blockType
    required property string content
    required property int indentLevel
    required property bool checked
    required property int ordinal
    required property string language
    required property string calloutTitle

    property int blockIndex: index
    property bool isPooled: false
    property ListView listView: ListView.view
    property bool isFocused: sourceArea.activeFocus
    // The gutter's MouseAreas sit over hoverArea and steal its hover; fold
    // the gutter's own hover back in so the buttons do not vanish the moment
    // the pointer reaches them (as EditableBlock does).
    property bool isHovered: hoverArea.containsMouse || blockHandle.hovered

    readonly property bool editing: sourceArea.activeFocus

    // {ok, error, view, columns, rows, groups} from QueryTools.
    property var queryResult: ({ ok: false, error: "", view: "table",
                                 columns: [], rows: [], groups: [] })

    // A query's `limit` defaults to unlimited, and results are drawn inline
    // with one Rectangle, Text, HoverHandler and TapHandler per cell, so a
    // broad query over a large vault builds a subtree the size of the vault.
    // Only a window is rendered; the count of what is held back is shown
    // next to the way past it. An explicit limit in the query still applies
    // first, and can be lower or higher than this.
    readonly property int rowWindowStep: 100
    property int revealedRows: rowWindowStep
    readonly property int totalRows:
        queryResult.ok && queryResult.rows ? queryResult.rows.length : 0
    readonly property int renderedRows: Math.min(totalRows, revealedRows)
    readonly property int hiddenRows: Math.max(0, totalRows - renderedRows)
    function revealAllRows() { revealedRows = totalRows }

    // The board view draws the same rows as cards grouped into columns, so
    // the window is shared out between the groups rather than applied to
    // each: twenty groups of a hundred would be two thousand cards, which is
    // the thing being avoided. A small floor keeps every group showing
    // something.
    readonly property int boardCardCap: {
        var groups = (queryResult.ok && queryResult.groups)
            ? queryResult.groups.length : 0
        if (groups <= 0)
            return revealedRows
        return Math.max(5, Math.ceil(revealedRows / groups))
    }

    function refresh() {
        // A new result is a new set of rows, so the window starts over.
        revealedRows = rowWindowStep
        // Evaluating scans the whole collection, so it runs on a worker and
        // answers through resultReady below. A result already computed for
        // this collection revision comes back from the cache here, which
        // keeps a re-render from flashing empty.
        var cached = QueryTools.cachedResult(root.content)
        if (cached && cached.ok !== undefined)
            queryResult = cached
        QueryTools.requestRun(root.blockId, root.content)
    }

    Connections {
        target: QueryTools
        enabled: !root.isPooled
        function onResultReady(token, result) {
            if (token === root.blockId)
                root.queryResult = result
        }
    }
    function scheduleRefresh() {
        refreshTimer.restart()
    }
    Timer {
        id: refreshTimer
        interval: 150
        repeat: false
        onTriggered: root.refresh()
    }
    Component.onCompleted: refresh()
    onContentChanged: scheduleRefresh()
    // A pooled delegate reused for a different block must not show the
    // previous block's results.
    onBlockIdChanged: {
        queryResult = ({ ok: false, error: "", view: "table",
                         columns: [], rows: [], groups: [] })
        refresh()
    }
    Connections {
        target: NoteCollection
        // Disabled while pooled. Stopping the timer on pooling was not
        // enough: the connection stayed live, so a collection revision
        // arriving afterwards restarted it and ran a whole-vault query for a
        // delegate nobody was looking at. The reuse path re-runs the query
        // for whatever block the delegate is given, so nothing is missed.
        enabled: !root.isPooled
        function onRevisionChanged() { root.scheduleRefresh() }
        function onRootChanged() { root.scheduleRefresh() }
    }

    readonly property bool blockSelected: {
        var revision = DocumentSelection.revision // dependency only
        return DocumentSelection.isBlockSelected(root.index)
            || DocumentSelection.portionForBlock(root.index).selected === true
    }

    // ---- non-text focus API (matches the other wave-2 blocks) ----
    // The results are computed from other notes and are at no offset into
    // this block's `query` fence, so the block joins a document-level range as
    // a whole unit and answers a single position 0 — copying such a range
    // still yields the fence. Selecting part of the results is a separate,
    // block-private thing; renderedSelection below is where that lives.
    function markdownPositionAt(sceneX, sceneY) { return 0 }
    function pointInText(sceneX, sceneY) { return false }
    function lineStepPosition(mdPos, dir) { return -1 }
    function entryPositionAtX(x, fromTop) { return 0 }
    function xAtMarkdown(mdPos) { return 0 }

    // Selecting part of the results with the pointer.
    RenderedTextSelection {
        id: renderedSelection
        objectName: "renderedSelection"
        content: card
        blockList: root.listView
        // Ctrl+C and Escape belong to selectionFocus below, so a sweep hands
        // it the keyboard. Not the spec editor: focusing that would open the
        // source over the results the sweep is selecting.
        onSweepStarted: selectionFocus.forceActiveFocus()
    }
    // Every collection revision replaces the rows, taking the runs a
    // selection names with them.
    onQueryResultChanged: renderedSelection.clear()

    // Where the keyboard sits while a rendered selection is showing. The
    // block's only other focus target is the spec editor, and taking focus
    // there is what opens it, so a sweep over the results needs somewhere
    // else to put the keyboard. It is never reached by Tab; only a sweep
    // focuses it.
    Item {
        id: selectionFocus
        objectName: "querySelectionFocus"
        activeFocusOnTab: false
        width: 0
        height: 0
        Keys.onPressed: function(event) {
            if (root.handleContextMenuKey(event))
                return
            if (renderedSelection.handleSelectionKey(event))
                return
            // The second Ctrl+A, once the block's own text is all selected,
            // is the document's, as it is in every other block.
            if (event.key === Qt.Key_A
                && (event.modifiers & Qt.ControlModifier)) {
                DocumentSelection.selectAllBlocks()
                root.focusSelectionHandler()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Escape) {
                root.focusSelectionHandler()
                event.accepted = true
            }
        }
    }

    readonly property bool isDragSource: {
        if (!root.shell || !root.shell.blockDrag || !root.shell.blockDrag.active) return false
        return root.shell.blockDrag.isMulti ? root.blockSelected
                                     : root.shell.blockDrag.sourceIndex === root.index
    }
    function focusSelectionHandler() {
        AppActions.requestSelectionFocus()
    }
    onIsFocusedChanged: {
        if (isFocused) {
            if (root.shell && root.shell.lastFocusedBlock !== undefined)
                root.shell.lastFocusedBlock = index
        }
    }

    blockContentHeight: contentColumn.implicitHeight + 16

    ListView.onPooled: {
        sourceArea.commitPendingSource()
        refreshTimer.stop()
        isPooled = true
        opacity = 0
        renderedSelection.clear()
        // An unlimited query over a large vault produces a large structure,
        // and holding it for a row that is off screen is holding it for
        // nothing. onBlockIdChanged already re-runs the query on reuse.
        queryResult = ({ ok: false, error: "", view: "table",
                         columns: [], rows: [], groups: [] })
    }
    ListView.onReused: {
        isPooled = false
        opacity = 1
        sourceArea.text = Qt.binding(function() { return root.content })
        // Pooling released the results, and a delegate reused for the same
        // block never sees blockId change, so this is the only thing that
        // asks for them back. Usually a cache hit, since the collection has
        // not moved while the row was off screen.
        refresh()
    }

    function focusAtStart() { sourceArea.forceActiveFocus(); sourceArea.cursorPosition = 0 }
    function focusAtEnd() { sourceArea.forceActiveFocus(); sourceArea.cursorPosition = sourceArea.length }
    function focusAtPosition(markdownPos) { focusAtStart() }
    function isCursorOnFirstLine() {
        var cursorRect = sourceArea.positionToRectangle(sourceArea.cursorPosition)
        var firstRect = sourceArea.positionToRectangle(0)
        return Math.abs(cursorRect.y - firstRect.y) < 1
    }
    function isCursorOnLastLine() {
        var cursorRect = sourceArea.positionToRectangle(sourceArea.cursorPosition)
        var lastRect = sourceArea.positionToRectangle(sourceArea.length)
        return Math.abs(cursorRect.y - lastRect.y) < 1
    }
    function focusAdjacentBlock(direction) {
        var targetIndex = root.index + direction
        if (!root.listView || targetIndex < 0 || targetIndex >= BlockModel.count)
            return false
        root.listView.currentIndex = targetIndex
        var target = (root.listView.itemAtIndex(targetIndex) as BlockDelegateBase)
        if (!target) return false
        if (direction < 0) target.focusAtEnd(); else target.focusAtStart()
        return true
    }
    function deleteCurrentBlock() {
        var prevIndex = root.index - 1
        BlockModel.removeBlock(root.index)
        Qt.callLater(function() {
            if (listView && prevIndex >= 0) {
                listView.currentIndex = prevIndex
                var item = (listView.itemAtIndex(prevIndex) as BlockDelegateBase)
                if (item) item.focusAtEnd()
            }
        })
    }
    // Ctrl+Enter out of the block: fold the editor away, then insert the new
    // row once the list has applied the resting geometry.
    function createBlockBelow() {
        root.forceActiveFocus()            // folds the spec editor away
        exitBelow.begin()
    }
    BlockExitBelow {
        id: exitBelow
        blockIndex: root.index
        listView: root.listView
        blockItem: root
        editing: root.editing
    }
    function insertBlockBelowAndOpenMenu() {
        var newIndex = root.index + 1
        BlockModel.insertBlock(newIndex, 0, "")
        var lv = listView
        Qt.callLater(function() {
            if (!lv) return
            lv.currentIndex = newIndex
            var item = (lv.itemAtIndex(newIndex) as BlockDelegateBase)
            if (item) {
                item.focusAtStart()
                if (item.openBlockMenu)
                    item.openBlockMenu("insert")
            }
        })
    }

    function openRow(relPath) {
            AppActions.requestOpenNoteByPath(relPath)
    }

    // Selection/focus catcher (declared before the card so per-row click
    // handlers window over it).
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        // Not an element: this covers the whole block so the §3.1
        // modifier-click gestures work on it, and the block itself is what a
        // screen reader should find here (accessibility.md Finding 1).
        Accessible.ignored: true
        onClicked: function(mouse) {
            // A sweep over the results ends with the button coming up over
            // this catcher, and a MouseArea's onClicked fires on release
            // however far the pointer travelled. Without this the gesture
            // that selects a row also opened the spec editor over it.
            if (renderedSelection.suppressClick)
                return
            if (mouse.modifiers & Qt.ControlModifier) {
                DocumentSelection.toggleBlock(root.index)
                if (DocumentSelection.hasBlockSelection)
                    root.focusSelectionHandler()
                return
            }
            if (mouse.modifiers & Qt.ShiftModifier) {
                var anchor = root.shell && root.shell.lastFocusedBlock !== undefined
                        ? root.shell.lastFocusedBlock : -1
                if (!DocumentSelection.hasBlockSelection
                    && anchor >= 0 && anchor !== root.index)
                    DocumentSelection.selectBlock(anchor)
                DocumentSelection.extendBlockSelectionTo(root.index)
                root.focusSelectionHandler()
                return
            }
            if (DocumentSelection.hasBlockSelection
                || DocumentSelection.hasTextSelection)
                DocumentSelection.clear()
            sourceArea.forceActiveFocus()
        }
    }

    Column {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 52
        anchors.rightMargin: 8
        anchors.top: parent.top
        anchors.topMargin: 4
        spacing: 6

        // ---- Source editor (the DiagramBlock pattern) ----
        // Above the results rather than in place of them, as a diagram and an
        // equation put their source above their live preview. Editing a spec
        // is watching what it matches change, and the results are the only
        // preview a query has; putting them away for the duration hid the
        // answer to the question being asked. It also made the results
        // unselectable in the one gesture that ends on them, since the
        // release lands on the block and the block's answer to a click is to
        // open its spec.
        Flickable {
            id: sourceFlick
            width: parent.width
            visible: root.editing
            height: root.editing ? Math.min(sourceArea.implicitHeight, 240) : 0
            clip: true
            contentWidth: sourceArea.implicitWidth
            contentHeight: sourceArea.implicitHeight
            interactive: contentWidth > width
            boundsBehavior: Flickable.StopAtBounds

            TextArea {
                id: sourceArea
                objectName: "querySourceArea"
                width: Math.max(implicitWidth, sourceFlick.width)
                text: root.content
                font.family: Typography.monoFamily
                font.pixelSize: Typography.monoSize
                color: Theme.textPrimary
                wrapMode: TextEdit.NoWrap
                selectByMouse: true
                background: Rectangle {
                    color: Theme.codePanelBackground
                    radius: 4
                    border.color: Theme.border; border.width: 1
                }
                // Committing only on focus loss means a click straight from
                // this editor onto another note replaces the model before the
                // callback runs, and the edit is gone. commitPendingSource is
                // therefore also driven by the document-level flush, and it
                // addresses the block by stable id because by the time it runs
                // this delegate may have been rebound to a different row.
                function commitPendingSource() {
                    if (text !== root.content)
                        BlockModel.updateContentById(root.blockId, text)
                }
                onActiveFocusChanged: {
                    if (!activeFocus) {
                        commitPendingSource()
                        text = Qt.binding(function() { return root.content })
                    }
                }
                Keys.onPressed: function(event) {
                    if (root.handleContextMenuKey(event))
                        return
                    // A query spec is a list of lines, so Enter is a line
                    // break here and Ctrl+Enter is the way to a new block —
                    // the same key the code, math and diagram editors use,
                    // named in the corner below. Commit first so the spec
                    // that the block runs is the one on screen.
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        && (event.modifiers & Qt.ControlModifier)) {
                        sourceArea.commitPendingSource()
                        root.createBlockBelow()
                        event.accepted = true
                        return
                    }
                    var arrowModifiers = Qt.ControlModifier | Qt.ShiftModifier
                        | Qt.AltModifier | Qt.MetaModifier
                    if (!(event.modifiers & arrowModifiers)
                        && event.key === Qt.Key_Up
                        && root.isCursorOnFirstLine()) {
                        if (root.focusAdjacentBlock(-1)) event.accepted = true
                        return
                    }
                    if (!(event.modifiers & arrowModifiers)
                        && event.key === Qt.Key_Down
                        && root.isCursorOnLastLine()) {
                        if (root.focusAdjacentBlock(1)) event.accepted = true
                        return
                    }
                    if (event.key === Qt.Key_Escape) {
                        root.focusSelectionHandler()
                        event.accepted = true
                    }
                }

                Connections {
                    target: DocumentManager
                    function onPendingEditsRequested() {
                        sourceArea.commitPendingSource()
                    }
                }
            }
        }

        // ---- Results ----
        // Shown whether or not the spec is being edited.
        Rectangle {
            id: card
            objectName: "queryCard"
            width: parent.width
            height: readColumn.implicitHeight + 16
            radius: 6
            color: root.blockSelected ? Theme.blockSelectionTint
                 : Theme.panelBackground
            border.color: root.blockSelected ? Theme.accent : Theme.border
            border.width: 1
            opacity: root.isDragSource ? 0.35 : 1
            clip: true

            // The sweep. A passive handler rather than a MouseArea, for the
            // reason CrossBlockTextDrag gives about the block editors: it
            // never takes the press away from the cell handlers below it, and
            // it goes on reporting the pointer after it has left the card.
            PointHandler {
                id: sweepObserver
                acceptedButtons: Qt.LeftButton
                onActiveChanged: {
                    var at = sweepObserver.point.scenePosition
                    if (sweepObserver.active)
                        renderedSelection.beginPress(at.x, at.y)
                    else
                        renderedSelection.endPress()
                }
                onPointChanged: {
                    if (!sweepObserver.active)
                        return
                    var at = sweepObserver.point.scenePosition
                    renderedSelection.updatePress(at.x, at.y)
                }
            }

            Column {
                id: readColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                spacing: 4

                Row {
                    spacing: 6
                    Text {
                        text: qsTr("Query")
                        font.pixelSize: Interface.small
                        font.bold: true
                        color: Theme.textMuted
                    }
                    Text {
                        objectName: "queryCountText"
                        visible: root.queryResult.ok
                        text: qsTr("%1 notes").arg(root.queryResult.rows.length)
                        font.pixelSize: Interface.small
                        color: Theme.textFaint
                    }
                }

                // Parse error / empty collection message.
                SelectableText {
                    objectName: "queryErrorText"
                    visible: !root.queryResult.ok
                    width: parent.width
                    text: root.queryResult.error
                    wrapMode: Text.Wrap
                    font.pixelSize: Interface.body
                    color: Theme.danger
                }

                // ---- Table view ----
                Grid {
                    id: tableGrid
                    objectName: "queryTable"
                    visible: root.queryResult.ok
                             && root.queryResult.view === "table"
                    width: parent.width
                    columns: Math.max(1, root.queryResult.columns.length)
                    columnSpacing: 12
                    rowSpacing: 0

                    // Header cells, then all row cells, one flat repeat.
                    Repeater {
                        model: root.queryResult.ok
                               && root.queryResult.view === "table"
                               ? root.queryResult.columns : []
                        SelectableText {
                            id: headerCell
                            required property var modelData
                            text: modelData
                            font.pixelSize: Interface.small
                            font.bold: true
                            color: Theme.textMuted
                            elide: Text.ElideRight
                            width: Math.max(40, (tableGrid.width
                                - tableGrid.columnSpacing
                                  * (tableGrid.columns - 1))
                                / tableGrid.columns)
                            bottomPadding: 4
                        }
                    }
                    Repeater {
                        model: {
                            if (!root.queryResult.ok
                                || root.queryResult.view !== "table")
                                return []
                            var flat = []
                            var rows = root.queryResult.rows
                            for (var r = 0; r < root.renderedRows; ++r) {
                                var cells = rows[r].cells
                                for (var c = 0;
                                     c < root.queryResult.columns.length; ++c)
                                    flat.push({
                                        text: c < cells.length ? cells[c] : "",
                                        relPath: rows[r].relPath,
                                        row: r,
                                    })
                            }
                            return flat
                        }
                        Rectangle {
                            id: tableCell
                            required property var modelData
                            width: Math.max(40, (tableGrid.width
                                - tableGrid.columnSpacing
                                  * (tableGrid.columns - 1))
                                / tableGrid.columns)
                            height: cellText.implicitHeight + 8
                            color: cellHover.hovered
                                   ? Theme.hoverTint : "transparent"
                            SelectableText {
                                id: cellText
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                text: tableCell.modelData.text
                                font.pixelSize: Interface.body
                                color: Theme.textPrimary
                                elide: Text.ElideRight
                            }
                            Accessible.role: Accessible.Cell
                            Accessible.name: tableCell.modelData.text
                            Accessible.description: qsTr("Opens %1")
                                .arg(tableCell.modelData.relPath)
                            Accessible.onPressAction:
                                root.openRow(tableCell.modelData.relPath)
                            HoverHandler { id: cellHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: {
                                    if (renderedSelection.suppressClick)
                                        return
                                    root.openRow(tableCell.modelData.relPath)
                                }
                            }
                        }
                    }
                }

                SelectableText {
                    visible: root.queryResult.ok
                             && root.queryResult.view === "table"
                             && root.queryResult.rows.length === 0
                    text: qsTr("No matching notes")
                    font.pixelSize: Interface.body
                    color: Theme.textFaint
                }

                // What the row window is holding back. Shown for both views,
                // since both draw from the same result rows.
                Text {
                    objectName: "queryRowWindowNotice"
                    visible: root.hiddenRows > 0
                    text: qsTr("%n more result(s) — show all", "",
                               root.hiddenRows)
                    font.pixelSize: Interface.small
                    color: showAllRows.containsMouse ? Theme.accent : Theme.link
                    font.underline: showAllRows.containsMouse
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Show all %n result(s)", "",
                                          root.hiddenRows)
                    Accessible.onPressAction: root.revealAllRows()
                    MouseArea {
                        id: showAllRows
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.revealAllRows()
                    }
                }

                // ---- Board view ----
                Flickable {
                    objectName: "queryBoard"
                    visible: root.queryResult.ok
                             && root.queryResult.view === "board"
                    width: parent.width
                    height: visible ? Math.min(boardRow.implicitHeight, 420) : 0
                    contentWidth: boardRow.implicitWidth
                    contentHeight: boardRow.implicitHeight
                    interactive: contentWidth > width
                    clip: true

                    Row {
                        id: boardRow
                        spacing: 8
                        Repeater {
                            model: root.queryResult.ok
                                   && root.queryResult.view === "board"
                                   ? root.queryResult.groups : []
                            Rectangle {
                                id: boardGroup
                                required property var modelData
                                width: 190
                                height: groupColumn.implicitHeight + 12
                                radius: 6
                                color: Theme.listBackground
                                border.color: Theme.border
                                border.width: 1

                                Column {
                                    id: groupColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 6
                                    spacing: 4

                                    Row {
                                        spacing: 5
                                        SelectableText {
                                            text: boardGroup.modelData.name
                                            font.pixelSize: Interface.body
                                            font.bold: true
                                            color: Theme.textSecondary
                                        }
                                        SelectableText {
                                            text: boardGroup.modelData.cards.length
                                            font.pixelSize: Interface.small
                                            color: Theme.textFaint
                                        }
                                    }

                                    Repeater {
                                        model: boardGroup.modelData.cards.length
                                                   <= root.boardCardCap
                                            ? boardGroup.modelData.cards
                                            : boardGroup.modelData.cards.slice(
                                                  0, root.boardCardCap)
                                        Rectangle {
                                            id: boardCard
                                            required property var modelData
                                            width: groupColumn.width
                                            height: cardCol.implicitHeight + 10
                                            radius: 4
                                            color: Theme.panelBackground
                                            border.color: cardHover.hovered
                                                ? Theme.accent : Theme.border
                                            border.width: 1
                                            Column {
                                                id: cardCol
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.top: parent.top
                                                anchors.margins: 5
                                                spacing: 1
                                                Repeater {
                                                    model: boardCard.modelData.cells
                                                    SelectableText {
                                                        required property var modelData
                                                        required property int index
                                                        width: cardCol.width
                                                        visible: String(modelData) !== ""
                                                        text: modelData
                                                        font.pixelSize: index === 0 ? 12 : 10
                                                        font.bold: index === 0
                                                        color: index === 0
                                                            ? Theme.textPrimary
                                                            : Theme.textMuted
                                                        elide: Text.ElideRight
                                                    }
                                                }
                                            }
                                            Accessible.role: Accessible.ListItem
                                            // The first cell is the card's
                                            // heading line, drawn larger and
                                            // bold; the rest are its detail.
                                            Accessible.name:
                                                boardCard.modelData.cells.length > 0
                                                    ? boardCard.modelData.cells[0]
                                                    : boardCard.modelData.relPath
                                            Accessible.description: qsTr("Opens %1")
                                                .arg(boardCard.modelData.relPath)
                                            Accessible.onPressAction: root.openRow(
                                                boardCard.modelData.relPath)
                                            HoverHandler { id: cardHover; cursorShape: Qt.PointingHandCursor }
                                            TapHandler {
                                                onTapped: {
                                                    if (renderedSelection.suppressClick)
                                                        return
                                                    root.openRow(
                                                        boardCard.modelData.relPath)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // The exit key, in the corner the code block puts it in, and only
        // while the spec editor is open.
        BlockKeyHint {
            objectName: "queryExitHint"
            width: parent.width
            visible: root.editing
            basePixelSize: Typography.monoSize
        }
    }

    // Hover chip: enter the spec editor without hunting for the focus path.
    Rectangle {
        objectName: "queryEditChip"
        anchors.right: contentColumn.right
        anchors.top: contentColumn.top
        anchors.margins: 6
        visible: root.isHovered && !root.editing
        width: editChipText.implicitWidth + 12
        height: 18
        radius: 4
        color: editChipArea.containsMouse ? Theme.hoverTint : Theme.chipBackground
        border.color: Theme.border
        border.width: 1
        Text {
            id: editChipText
            anchors.centerIn: parent
            text: qsTr("Edit query")
            font.pixelSize: Interface.caption
            color: Theme.textMuted
        }
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Edit query")
        Accessible.onPressAction: root.focusAtEnd()
        MouseArea {
            id: editChipArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.focusAtEnd()
        }
    }

    // The gutter: plus / delete / drag handle, shared with every other
    // block delegate so the strip does not shift as the pointer moves down
    // a document. The reorder itself goes to the window's coordinator.
    BlockGutter {
        id: blockHandle
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 4

        rowHovered: root.isHovered
        dragEnabled: root.shell !== null && root.shell.blockDrag !== null

        onInsertRequested: root.insertBlockBelowAndOpenMenu()
        onDeleteRequested: root.deleteCurrentBlock()
        onHandleMenuRequested: AppActions.requestBlockHandleMenu(root)
        onBlockSelectRequested: {
            if (root.listView)
                root.listView.currentIndex = root.index
            DocumentSelection.selectBlock(root.index)
            root.focusSelectionHandler()
        }
        onDragStarted: function(sceneX, sceneY) {
            root.shell.blockDrag.begin(root.index, sceneX, sceneY)
        }
        onDragMoved: function(sceneX, sceneY) {
            root.shell.blockDrag.update(sceneX, sceneY)
        }
        onDragDropped: {
            if (root.shell && root.shell.blockDrag)
                root.shell.blockDrag.drop()
        }
        onDragCanceled: {
            if (root.shell && root.shell.blockDrag)
                root.shell.blockDrag.cancel()
        }
    }
}
