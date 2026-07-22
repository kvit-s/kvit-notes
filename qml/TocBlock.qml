// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The heading rows nest Texts and handlers, each its own scope.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Kvit 1.0

// Table-of-contents block (features.md §17.2): a
// `toc`-tagged code fence rendered read-only as a clickable list of the
// document's headings. The list is a live projection of DocumentOutline —
// it regenerates as headings change — and clicking an entry scrolls to that
// heading. Editing is regeneration, not free text, so this delegate carries
// the block focus/selection/drag API (like DividerDelegate) but no editor.
BlockDelegateBase {
    id: delegate

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

    property int blockIndex: index
    property bool isPooled: false
    property ListView listView: ListView.view
    property bool isFocused: focusTarget.activeFocus
    property bool isHovered: hoverArea.containsMouse

    // Live heading list; re-read only when the outline's heading projection
    // changes, not when outline panel state changes.
    property var headings: {
        var r = DocumentOutline.slugsRevision // dependency only
        return DocumentOutline.headings()
    }
    readonly property int minLevel: {
        var m = 6
        for (var i = 0; i < headings.length; i++)
            m = Math.min(m, headings[i].level)
        return headings.length > 0 ? m : 1
    }

    // A table of contents is drawn inline and in full, and every entry is an
    // Item, a Text and a MouseArea. A heavily sectioned document has hundreds
    // of headings, and a note can hold several TOC blocks, each rendering its
    // own copy of the same projection. Only a window is built; the rest are
    // one button away.
    readonly property int entryWindowStep: 100
    property int revealedEntries: entryWindowStep
    readonly property int renderedEntries:
        Math.min(headings.length, revealedEntries)
    readonly property int hiddenEntries:
        Math.max(0, headings.length - renderedEntries)
    function revealAllEntries() { revealedEntries = headings.length }

    readonly property bool blockSelected: {
        var revision = DocumentSelection.revision // dependency only
        return DocumentSelection.isBlockSelected(delegate.index)
            || DocumentSelection.portionForBlock(delegate.index).selected === true
    }

    // Cross-block position helpers (a TOC has no text: a single position 0).
    function markdownPositionAt(sceneX, sceneY) { return 0 }
    function pointInText(sceneX, sceneY) { return false }
    function lineStepPosition(mdPos, dir) { return -1 }
    function entryPositionAtX(x, fromTop) { return 0 }
    function xAtMarkdown(mdPos) { return 0 }

    readonly property bool isDragSource: {
        if (!delegate.shell || !delegate.shell.blockDrag || !delegate.shell.blockDrag.active)
            return false
        return delegate.shell.blockDrag.isMulti ? delegate.blockSelected
                                     : delegate.shell.blockDrag.sourceIndex === delegate.index
    }

    function focusSelectionHandler() {
        AppActions.requestSelectionFocus()
    }

    onIsFocusedChanged: {
        if (isFocused) {
            if (delegate.shell && delegate.shell.lastFocusedBlock !== undefined)
                delegate.shell.lastFocusedBlock = index
        }
    }

    implicitHeight: card.implicitHeight + 12

    ListView.onPooled: {
        isPooled = true
        focusTarget.focus = false
        opacity = 0
    }
    ListView.onReused: {
        isPooled = false
        opacity = 1
        // A reused delegate is a different block; it must not inherit however
        // far the previous one had been expanded.
        revealedEntries = entryWindowStep
    }

    function focusAtStart() { focusTarget.forceActiveFocus() }
    function focusAtEnd() { focusTarget.forceActiveFocus() }
    function focusAtPosition(markdownPos) { focusTarget.forceActiveFocus() }
    function isCursorOnFirstLine() { return true }
    function isCursorOnLastLine() { return true }

    function deleteCurrentBlock() {
        var prevIndex = delegate.index - 1
        BlockModel.removeBlock(delegate.index)
        Qt.callLater(function() {
            if (listView && prevIndex >= 0) {
                listView.currentIndex = prevIndex
                var item = (listView.itemAtIndex(prevIndex) as BlockDelegateBase)
                if (item) item.focusAtEnd()
            }
        })
    }

    function createBlockBelow() {
        var newIndex = delegate.index + 1
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
        var newIndex = delegate.index + 1
        BlockModel.insertBlock(newIndex, 0, "")
        var lv = listView
        Qt.callLater(function() {
            if (!lv)
                return
            lv.currentIndex = newIndex
            var item = (lv.itemAtIndex(newIndex) as BlockDelegateBase)
            if (item) {
                item.focusAtStart()
                if (item.openBlockMenu)
                    item.openBlockMenu("insert")
            }
        })
    }

    Item {
        id: focusTarget
        objectName: "tocFocusItem"
        anchors.fill: parent
        activeFocusOnTab: true

        Keys.onPressed: function(event) {
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && (event.modifiers & Qt.ControlModifier)
                && (event.modifiers & Qt.ShiftModifier)) {
                if (delegate.listView)
                    delegate.listView.currentIndex = delegate.index
                DocumentSelection.selectBlock(delegate.index)
                delegate.focusSelectionHandler()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                DocumentSelection.selectAllBlocks()
                delegate.focusSelectionHandler()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_D && (event.modifiers & Qt.ControlModifier)
                && (event.modifiers & Qt.ShiftModifier)) {
                delegate.deleteCurrentBlock()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Up && delegate.index > 0 && delegate.listView) {
                var prevIndex = delegate.index - 1
                delegate.listView.currentIndex = prevIndex
                var prev = (delegate.listView.itemAtIndex(prevIndex) as BlockDelegateBase)
                if (prev) prev.focusAtEnd()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Down && delegate.index < BlockModel.count - 1
                && delegate.listView) {
                var nextIndex = delegate.index + 1
                delegate.listView.currentIndex = nextIndex
                var next = (delegate.listView.itemAtIndex(nextIndex) as BlockDelegateBase)
                if (next) next.focusAtStart()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                delegate.deleteCurrentBlock()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                delegate.createBlockBelow()
                event.accepted = true
                return
            }
        }
    }

    // Selection/focus catcher for the card's empty areas (declared before the
    // card so the per-row click handlers window over it).
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: function(mouse) {
            if (mouse.modifiers & Qt.ControlModifier) {
                DocumentSelection.toggleBlock(delegate.index)
                if (DocumentSelection.hasBlockSelection)
                    delegate.focusSelectionHandler()
                else
                    focusTarget.forceActiveFocus()
                return
            }
            if (mouse.modifiers & Qt.ShiftModifier) {
                var anchor = delegate.shell && delegate.shell.lastFocusedBlock !== undefined
                        ? delegate.shell.lastFocusedBlock : -1
                if (!DocumentSelection.hasBlockSelection
                    && anchor >= 0 && anchor !== delegate.index)
                    DocumentSelection.selectBlock(anchor)
                DocumentSelection.extendBlockSelectionTo(delegate.index)
                delegate.focusSelectionHandler()
                return
            }
            if (DocumentSelection.hasBlockSelection
                || DocumentSelection.hasTextSelection)
                DocumentSelection.clear()
            focusTarget.forceActiveFocus()
        }
    }

    // The TOC card.
    Rectangle {
        id: card
        objectName: "tocCard"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 48
        anchors.rightMargin: 8
        anchors.top: parent.top
        anchors.topMargin: 4
        radius: 6
        color: delegate.blockSelected ? Theme.blockSelectionTint
             : (delegate.isFocused ? Theme.focusTint : Theme.panelBackground)
        border.color: delegate.blockSelected ? Theme.accent : Theme.border
        border.width: 1
        opacity: delegate.isDragSource ? 0.35 : 1
        implicitHeight: cardColumn.implicitHeight + 16

        Column {
            id: cardColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 2

            Text {
                text: qsTr("Contents")
                font.pixelSize: 11
                font.bold: true
                color: Theme.textMuted
                bottomPadding: 2
            }

            Text {
                visible: delegate.headings.length === 0
                text: qsTr("No headings yet.")
                font.pixelSize: 12
                color: Theme.textFaint
            }

            Repeater {
                model: delegate.renderedEntries
                Item {
                    id: headingRow
                    required property int index
                    // The model is a count rather than the list itself, so
                    // the entry is looked up here. An index past the end is
                    // possible for one binding pass while the outline shrinks.
                    readonly property var heading: {
                        var h = delegate.headings[headingRow.index]
                        return h !== undefined ? h
                             : { level: 1, text: "", blockIndex: -1 }
                    }
                    width: cardColumn.width
                    height: entry.implicitHeight + 4

                    Text {
                        id: entry
                        x: (headingRow.heading.level - delegate.minLevel) * 16
                        text: headingRow.heading.text === "" ? qsTr("(untitled)")
                                                    : headingRow.heading.text
                        font.pixelSize: 13
                        color: linkArea.containsMouse ? Theme.accent
                                                      : Theme.link
                        font.underline: linkArea.containsMouse
                    }
                    MouseArea {
                        id: linkArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // No window lookup any more: the request goes to
                        // AppActions, and an unconnected signal is a no-op
                        // exactly as the old `if (window)` guard was.
                        onClicked: {
                            if (headingRow.heading.blockIndex >= 0)
                                AppActions.requestScrollToBlock(
                                    headingRow.heading.blockIndex)
                        }
                    }
                }
            }

            Row {
                objectName: "tocEntryWindowNotice"
                visible: delegate.hiddenEntries > 0
                spacing: 6
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%n more heading(s) not shown", "",
                               delegate.hiddenEntries)
                    font.pixelSize: 11
                    color: Theme.textFaint
                }
                Text {
                    objectName: "tocShowAllEntries"
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Show all")
                    font.pixelSize: 11
                    color: showAllArea.containsMouse ? Theme.accent : Theme.link
                    font.underline: showAllArea.containsMouse
                    MouseArea {
                        id: showAllArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: delegate.revealAllEntries()
                    }
                }
            }
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
        opacity: delegate.isHovered ? 1 : 0
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
            onClicked: delegate.insertBlockBelowAndOpenMenu()
        }
    }

    // Drag handle.
    Item {
        objectName: "tocHandle"
        width: 14
        height: 18
        x: 30
        y: 8
        opacity: delegate.isHovered || tocHandleArea.pressed ? 0.6 : 0
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
            id: tocHandleArea
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
                if (!delegate.shell || !delegate.shell.blockDrag) return
                var sp = tocHandleArea.mapToItem(null, mouse.x, mouse.y)
                if (!dragging) {
                    if (Math.abs(mouse.x - pressX) < 5 && Math.abs(mouse.y - pressY) < 5)
                        return
                    dragging = true
                    delegate.shell.blockDrag.begin(delegate.index, sp.x, sp.y)
                } else {
                    delegate.shell.blockDrag.update(sp.x, sp.y)
                }
            }
            onReleased: {
                if (dragging) {
                    dragging = false
                    if (delegate.shell && delegate.shell.blockDrag) delegate.shell.blockDrag.drop()
                    return
                }
                if (delegate.listView)
                    delegate.listView.currentIndex = delegate.index
                DocumentSelection.selectBlock(delegate.index)
                delegate.focusSelectionHandler()
            }
            onCanceled: {
                if (dragging) {
                    dragging = false
                    if (delegate.shell && delegate.shell.blockDrag) delegate.shell.blockDrag.cancel()
                }
            }
        }
    }
}
