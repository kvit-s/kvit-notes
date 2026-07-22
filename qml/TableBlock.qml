// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The table cells nest content and handlers in separate scopes that
// read the row and cell ids declared around them.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// Table block (features.md §1.2.11). The block's content is the raw
// pipe-table markdown; TableTools parses it to a grid and applies every
// mutation as a whole-markdown rewrite (one undo step). Cells render
// statically through the formatter (markdown → styled rich text); the
// clicked/tabbed cell becomes live, loading a single hybrid-editing engine
// at a time — so a large table costs one engine, not one per cell. The
// delegate keeps the non-text focus API of the other block delegates.
BlockDelegateBase {
    id: root

    // The editor window this row is in, typed. Null for any other window,
    // so the guards below still mean what they meant.
    readonly property KvitShell shell: Window.window as KvitShell


    required property int index
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
    property bool isFocused: activeRow !== -2 || focusTarget.activeFocus
    property bool isHovered: hoverArea.containsMouse

    // Parsed grid (re-evaluates on content change). Cell text is read out of
    // this map rather than re-parsed per cell: the grid already holds every
    // header and row, so a lookup is an array index.
    readonly property var grid: TableTools.parse(content)
    readonly property int columns: grid.valid ? grid.columns : 0
    readonly property int dataRows: grid.valid ? grid.rowCount : 0

    // A table is laid out inline in the document, so it has no viewport of
    // its own to virtualise against: every rendered row is a live row of
    // cells. Only a window of rows is built, and the rest are one button
    // away. Editing or navigating into a row past the window widens it,
    // so nothing becomes unreachable.
    readonly property int rowWindowStep: 100
    property int revealedRows: rowWindowStep
    readonly property int renderedRows: Math.min(dataRows, revealedRows)
    readonly property int hiddenRows: Math.max(0, dataRows - renderedRows)

    function revealThrough(row) {
        if (row >= revealedRows)
            revealedRows = row + 1
    }
    function revealAllRows() { revealedRows = dataRows }

    // The one live cell: activeRow -2 = none, -1 = header row, 0..n data row.
    property int activeRow: -2
    property int activeCol: -1

    // The cell rectangle the single editor is parented into, set by whichever
    // cell is active. One editor exists per table, not one Loader per cell.
    property Item activeCellItem: null

    readonly property int tableWidth: Math.max(240, root.width - 96)
    readonly property int colWidth: columns > 0 ? Math.floor(tableWidth / columns) : 80

    readonly property bool blockSelected: {
        var revision = DocumentSelection.revision // dependency only
        return DocumentSelection.isBlockSelected(root.index)
            || DocumentSelection.portionForBlock(root.index).selected === true
    }

    function markdownPositionAt(sceneX, sceneY) { return 0 }
    function pointInText(sceneX, sceneY) { return false }
    function lineStepPosition(mdPos, dir) { return -1 }
    function entryPositionAtX(x, fromTop) { return 0 }
    function xAtMarkdown(mdPos) { return 0 }

    readonly property bool isDragSource: {
        if (!root.shell || !root.shell.blockDrag || !root.shell.blockDrag.active)
            return false
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

    implicitHeight: gridColumn.implicitHeight + 16

    ListView.onPooled: { isPooled = true; activeRow = -2; opacity = 0 }
    // A reused delegate is a different table, so it must not inherit however
    // far the previous one had been expanded.
    ListView.onReused: { isPooled = false; opacity = 1; revealedRows = rowWindowStep }

    function focusAtStart() { editCell(-1, 0) }
    function focusAtEnd() { editCell(dataRows > 0 ? dataRows - 1 : -1, Math.max(0, columns - 1)) }
    function focusAtPosition(markdownPos) { focusAtStart() }
    function isCursorOnFirstLine() { return true }
    function isCursorOnLastLine() { return true }

    // ---- Mutations, each one model content update (one undo step) ----
    function writeTable(md) { BlockModel.updateContent(root.index, md) }
    function editCell(r, c) {
        revealThrough(r)
        activeRow = r
        activeCol = c
        focusTarget.forceActiveFocus()  // keep the block "focused" for the shell
    }
    function commitCell(r, c, value) {
        var md = TableTools.setCell(content, r, c, value)
        if (md !== content)
            writeTable(md)
    }
    // O(1) against the grid parsed once above. Asking TableTools for a cell
    // re-parsed the whole table markdown, which every rendered cell then paid
    // on every edit.
    function cellText(r, c) {
        if (!root.grid.valid || c < 0 || c >= root.columns)
            return ""
        if (r === -1)
            return root.grid.headers[c] !== undefined ? root.grid.headers[c] : ""
        if (r >= 0 && r < root.dataRows) {
            var row = root.grid.rows[r]
            return (row && row[c] !== undefined) ? row[c] : ""
        }
        return ""
    }
    function moveCell(forward) {
        var r = activeRow, c = activeCol
        if (forward) {
            if (c + 1 < columns) { editCell(r, c + 1); return }
            if (r === -1) { editCell(dataRows > 0 ? 0 : -1, 0)
                if (dataRows === 0) { writeTable(TableTools.insertRow(content, -1)); editCell(0, 0) }
                return }
            if (r + 1 < dataRows) { editCell(r + 1, 0); return }
            // Last cell: append a row and land in it.
            writeTable(TableTools.insertRow(content, dataRows - 1))
            editCell(dataRows, 0)  // dataRows is the new row's index after insert
        } else {
            if (c - 1 >= 0) { editCell(r, c - 1); return }
            if (r === 0) { editCell(-1, columns - 1); return }
            if (r > 0) { editCell(r - 1, columns - 1); return }
            // At header first cell: stay.
        }
    }
    function sortBy(col) {
        // Cycle: ascending, then descending on a repeat.
        var asc = !(root._lastSortCol === col && root._lastSortAsc)
        writeTable(TableTools.sortByColumn(content, col, asc))
        root._lastSortCol = col
        root._lastSortAsc = asc
    }
    property int _lastSortCol: -1
    property bool _lastSortAsc: false

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
    function createBlockBelow() {
        var newIndex = root.index + 1
        BlockModel.insertBlock(newIndex, 0, "")
        Qt.callLater(function() {
            if (listView) {
                listView.currentIndex = newIndex
                var item = (listView.itemAtIndex(newIndex) as BlockDelegateBase)
                if (item) item.focusAtStart()
            }
        })
    }
    function insertBlockBelowAndOpenMenu() {
        var newIndex = root.index + 1
        BlockModel.insertBlock(newIndex, 0, "")
        var lv = listView
        Qt.callLater(function() {
            if (!lv) return
            lv.currentIndex = newIndex
            var item = (lv.itemAtIndex(newIndex) as BlockDelegateBase)
            if (item) { item.focusAtStart(); if (item.openBlockMenu) item.openBlockMenu("insert") }
        })
    }

    Item {
        id: focusTarget
        objectName: "tableFocusItem"
        anchors.fill: parent
        activeFocusOnTab: true
        Keys.onPressed: function(event) {
            // When no cell is live, arrow/selection keys behave like the other
            // non-text delegates.
            if (root.activeRow !== -2)
                return
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && (event.modifiers & Qt.ControlModifier)
                && (event.modifiers & Qt.ShiftModifier)) {
                if (root.listView) root.listView.currentIndex = root.index
                DocumentSelection.selectBlock(root.index)
                root.focusSelectionHandler(); event.accepted = true; return
            }
            if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                DocumentSelection.selectAllBlocks()
                root.focusSelectionHandler(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Up && root.index > 0 && root.listView) {
                var pi = root.index - 1
                root.listView.currentIndex = pi
                var prev = (root.listView.itemAtIndex(pi) as BlockDelegateBase)
                if (prev) prev.focusAtEnd(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Down && root.index < BlockModel.count - 1
                && root.listView) {
                var ni = root.index + 1
                root.listView.currentIndex = ni
                var next = (root.listView.itemAtIndex(ni) as BlockDelegateBase)
                if (next) next.focusAtStart(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                root.deleteCurrentBlock(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.createBlockBelow(); event.accepted = true; return
            }
        }
    }

    // Selection / focus / hover background.
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 8
        radius: 4
        opacity: root.isDragSource ? 0.35 : 1
        color: root.blockSelected ? Theme.blockSelectionTint
             : (root.isHovered ? Theme.blockHoverTint : "transparent")
        border.color: root.blockSelected ? Theme.accent : "transparent"
        border.width: root.blockSelected ? 1 : 0
    }

    // The grid.
    Column {
        id: gridColumn
        objectName: "tableGrid"
        x: 36
        y: 8
        width: root.tableWidth
        opacity: root.isDragSource ? 0.35 : 1

        // Header row + the rendered window of data rows, as row indices
        // -1..renderedRows-1.
        Repeater {
            model: root.renderedRows + 1
            delegate: Row {
                id: rowItem
                required property int index
                readonly property int rowIndex: index - 1   // -1 = header

                Repeater {
                    model: root.columns
                    delegate: Rectangle {
                        id: cell
                        required property int index
                        readonly property int colIndex: index
                        width: root.colWidth
                        implicitHeight: Math.max(30, cellContent.implicitHeight + 12)
                        height: implicitHeight
                        color: rowItem.rowIndex === -1 ? Theme.chipBackground
                             : (root.activeRow === rowItem.rowIndex
                                && root.activeCol === colIndex ? Theme.focusTint
                                : Theme.windowBackground)
                        border.width: 1
                        border.color: Theme.border

                        readonly property bool isActive:
                            root.activeRow === rowItem.rowIndex
                            && root.activeCol === cell.colIndex
                        readonly property int align: {
                            var a = root.grid.valid ? root.grid.alignments[cell.colIndex] : "none"
                            return a === "center" ? Text.AlignHCenter
                                 : a === "right" ? Text.AlignRight : Text.AlignLeft
                        }

                        // Static rendering (markdown → styled rich text).
                        Text {
                            id: cellContent
                            visible: !cell.isActive
                            anchors.fill: parent
                            anchors.margins: 6
                            text: MarkdownFormatter.toHtml(
                                root.cellText(rowItem.rowIndex, cell.colIndex))
                            textFormat: Text.RichText
                            wrapMode: Text.Wrap
                            font.bold: rowItem.rowIndex === -1
                            color: Theme.textPrimary
                            horizontalAlignment: cell.align
                            verticalAlignment: Text.AlignVCenter
                        }

                        // The table-scope editor parents itself here while
                        // this cell is the active one. A Loader per cell
                        // bought nothing: only one can ever be active.
                        onIsActiveChanged: {
                            if (isActive)
                                root.activeCellItem = cell
                            else if (root.activeCellItem === cell)
                                root.activeCellItem = null
                        }
                        Component.onDestruction: {
                            if (root.activeCellItem === cell)
                                root.activeCellItem = null
                        }

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            onClicked: function(mouse) {
                                if (rowItem.rowIndex === -1
                                    && (mouse.modifiers & Qt.NoModifier) === 0) {
                                    // plain click on header edits it
                                }
                                root.editCell(rowItem.rowIndex, cell.colIndex)
                            }
                            // A header cell has a sort affordance on hover.
                            onDoubleClicked: {
                                if (rowItem.rowIndex === -1)
                                    root.sortBy(cell.colIndex)
                            }
                        }

                        // Header sort indicator.
                        Text {
                            visible: rowItem.rowIndex === -1
                                     && root._lastSortCol === cell.colIndex
                            anchors.right: parent.right
                            anchors.rightMargin: 3
                            anchors.verticalCenter: parent.verticalCenter
                            text: root._lastSortAsc ? "▲" : "▼"
                            font.pixelSize: 8
                            color: Theme.textFaint
                        }
                    }
                }
            }
        }

        // What the row window is holding back, and the way past it. Without
        // this the omitted rows would simply look deleted.
        Row {
            objectName: "tableRowWindowNotice"
            visible: root.hiddenRows > 0
            spacing: 6
            topPadding: 4
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%n more row(s) not shown", "", root.hiddenRows)
                font.pixelSize: 11
                color: Theme.textMuted
            }
            Button {
                objectName: "tableShowAllRows"
                text: qsTr("Show all")
                focusPolicy: Qt.NoFocus
                font.pixelSize: 11
                onClicked: root.revealAllRows()
            }
        }

        // Add-row / add-column buttons.
        Row {
            spacing: 6
            topPadding: 4
            Button {
                objectName: "tableAddRow"
                text: qsTr("+ Row")
                focusPolicy: Qt.NoFocus
                font.pixelSize: 11
                onClicked: {
                    // The appended row is the last one, so it has to be inside
                    // the window or the button would look like it did nothing.
                    root.revealThrough(root.dataRows)
                    root.writeTable(
                        TableTools.insertRow(root.content, root.dataRows - 1))
                }
            }
            Button {
                objectName: "tableAddColumn"
                text: qsTr("+ Column")
                focusPolicy: Qt.NoFocus
                font.pixelSize: 11
                onClicked: root.writeTable(
                    TableTools.insertColumn(root.content, root.columns - 1))
            }
        }
    }

    // One editor for the whole table, reparented into whichever cell is
    // active. It carries the block's only BlockEditorEngine.
    Loader {
        id: cellEditor
        parent: root.activeCellItem !== null ? root.activeCellItem : root
        active: root.activeCellItem !== null
        anchors.fill: parent
        anchors.margins: 3
        sourceComponent: cellEditorComponent
    }

    Component {
        id: cellEditorComponent
        Item {
            id: editorRoot
            readonly property int rowIndex: root.activeRow
            readonly property int colIndex: root.activeCol

            // Focus and caret placement used to come with a freshly created
            // per-cell editor. One editor for the whole table stays loaded
            // while it moves between cells, so it re-applies them itself:
            // on creation for the first cell, and on every move after that.
            function beginEditing() {
                cellArea.forceActiveFocus()
                cellArea.cursorPosition = cellArea.length
            }
            Component.onCompleted: beginEditing()
            Connections {
                target: root
                function onActiveCellItemChanged() {
                    if (root.activeCellItem !== null)
                        editorRoot.beginEditing()
                }
            }

            BlockEditorEngine {
                id: cellEngine
                document: cellArea.textDocument
                markdown: root.cellText(editorRoot.rowIndex, editorRoot.colIndex)
                cursorPosition: cellArea.cursorPosition
                cursorActive: cellArea.activeFocus
                theme: root.appThemeRef
                onMarkdownEdited: function(md) {
                    root.commitCell(editorRoot.rowIndex, editorRoot.colIndex, md)
                }
            }
            TextArea {
                id: cellArea
                objectName: "tableCellEditor"
                anchors.fill: parent
                background: null
                wrapMode: TextEdit.Wrap
                font.pixelSize: Typography.baseSize - 1
                color: Theme.textPrimary
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Tab) {
                        root.moveCell(true); event.accepted = true; return
                    }
                    if (event.key === Qt.Key_Backtab
                        || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        root.moveCell(false); event.accepted = true; return
                    }
                    if (event.key === Qt.Key_Escape) {
                        root.activeRow = -2
                        focusTarget.forceActiveFocus()
                        event.accepted = true; return
                    }
                }
            }
        }
    }
    // theme reference for the cell engine (a bare `theme` inside the engine
    // resolves to the engine's own property).
    readonly property var appThemeRef: Theme

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton   // never steals cell clicks
    }

    // Gutter plus-button + drag handle (matching the other block delegates).
    Rectangle {
        objectName: "plusButton"
        width: 18; height: 18; x: 10; y: 8
        radius: 4
        color: plusArea.containsMouse ? Theme.hoverTint : "transparent"
        opacity: root.isHovered ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Text { anchors.centerIn: parent; text: "+"; color: Theme.textMuted; font.pixelSize: 14; font.bold: true }
        MouseArea {
            id: plusArea
            anchors.fill: parent; anchors.margins: -2
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: root.insertBlockBelowAndOpenMenu()
        }
    }
    Item {
        objectName: "tableHandle"
        width: 14; height: 18; x: 30; y: 8
        opacity: root.isHovered || tableHandleArea.pressed ? 0.6 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Column {
            anchors.centerIn: parent; spacing: 2
            Repeater { model: 2; Row { spacing: 2; Repeater { model: 2
                Rectangle { width: 3; height: 3; radius: 1.5; color: Theme.textFaint } } } }
        }
        MouseArea {
            id: tableHandleArea
            objectName: "dragHandle"
            anchors.fill: parent; anchors.margins: -2
            hoverEnabled: true; cursorShape: Qt.OpenHandCursor; preventStealing: true
            property real pressX: 0; property real pressY: 0; property bool dragging: false
            onPressed: function(mouse) { pressX = mouse.x; pressY = mouse.y; dragging = false }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                if (!root.shell || !root.shell.blockDrag) return
                var sp = tableHandleArea.mapToItem(null, mouse.x, mouse.y)
                if (!dragging) {
                    if (Math.abs(mouse.x - pressX) < 5 && Math.abs(mouse.y - pressY) < 5) return
                    dragging = true; root.shell.blockDrag.begin(root.index, sp.x, sp.y)
                } else { root.shell.blockDrag.update(sp.x, sp.y) }
            }
            onReleased: {
                if (dragging) { dragging = false; if (root.shell && root.shell.blockDrag) root.shell.blockDrag.drop(); return }
                if (root.listView) root.listView.currentIndex = root.index
                DocumentSelection.selectBlock(root.index)
                root.focusSelectionHandler()
            }
            onCanceled: {
                if (dragging) { dragging = false;                    if (root.shell && root.shell.blockDrag) root.shell.blockDrag.cancel() }
            }
        }
    }
}
