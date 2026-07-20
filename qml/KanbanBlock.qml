// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// Kanban board (features.md §1.2.12). The block is a `kanban`-tagged code
// fence; its content is human-readable markdown that KanbanTools maps to a
// board and back. Every mutation — dragging a card within or between columns,
// reordering columns, add/remove, editing a card, toggling done — rewrites the
// fence content through the model as one undo step. Column collapse and the
// label filter are session-scoped chrome.
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
    property bool isFocused: focusTarget.activeFocus
    property bool isHovered: hoverArea.containsMouse

    readonly property var board: KanbanTools.parse(content)
    readonly property var columns: board.columns
    // Session-scoped collapse state and label filter.
    property var collapsed: ({})
    property string labelFilter: ""

    // All labels across the board (for the filter row and palette coloring).
    readonly property var allLabels: {
        var seen = []
        for (var c = 0; c < columns.length; ++c)
            for (var k = 0; k < columns[c].cards.length; ++k) {
                var ls = columns[c].cards[k].labels
                for (var l = 0; l < ls.length; ++l)
                    if (seen.indexOf(ls[l]) === -1) seen.push(ls[l])
            }
        return seen
    }
    function labelColor(label) {
        var pal = Theme.colorPalette
        var h = 0
        for (var i = 0; i < label.length; ++i) h = (h * 31 + label.charCodeAt(i)) % pal.length
        return pal[h]
    }

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
        var win = Window.window
        if (!win || !win.blockDrag || !win.blockDrag.active) return false
        return win.blockDrag.isMulti ? root.blockSelected
                                     : win.blockDrag.sourceIndex === root.index
    }
    function focusSelectionHandler() {
        AppActions.requestSelectionFocus()
    }
    onIsFocusedChanged: {
        if (isFocused) {
            var win = Window.window
            if (win && win.lastFocusedBlock !== undefined) win.lastFocusedBlock = index
        }
    }

    implicitHeight: boardColumn.implicitHeight + 16

    ListView.onPooled: { isPooled = true; opacity = 0 }
    ListView.onReused: { isPooled = false; opacity = 1 }

    function focusAtStart() { focusTarget.forceActiveFocus() }
    function focusAtEnd() { focusTarget.forceActiveFocus() }
    function focusAtPosition(markdownPos) { focusTarget.forceActiveFocus() }
    function isCursorOnFirstLine() { return true }
    function isCursorOnLastLine() { return true }

    function writeBoard(md) { BlockModel.updateContent(root.index, md) }

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
            if (item) { item.focusAtStart(); if (item.openBlockMenu) item.openBlockMenu("insert") }
        })
    }

    Item {
        id: focusTarget
        objectName: "kanbanFocusItem"
        anchors.fill: parent
        activeFocusOnTab: true
        Keys.onPressed: function(event) {
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)) {
                if (root.listView) root.listView.currentIndex = root.index
                DocumentSelection.selectBlock(root.index)
                root.focusSelectionHandler(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Up && root.index > 0 && root.listView) {
                var pi = root.index - 1
                root.listView.currentIndex = pi
                var prev = root.listView.itemAtIndex(pi)
                if (prev) prev.focusAtEnd(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Down && root.index < BlockModel.count - 1 && root.listView) {
                var ni = root.index + 1
                root.listView.currentIndex = ni
                var next = root.listView.itemAtIndex(ni)
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

    Column {
        id: boardColumn
        x: 34; y: 8
        width: root.width - 46
        spacing: 6
        opacity: root.isDragSource ? 0.35 : 1

        // Label filter chip row.
        Flow {
            width: parent.width
            spacing: 6
            visible: root.allLabels.length > 0
            Repeater {
                model: root.allLabels
                delegate: Rectangle {
                    required property var modelData
                    height: 20; radius: 10
                    width: fLabel.implicitWidth + 16
                    color: root.labelFilter === modelData
                        ? root.labelColor(modelData) : Theme.chipBackground
                    border.width: 1; border.color: root.labelColor(modelData)
                    Text {
                        id: fLabel
                        anchors.centerIn: parent
                        text: "#" + modelData
                        font.pixelSize: 11
                        color: root.labelFilter === modelData ? Theme.onAccent : Theme.textMuted
                    }
                    TapHandler {
                        onTapped: root.labelFilter = (root.labelFilter === modelData ? "" : modelData)
                    }
                }
            }
        }

        // The columns, laid out in a horizontal Flickable.
        Flickable {
            width: parent.width
            height: columnsRow.implicitHeight
            contentWidth: columnsRow.implicitWidth
            clip: true
            Row {
                id: columnsRow
                spacing: 10
                Repeater {
                    model: root.columns.length
                    delegate: Rectangle {
                        id: columnItem
                        required property int index
                        readonly property int colIndex: index
                        readonly property var colData: root.columns[colIndex]
                        readonly property bool isCollapsed: root.collapsed[colData.name] === true
                        width: 220
                        implicitHeight: colHeader.height + (isCollapsed ? 8 : colCards.implicitHeight + 16)
                        height: implicitHeight
                        radius: 6
                        color: Theme.panelBackground
                        border.width: 1; border.color: Theme.border

                        // Column header.
                        Item {
                            id: colHeader
                            width: parent.width; height: 30
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: (columnItem.isCollapsed ? "▸ " : "▾ ") + columnItem.colData.name
                                      + "  " + columnItem.colData.cards.length
                                font.bold: true; font.pixelSize: 12; color: Theme.textPrimary
                                TapHandler {
                                    onTapped: {
                                        var c = Object.assign({}, root.collapsed)
                                        c[columnItem.colData.name] = !columnItem.isCollapsed
                                        root.collapsed = c
                                    }
                                }
                            }
                            // Column controls: reorder left/right and add a
                            // card. Reorder is a button pair (a column drag is
                            // hard to land reliably); each is one undo step.
                            Row {
                                anchors.right: parent.right; anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8
                                Text {
                                    objectName: "kanbanColLeft"
                                    text: "‹"; font.pixelSize: 15
                                    color: columnItem.colIndex > 0 ? Theme.textMuted : Theme.textFaint
                                    TapHandler {
                                        enabled: columnItem.colIndex > 0
                                        onTapped: root.writeBoard(KanbanTools.moveColumn(
                                            root.content, columnItem.colIndex, columnItem.colIndex - 1))
                                    }
                                }
                                Text {
                                    objectName: "kanbanColRight"
                                    text: "›"; font.pixelSize: 15
                                    color: columnItem.colIndex < root.columns.length - 1
                                           ? Theme.textMuted : Theme.textFaint
                                    TapHandler {
                                        enabled: columnItem.colIndex < root.columns.length - 1
                                        onTapped: root.writeBoard(KanbanTools.moveColumn(
                                            root.content, columnItem.colIndex, columnItem.colIndex + 1))
                                    }
                                }
                                Text {
                                    objectName: "kanbanAddCard"
                                    text: "+"; font.pixelSize: 16; color: Theme.textMuted
                                    TapHandler {
                                        onTapped: root.writeBoard(
                                            KanbanTools.addCard(root.content, columnItem.colIndex, "New card"))
                                    }
                                }
                            }
                        }

                        // Drop target for cards.
                        DropArea {
                            anchors.top: colHeader.bottom
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            onDropped: function(drop) {
                                if (!drop.source || drop.source.cardColIndex === undefined) return
                                var toIdx = columnItem.colData.cards.length
                                root.writeBoard(KanbanTools.moveCard(root.content,
                                    drop.source.cardColIndex, drop.source.cardIndex,
                                    columnItem.colIndex, toIdx))
                                drop.accept()
                            }
                        }

                        Column {
                            id: colCards
                            visible: !columnItem.isCollapsed
                            anchors.top: colHeader.bottom
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.margins: 8
                            spacing: 6

                            Repeater {
                                model: columnItem.colData.cards.length
                                delegate: Rectangle {
                                    id: cardItem
                                    required property int index
                                    readonly property int cardIndex: index
                                    readonly property int cardColIndex: columnItem.colIndex
                                    readonly property var cardData: columnItem.colData.cards[cardIndex]
                                    width: colCards.width
                                    implicitHeight: cardCol.implicitHeight + 12
                                    height: implicitHeight
                                    radius: 5
                                    color: Theme.windowBackground
                                    border.width: 1; border.color: Theme.border
                                    // Dim cards not matching the active label filter.
                                    opacity: (root.labelFilter === ""
                                              || cardData.labels.indexOf(root.labelFilter) !== -1)
                                             ? (Drag.active ? 0.7 : 1) : 0.35

                                    // Card drag (features.md §1.2.12). The
                                    // canonical Qt idiom: Drag carries this
                                    // card as the drop payload; while the
                                    // handler is active a State reparents the
                                    // card out of its Column into `root` (a
                                    // coordinate-preserving ParentChange, so it
                                    // escapes the column clip and follows the
                                    // pointer), and reverts cleanly if the drop
                                    // misses. The drop itself is fired manually
                                    // because an internal drag does not auto-drop.
                                    Drag.active: cardDrag.active
                                    Drag.source: cardItem
                                    Drag.dragType: Drag.Internal
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: 16

                                    DragHandler {
                                        id: cardDrag
                                        onActiveChanged: if (!active) cardItem.Drag.drop()
                                    }

                                    states: State {
                                        when: cardDrag.active
                                        ParentChange { target: cardItem; parent: root }
                                        PropertyChanges { cardItem.z: 100 }
                                    }

                                    // Drop onto this card inserts the dragged
                                    // card immediately before it. Disabled while
                                    // this card is itself the drag source so it
                                    // never targets its own slot.
                                    DropArea {
                                        anchors.fill: parent
                                        enabled: !cardDrag.active
                                        onDropped: function(drop) {
                                            if (!drop.source || drop.source.cardColIndex === undefined) return
                                            root.writeBoard(KanbanTools.moveCard(root.content,
                                                drop.source.cardColIndex, drop.source.cardIndex,
                                                cardItem.cardColIndex, cardItem.cardIndex))
                                            drop.accept()
                                        }
                                    }

                                    Column {
                                        id: cardCol
                                        anchors.left: parent.left; anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 6
                                        spacing: 3
                                        Row {
                                            width: parent.width
                                            spacing: 6
                                            Rectangle {
                                                width: 14; height: 14; radius: 3
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: cardItem.cardData.done ? Theme.accent : "transparent"
                                                border.color: cardItem.cardData.done ? Theme.accent : Theme.borderStrong
                                                border.width: 1.5
                                                Text { anchors.centerIn: parent; visible: cardItem.cardData.done
                                                    text: "✓"; color: Theme.onAccent; font.pixelSize: 9 }
                                                TapHandler {
                                                    onTapped: root.writeBoard(KanbanTools.toggleCardDone(
                                                        root.content, cardItem.cardColIndex, cardItem.cardIndex))
                                                }
                                            }
                                            Text {
                                                width: parent.width - 20
                                                text: cardItem.cardData.title
                                                wrapMode: Text.Wrap
                                                font.pixelSize: 12
                                                font.strikeout: cardItem.cardData.done
                                                color: cardItem.cardData.done ? Theme.textFaint : Theme.textPrimary
                                            }
                                        }
                                        // Labels + due date row.
                                        Flow {
                                            width: parent.width
                                            spacing: 4
                                            visible: cardItem.cardData.labels.length > 0
                                                     || cardItem.cardData.due !== ""
                                            Repeater {
                                                model: cardItem.cardData.labels
                                                delegate: Rectangle {
                                                    required property var modelData
                                                    height: 16; radius: 8
                                                    width: lblT.implicitWidth + 12
                                                    color: Qt.alpha(root.labelColor(modelData), 0.2)
                                                    Text { id: lblT; anchors.centerIn: parent
                                                        text: "#" + modelData; font.pixelSize: 9
                                                        color: root.labelColor(modelData) }
                                                }
                                            }
                                            Text {
                                                visible: cardItem.cardData.due !== ""
                                                text: "◷ " + cardItem.cardData.due
                                                font.pixelSize: 9; color: Theme.textMuted
                                            }
                                        }
                                    }

                                    TapHandler {
                                        acceptedButtons: Qt.LeftButton
                                        onDoubleTapped: cardEditor.openFor(
                                            cardItem.cardColIndex, cardItem.cardIndex)
                                    }
                                }
                            }
                        }
                    }
                }
                // Add-column affordance.
                Rectangle {
                    width: 120; height: 40; radius: 6
                    color: "transparent"; border.width: 1; border.color: Theme.border
                    Text { anchors.centerIn: parent; text: "+ Column"; color: Theme.textMuted; font.pixelSize: 12 }
                    TapHandler {
                        onTapped: root.writeBoard(KanbanTools.addColumn(root.content, "New column"))
                    }
                }
            }
        }
    }

    // The card editor popover.
    Popup {
        id: cardEditor
        objectName: "kanbanCardEditor"
        anchors.centerIn: Overlay.overlay
        width: 320
        modal: true
        focus: true
        padding: 12
        property int col: -1
        property int idx: -1
        background: Rectangle { color: Theme.popupBackground; border.color: Theme.borderStrong; border.width: 1; radius: 8 }
        function openFor(c, i) {
            col = c; idx = i
            var card = root.columns[c].cards[i]
            titleField.text = card.title
            doneBox.checked = card.done
            labelsField.text = card.labels.join(", ")
            dueField.text = card.due
            descField.text = card.description
            open()
        }
        function save() {
            var labels = labelsField.text.split(",").map(function(s){return s.trim()}).filter(function(s){return s.length})
            root.writeBoard(KanbanTools.setCard(root.content, col, idx,
                titleField.text.trim(), doneBox.checked, labels, dueField.text.trim(), descField.text.trim()))
            close()
        }
        // Move the card to the end of another column. Dragging is the primary
        // way to move a card; this keyboard/click path guarantees the same
        // reordering without a pointer gesture (accessibility and headless
        // reach), as one undo step.
        function moveToColumn(targetCol) {
            var destCount = root.columns[targetCol].cards.length
            root.writeBoard(KanbanTools.moveCard(root.content, col, idx, targetCol, destCount))
            close()
        }
        contentItem: Column {
            spacing: 6
            Text { text: qsTr("Edit card"); font.bold: true; color: Theme.textPrimary }
            TextField { id: titleField; width: parent.width; placeholderText: qsTr("Title") }
            CheckBox { id: doneBox; text: qsTr("Done") }
            TextField { id: labelsField; width: parent.width; placeholderText: qsTr("Labels (comma separated)") }
            TextField { id: dueField; width: parent.width; placeholderText: qsTr("Due date (YYYY-MM-DD)") }
            TextArea { id: descField; width: parent.width; placeholderText: qsTr("Description")
                background: Rectangle { border.color: Theme.border; border.width: 1; radius: 3 } }
            Text { text: qsTr("Move to column"); font.pixelSize: 11; color: Theme.textMuted }
            Flow {
                width: parent.width
                spacing: 4
                Repeater {
                    model: root.columns.length
                    delegate: Button {
                        required property int index
                        objectName: "kanbanMoveTo"
                        enabled: index !== cardEditor.col
                        text: root.columns[index].name
                        onClicked: cardEditor.moveToColumn(index)
                    }
                }
            }
            Row {
                spacing: 6
                Button { text: qsTr("Save"); onClicked: cardEditor.save() }
                Button { text: qsTr("Delete card"); onClicked: {
                    root.writeBoard(KanbanTools.removeCard(root.content, cardEditor.col, cardEditor.idx))
                    cardEditor.close() } }
                Button { text: qsTr("Cancel"); onClicked: cardEditor.close() }
            }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    // Gutter plus-button + drag handle.
    Rectangle {
        objectName: "plusButton"
        width: 18; height: 18; x: 10; y: 8; radius: 4
        color: plusArea.containsMouse ? Theme.hoverTint : "transparent"
        opacity: root.isHovered ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Text { anchors.centerIn: parent; text: "+"; color: Theme.textMuted; font.pixelSize: 14; font.bold: true }
        MouseArea { id: plusArea; anchors.fill: parent; anchors.margins: -2
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: root.insertBlockBelowAndOpenMenu() }
    }
    Item {
        objectName: "kanbanHandle"
        width: 14; height: 18; x: 30; y: 8
        opacity: root.isHovered || kbHandle.pressed ? 0.6 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Column { anchors.centerIn: parent; spacing: 2
            Repeater { model: 2; Row { spacing: 2; Repeater { model: 2
                Rectangle { width: 3; height: 3; radius: 1.5; color: Theme.textFaint } } } } }
        MouseArea {
            id: kbHandle
            objectName: "dragHandle"
            anchors.fill: parent; anchors.margins: -2
            hoverEnabled: true; cursorShape: Qt.OpenHandCursor; preventStealing: true
            property real pressX: 0; property real pressY: 0; property bool dragging: false
            onPressed: function(mouse) { pressX = mouse.x; pressY = mouse.y; dragging = false }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                var win = Window.window
                if (!win || !win.blockDrag) return
                var sp = kbHandle.mapToItem(null, mouse.x, mouse.y)
                if (!dragging) {
                    if (Math.abs(mouse.x - pressX) < 5 && Math.abs(mouse.y - pressY) < 5) return
                    dragging = true; win.blockDrag.begin(root.index, sp.x, sp.y)
                } else { win.blockDrag.update(sp.x, sp.y) }
            }
            onReleased: {
                var win = Window.window
                if (dragging) { dragging = false; if (win && win.blockDrag) win.blockDrag.drop(); return }
                if (root.listView) root.listView.currentIndex = root.index
                DocumentSelection.selectBlock(root.index)
                root.focusSelectionHandler()
            }
            onCanceled: { if (dragging) { dragging = false; var win = Window.window
                if (win && win.blockDrag) win.blockDrag.cancel() } }
        }
    }
}
