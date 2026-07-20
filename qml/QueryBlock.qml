// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
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
    property bool isFocused: sourceArea.activeFocus
    property bool isHovered: hoverArea.containsMouse

    readonly property bool editing: sourceArea.activeFocus

    // {ok, error, view, columns, rows, groups} from QueryTools.
    property var queryResult: ({ ok: false, error: "", view: "table",
                                 columns: [], rows: [], groups: [] })

    function refresh() {
        queryResult = QueryTools.run(root.content)
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
        function onRevisionChanged() { root.scheduleRefresh() }
        function onRootChanged() { root.scheduleRefresh() }
    }

    readonly property bool blockSelected: {
        var revision = DocumentSelection.revision // dependency only
        return DocumentSelection.isBlockSelected(root.index)
            || DocumentSelection.portionForBlock(root.index).selected === true
    }

    // ---- non-text focus API (matches the other wave-2 blocks) ----
    function markdownPositionAt(sceneX, sceneY) { return 0 }
    function pointInText(sceneX, sceneY) { return false }
    function lineStepPosition(mdPos, dir) { return -1 }
    function entryPositionAtX(x, fromTop) { return 0 }
    function xAtMarkdown(mdPos) { return 0 }

    readonly property bool isDragSource: {
        var win = Window.window
        if (!win || !win.blockDrag || !win.blockDrag.active) return false
        return win.blockDrag.isMulti ? root.blockSelected
                                     : win.blockDrag.sourceIndex === root.index
    }
    function focusSelectionHandler() {
        var win = Window.window
        if (win && win.selectionKeyHandler)
            win.selectionKeyHandler.forceActiveFocus()
    }
    onIsFocusedChanged: {
        if (isFocused) {
            var win = Window.window
            if (win && win.lastFocusedBlock !== undefined)
                win.lastFocusedBlock = index
        }
    }

    implicitHeight: contentColumn.implicitHeight + 16

    ListView.onPooled: { isPooled = true; opacity = 0 }
    ListView.onReused: { isPooled = false; opacity = 1 }

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
        var target = root.listView.itemAtIndex(targetIndex)
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
                var item = listView.itemAtIndex(prevIndex)
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
                var item = listView.itemAtIndex(newIndex)
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
            var item = lv.itemAtIndex(newIndex)
            if (item) {
                item.focusAtStart()
                if (item.openBlockMenu)
                    item.openBlockMenu("insert")
            }
        })
    }

    function openRow(relPath) {
        var win = Window.window
        if (win && win.openNoteByPath)
            AppActions.requestOpenNoteByPath(relPath)
    }

    // Selection/focus catcher (declared before the card so per-row click
    // handlers win over it).
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: function(mouse) {
            if (mouse.modifiers & Qt.ControlModifier) {
                DocumentSelection.toggleBlock(root.index)
                if (DocumentSelection.hasBlockSelection)
                    root.focusSelectionHandler()
                return
            }
            if (mouse.modifiers & Qt.ShiftModifier) {
                var win = Window.window
                var anchor = win && win.lastFocusedBlock !== undefined
                        ? win.lastFocusedBlock : -1
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
        anchors.leftMargin: 48
        anchors.rightMargin: 8
        anchors.top: parent.top
        anchors.topMargin: 4
        spacing: 6

        // ---- Read view ----
        Rectangle {
            id: card
            objectName: "queryCard"
            width: parent.width
            visible: !root.editing
            height: root.editing ? 0 : readColumn.implicitHeight + 16
            radius: 6
            color: root.blockSelected ? Theme.blockSelectionTint
                 : Theme.panelBackground
            border.color: root.blockSelected ? Theme.accent : Theme.border
            border.width: 1
            opacity: root.isDragSource ? 0.35 : 1
            clip: true

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
                        font.pixelSize: 11
                        font.bold: true
                        color: Theme.textMuted
                    }
                    Text {
                        objectName: "queryCountText"
                        visible: root.queryResult.ok
                        text: qsTr("%1 notes").arg(root.queryResult.rows.length)
                        font.pixelSize: 11
                        color: Theme.textFaint
                    }
                }

                // Parse error / empty collection message.
                Text {
                    objectName: "queryErrorText"
                    visible: !root.queryResult.ok
                    width: parent.width
                    text: root.queryResult.error
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
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
                        Text {
                            required property var modelData
                            text: modelData
                            font.pixelSize: 11
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
                            for (var r = 0; r < rows.length; ++r) {
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
                            required property var modelData
                            width: Math.max(40, (tableGrid.width
                                - tableGrid.columnSpacing
                                  * (tableGrid.columns - 1))
                                / tableGrid.columns)
                            height: cellText.implicitHeight + 8
                            color: cellHover.hovered
                                   ? Theme.hoverTint : "transparent"
                            Text {
                                id: cellText
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                text: modelData.text
                                font.pixelSize: 12
                                color: Theme.textPrimary
                                elide: Text.ElideRight
                            }
                            HoverHandler { id: cellHover }
                            TapHandler {
                                onTapped: root.openRow(modelData.relPath)
                            }
                        }
                    }
                }

                Text {
                    visible: root.queryResult.ok
                             && root.queryResult.view === "table"
                             && root.queryResult.rows.length === 0
                    text: qsTr("No matching notes")
                    font.pixelSize: 12
                    color: Theme.textFaint
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
                                        Text {
                                            text: modelData.name
                                            font.pixelSize: 12
                                            font.bold: true
                                            color: Theme.textSecondary
                                        }
                                        Text {
                                            text: modelData.cards.length
                                            font.pixelSize: 11
                                            color: Theme.textFaint
                                        }
                                    }

                                    Repeater {
                                        model: modelData.cards
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
                                                    model: modelData.cells
                                                    Text {
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
                                            HoverHandler { id: cardHover }
                                            TapHandler {
                                                onTapped: root.openRow(
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

        // ---- Source editor (the DiagramBlock pattern) ----
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
                font.pixelSize: Typography.sizeForBlockType(Block.CodeBlock)
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
            font.pixelSize: 10
            color: Theme.textMuted
        }
        MouseArea {
            id: editChipArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.focusAtEnd()
        }
    }

    // Gutter plus-button.
    Rectangle {
        objectName: "plusButton"
        width: 18
        height: 18
        x: 10
        y: 8
        radius: 4
        color: plusArea.containsMouse ? Theme.hoverTint : "transparent"
        opacity: root.isHovered ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Text {
            anchors.centerIn: parent
            text: "+"
            color: Theme.textMuted
            font.pixelSize: 14
            font.bold: true
        }
        MouseArea {
            id: plusArea
            anchors.fill: parent
            anchors.margins: -2
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.insertBlockBelowAndOpenMenu()
        }
    }

    // Drag handle.
    Item {
        objectName: "queryHandle"
        width: 14
        height: 18
        x: 30
        y: 8
        opacity: root.isHovered || queryHandleArea.pressed ? 0.6 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Column {
            anchors.centerIn: parent
            spacing: 2
            Repeater {
                model: 2
                Row {
                    spacing: 2
                    Repeater {
                        model: 2
                        Rectangle { width: 3; height: 3; radius: 1.5; color: Theme.textFaint }
                    }
                }
            }
        }
        MouseArea {
            id: queryHandleArea
            objectName: "dragHandle"
            anchors.fill: parent
            anchors.margins: -2
            hoverEnabled: true
            cursorShape: Qt.OpenHandCursor
            preventStealing: true
            property real pressX: 0
            property real pressY: 0
            property bool dragging: false
            onPressed: function(mouse) { pressX = mouse.x; pressY = mouse.y; dragging = false }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                var win = Window.window
                if (!win || !win.blockDrag) return
                var sp = queryHandleArea.mapToItem(null, mouse.x, mouse.y)
                if (!dragging) {
                    if (Math.abs(mouse.x - pressX) < 5 && Math.abs(mouse.y - pressY) < 5)
                        return
                    dragging = true
                    win.blockDrag.begin(root.index, sp.x, sp.y)
                } else {
                    win.blockDrag.update(sp.x, sp.y)
                }
            }
            onReleased: {
                var win = Window.window
                if (dragging) {
                    dragging = false
                    if (win && win.blockDrag) win.blockDrag.drop()
                    return
                }
                if (root.listView)
                    root.listView.currentIndex = root.index
                DocumentSelection.selectBlock(root.index)
                root.focusSelectionHandler()
            }
            onCanceled: {
                if (dragging) {
                    dragging = false
                    var win = Window.window
                    if (win && win.blockDrag) win.blockDrag.cancel()
                }
            }
        }
    }
}
