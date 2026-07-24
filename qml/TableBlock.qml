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
    // The plus and drag-handle MouseAreas sit over hoverArea and steal its
    // hover; fold their own hover back in so the gutter buttons do not vanish
    // the moment the pointer reaches them (as EditableBlock/MathBlock do).
    property bool isHovered: hoverArea.containsMouse
        || plusArea.containsMouse || tableHandleArea.containsMouse
        || deleteArea.containsMouse

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
    // Column widths follow content instead of splitting evenly: each column's
    // weight is the longest cell it holds (header included), clamped so one
    // long column can't starve the others and a short one keeps a usable
    // minimum. Widths are handed out from tableWidth by weight; the frame then
    // spans their exact sum (gridWidth) so the right edge stays flush.
    readonly property var colWidths: {
        var cols = root.columns
        if (cols <= 0 || !root.grid.valid)
            return []
        var weights = []
        var total = 0
        for (var c = 0; c < cols; c++) {
            var longest = (root.grid.headers[c] !== undefined)
                ? String(root.grid.headers[c]).length : 1
            var n = Math.min(root.dataRows, root.renderedRows)
            for (var r = 0; r < n; r++) {
                var cells = root.grid.rows[r]
                var len = (cells && cells[c] !== undefined) ? String(cells[c]).length : 0
                if (len > longest)
                    longest = len
            }
            var w = Math.max(8, Math.min(40, longest))
            weights.push(w)
            total += w
        }
        var out = []
        for (var i = 0; i < cols; i++)
            // A floor so a short column beside long paragraph columns still
            // reads on one line rather than wrapping a single word.
            out.push(Math.max(92, Math.round(root.tableWidth * weights[i] / total)))
        return out
    }
    function colWidthAt(c) {
        return (c >= 0 && c < root.colWidths.length) ? root.colWidths[c] : 80
    }
    // Left edge of column c: the running sum of the widths before it. The
    // column separators are drawn once from this rather than per cell.
    function columnLeft(c) {
        var x = 0
        for (var i = 0; i < c && i < root.colWidths.length; i++)
            x += root.colWidths[i]
        return x
    }
    readonly property int gridWidth: {
        var sum = 0
        for (var i = 0; i < root.colWidths.length; i++)
            sum += root.colWidths[i]
        return sum > 0 ? sum : root.tableWidth
    }

    // Height of the live cell editor's text, so the active cell can grow to fit
    // what is being typed instead of clipping it.
    property real activeEditorHeight: 0

    // The cell a right-click context menu is acting on.
    property int menuRow: -2
    property int menuCol: -1

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

    // Right-click cell menu (below) acts on this cell.
    function openCellMenu(r, c) {
        root.menuRow = r
        root.menuCol = c
        cellMenu.popup()
    }
    // The live cell lost focus. If focus is still somewhere inside this table
    // (Tab moved between cells), keep editing; otherwise leave edit mode, so
    // the add controls fold away and the table reads cleanly again.
    function endEditingIfFocusLeft() {
        var p = Window.activeFocusItem
        while (p) {
            if (p === root)
                return
            p = p.parent
        }
        root.activeRow = -2
        root.activeCol = -1
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

    // A flat chip-style button matching the app's in-block affordances
    // (transparent until hovered), used for the table's add / show-all
    // controls instead of the OS-styled QtQuick Controls Button.
    component TableChipButton: Rectangle {
        id: chip
        property string label: ""
        signal clicked()
        implicitWidth: chipText.implicitWidth + 18
        implicitHeight: 22
        radius: 4
        color: chipHover.hovered ? Theme.hoverTint : "transparent"
        border.width: 1
        border.color: Theme.border
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: Theme.textMuted
            font.pixelSize: 11
        }
        HoverHandler { id: chipHover }
        TapHandler { onTapped: chip.clicked() }
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

    // The grid: a rounded, clipped frame holding the header and the rendered
    // window of data rows, then the row-window notice and the add controls
    // beneath it. Gridlines are drawn once per row and once per column rather
    // than per cell, so a large table does not pay an extra item per cell.
    Column {
        id: gridColumn
        objectName: "tableGrid"
        x: 36
        y: 8
        spacing: 6
        opacity: root.isDragSource ? 0.35 : 1

        Item {
            id: tableFrame
            width: root.gridWidth
            implicitHeight: rowsColumn.implicitHeight
            height: implicitHeight

            // Rounds the corners of the cell fills and clips the gridlines.
            Rectangle {
                id: clipRect
                anchors.fill: parent
                radius: 6
                color: Theme.windowBackground
                clip: true

                Column {
                    id: rowsColumn
                    width: parent.width

                    // Header row + the rendered window of data rows, as row
                    // indices -1..renderedRows-1. Each row is a Column so it
                    // can carry both its cells and the rule drawn beneath them.
                    Repeater {
                        model: root.renderedRows + 1
                        delegate: Column {
                            id: rowGroup
                            required property int index
                            readonly property int rowIndex: index - 1   // -1 = header
                            readonly property bool isHeader: rowGroup.rowIndex === -1
                            readonly property bool isLastRow: rowGroup.index === root.renderedRows

                            Row {
                                id: rowItem
                                Repeater {
                                    model: root.columns
                                    delegate: Rectangle {
                                        id: cell
                                        required property int index
                                        readonly property int colIndex: index
                                        readonly property int hPad: 10
                                        readonly property int vPad: 6
                                        readonly property real baseHeight:
                                            Math.max(32, cellContent.implicitHeight + cell.vPad * 2)
                                        width: root.colWidthAt(cell.colIndex)
                                        implicitHeight: cell.isActive
                                            ? Math.max(cell.baseHeight,
                                                       root.activeEditorHeight + cell.vPad * 2)
                                            : cell.baseHeight
                                        // The cell's own height, never the row's:
                                        // a Row derives its implicitHeight from
                                        // its children's heights, so a cell that
                                        // read the row height back would be a
                                        // binding loop that collapses the table.
                                        // A short data cell simply shows the
                                        // frame's matching windowBackground for
                                        // the rest of a taller row.
                                        height: cell.implicitHeight
                                        color: rowGroup.isHeader ? Theme.chipBackground
                                             : (cell.isActive ? Theme.focusTint
                                                : Theme.windowBackground)

                                        readonly property bool isActive:
                                            root.activeRow === rowGroup.rowIndex
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
                                            anchors.leftMargin: cell.hPad
                                            anchors.rightMargin: cell.hPad
                                            anchors.topMargin: cell.vPad
                                            anchors.bottomMargin: cell.vPad
                                            text: MarkdownFormatter.toHtml(
                                                root.cellText(rowGroup.rowIndex, cell.colIndex))
                                            textFormat: Text.RichText
                                            wrapMode: Text.Wrap
                                            font.bold: rowGroup.isHeader
                                            font.pixelSize: Typography.baseSize
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
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            onClicked: function(mouse) {
                                                if (mouse.button === Qt.RightButton) {
                                                    root.openCellMenu(rowGroup.rowIndex, cell.colIndex)
                                                    return
                                                }
                                                root.editCell(rowGroup.rowIndex, cell.colIndex)
                                            }
                                            // A header cell has a sort affordance on double-click.
                                            onDoubleClicked: {
                                                if (rowGroup.isHeader)
                                                    root.sortBy(cell.colIndex)
                                            }
                                        }

                                        // Header sort indicator.
                                        Text {
                                            visible: rowGroup.isHeader
                                                     && root._lastSortCol === cell.colIndex
                                            anchors.right: parent.right
                                            anchors.rightMargin: 4
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: root._lastSortAsc ? "▲" : "▼"
                                            font.pixelSize: 8
                                            color: Theme.textFaint
                                        }
                                    }
                                }
                            }

                            // The rule beneath this row: a heavier one under the
                            // header so it reads as a header, and none under the
                            // last row (the frame outline closes it off).
                            Rectangle {
                                visible: !rowGroup.isLastRow
                                width: root.gridWidth
                                height: rowGroup.isHeader ? 2 : 1
                                color: rowGroup.isHeader ? Theme.borderStrong : Theme.border
                            }
                        }
                    }
                }

                // Column separators, one per interior boundary, spanning the
                // full grid height. Drawn over the cells so they read as a grid.
                Repeater {
                    model: Math.max(0, root.columns - 1)
                    delegate: Rectangle {
                        required property int index
                        x: root.columnLeft(index + 1)
                        y: 0
                        width: 1
                        height: rowsColumn.height
                        color: Theme.border
                    }
                }
            }

            // The frame outline, drawn over the cell edges so the fills beneath
            // do not hide it.
            Rectangle {
                anchors.fill: parent
                radius: 6
                color: "transparent"
                border.width: 1
                border.color: Theme.borderStrong
            }
        }

        // What the row window is holding back, and the way past it. Without
        // this the omitted rows would simply look deleted.
        Row {
            objectName: "tableRowWindowNotice"
            visible: root.hiddenRows > 0
            spacing: 6
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%n more row(s) not shown", "", root.hiddenRows)
                font.pixelSize: 11
                color: Theme.textMuted
            }
            TableChipButton {
                objectName: "tableShowAllRows"
                anchors.verticalCenter: parent.verticalCenter
                label: qsTr("Show all")
                onClicked: root.revealAllRows()
            }
        }

        // Add-row / add-column controls. Kept out of the read-only rendered
        // view: they show only while a cell of this table is being edited, so a
        // table that is just being read is not cluttered by them. Removing rows
        // and columns is on the right-click cell menu.
        Row {
            objectName: "tableAddControls"
            spacing: 6
            visible: root.activeRow !== -2
            TableChipButton {
                objectName: "tableAddRow"
                label: qsTr("+ Row")
                onClicked: {
                    // The appended row is the last one, so it has to be inside
                    // the window or the button would look like it did nothing.
                    root.revealThrough(root.dataRows)
                    root.writeTable(
                        TableTools.insertRow(root.content, root.dataRows - 1))
                }
            }
            TableChipButton {
                objectName: "tableAddColumn"
                label: qsTr("+ Column")
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

            // Drive the active cell's height off what is being typed, so a cell
            // grows to fit its editor rather than clipping the text.
            Binding {
                target: root
                property: "activeEditorHeight"
                value: cellArea.contentHeight
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
                // Match the static cell's inset and size so text does not shift
                // or clip when a cell goes live.
                leftPadding: 10
                rightPadding: 10
                topPadding: 6
                bottomPadding: 6
                font.pixelSize: Typography.baseSize
                color: Theme.textPrimary
                onActiveFocusChanged: {
                    // Deferred: Tab between cells briefly drops focus before the
                    // next cell's editor takes it. endEditingIfFocusLeft keeps
                    // editing if focus stayed inside the table.
                    if (!activeFocus)
                        Qt.callLater(root.endEditingIfFocusLeft)
                }
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
                        root.activeCol = -1
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

    // Right-click cell menu: insert and delete rows and columns. Removing was
    // previously unreachable — the table had add buttons but no way back.
    // TableTools already provides the whole-markdown rewrites, so each item is
    // one undo step just like the add controls.
    Menu {
        id: cellMenu
        objectName: "tableCellMenu"
        MenuItem {
            objectName: "tableInsertRowAbove"
            text: qsTr("Insert row above")
            enabled: root.menuRow >= 0
            onTriggered: root.writeTable(TableTools.insertRow(root.content, root.menuRow - 1))
        }
        MenuItem {
            objectName: "tableInsertRowBelow"
            text: qsTr("Insert row below")
            onTriggered: {
                root.revealThrough(root.menuRow + 1)
                root.writeTable(TableTools.insertRow(root.content, root.menuRow))
            }
        }
        MenuItem {
            objectName: "tableDeleteRow"
            text: qsTr("Delete row")
            enabled: root.menuRow >= 0 && root.dataRows > 0
            onTriggered: root.writeTable(TableTools.removeRow(root.content, root.menuRow))
        }
        MenuSeparator {}
        MenuItem {
            objectName: "tableInsertColumnLeft"
            text: qsTr("Insert column left")
            onTriggered: root.writeTable(TableTools.insertColumn(root.content, root.menuCol - 1))
        }
        MenuItem {
            objectName: "tableInsertColumnRight"
            text: qsTr("Insert column right")
            onTriggered: root.writeTable(TableTools.insertColumn(root.content, root.menuCol))
        }
        MenuItem {
            objectName: "tableDeleteColumn"
            text: qsTr("Delete column")
            enabled: root.columns > 1
            onTriggered: root.writeTable(TableTools.removeColumn(root.content, root.menuCol))
        }
    }

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
    // Gutter delete-button, stacked under the plus. Removes this block;
    // undoable with Ctrl+Z, so no confirmation — the red hover fill is the
    // destructive cue. deleteArea folds into isHovered above so it does not
    // vanish under the pointer.
    Rectangle {
        objectName: "deleteButton"
        width: 18; height: 18; x: 10; y: 28; radius: 4
        color: deleteArea.containsMouse ? Theme.danger : "transparent"
        opacity: root.isHovered ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Text { anchors.centerIn: parent; text: "×"; color: deleteArea.containsMouse ? Theme.onAccent : Theme.textMuted; font.pixelSize: 15; font.bold: true }
        MouseArea { id: deleteArea; anchors.fill: parent; anchors.margins: -2
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: root.deleteCurrentBlock() }
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
