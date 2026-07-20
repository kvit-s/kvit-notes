// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The note rows, the recovery entries and the folder menu are each a
// delegate whose contents are separate scopes. Binding them means each
// declares the model roles it reads and addresses them by id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// The note list: rows of the NoteListModel projection with sort controls,
// pin/favorite toggles, bulk selection with its action bar, inline rename,
// drag-to-folder, and manual-order drag when sorting manually inside a
// folder.
Rectangle {
    id: noteListPane
    objectName: "noteListPane"

    color: Theme.listBackground

    // Wired by main.qml.
    property var appWindow
    property var sidebar

    // Pane focus entry (§14.1 tab order): land on the note list.
    function focusPane() {
        noteListView.forceActiveFocus()
    }

    // relPath of the row currently in inline rename ("" = none).
    property string renamingPath: ""

    // While a global-search query is active the results view replaces
    // the note rows.
    readonly property bool searching: CollectionSearch.query !== ""

    // Bulk selection (features.md §8.3): relPaths, gestured like the block
    // selection — Click selects (and opens), Ctrl+Click toggles,
    // Shift+Click ranges from the last plain-clicked row.
    property var selectedPaths: []
    property string selectionAnchor: ""

    function isSelected(relPath) {
        return selectedPaths.indexOf(relPath) !== -1
    }

    function clearSelection() {
        selectedPaths = []
        selectionAnchor = ""
    }

    function selectionClick(relPath, modifiers) {
        if (modifiers & Qt.ControlModifier) {
            var toggled = selectedPaths.slice()
            var at = toggled.indexOf(relPath)
            if (at >= 0)
                toggled.splice(at, 1)
            else
                toggled.push(relPath)
            selectedPaths = toggled
            if (selectionAnchor === "")
                selectionAnchor = relPath
            return
        }
        if ((modifiers & Qt.ShiftModifier) && selectionAnchor !== "") {
            var from = NoteListModel.rowOf(selectionAnchor)
            var to = NoteListModel.rowOf(relPath)
            if (from >= 0 && to >= 0) {
                var range = []
                var step = from <= to ? 1 : -1
                for (var i = from; i !== to + step; i += step)
                    range.push(NoteListModel.relPathAt(i))
                selectedPaths = range
            }
            return
        }
        selectedPaths = [relPath]
        selectionAnchor = relPath
        noteListPane.appWindow.openNoteByPath(relPath)
    }

    function startRename(relPath) {
        renamingPath = relPath
        var row = NoteListModel.rowOf(relPath)
        if (row >= 0)
            noteListView.positionViewAtIndex(row, ListView.Contain)
    }

    // Drop stale paths after any collection change.
    Connections {
        target: NoteCollection
        function onRevisionChanged() {
            if (noteListPane.selectedPaths.length === 0)
                return
            var alive = noteListPane.selectedPaths.filter(function(p) {
                return NoteListModel.rowOf(p) !== -1
            })
            if (alive.length !== noteListPane.selectedPaths.length)
                noteListPane.selectedPaths = alive
        }
    }

    Rectangle { // right edge
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    // ---- Note drag: onto a noteListPane.sidebar folder (move — the whole selection
    // when a selected row is dragged), or within the list to reorder
    // when the manual sort is active ------------------------------------
    QtObject {
        id: noteDrag
        property bool active: false
        property string relPath: ""
        property string title: ""
        property var dragPaths: []
        // Manual-sort reorder: the insertion gap under the pointer
        // (row index the drop lands BEFORE; -1 = not reordering).
        property int reorderGap: -1

        readonly property bool reorderEnabled:
            NoteListModel.scope === "folder"
            && NoteListModel.sortMode === "manual"

        function begin(path, name) {
            relPath = path
            title = name
            dragPaths = noteListPane.isSelected(path)
                ? noteListPane.selectedPaths.slice() : [path]
            reorderGap = -1
            active = true
        }
        function update(sceneX, sceneY) {
            if (!active)
                return
            noteDragProxy.x = sceneX + 10
            noteDragProxy.y = sceneY - 10
            var local = noteListView.mapFromItem(null, sceneX, sceneY)
            var inList = local.x >= 0 && local.x < noteListView.width
                         && local.y >= 0 && local.y < noteListView.height
            if (inList && reorderEnabled && dragPaths.length === 1) {
                reorderGap = gapAt(local.y + noteListView.contentY)
                if (noteListPane.sidebar)
                    noteListPane.sidebar.clearDropHover()
            } else {
                reorderGap = -1
                if (noteListPane.sidebar)
                    noteListPane.sidebar.setDropHover(sceneX, sceneY)
            }
        }
        function gapAt(contentY) {
            if (contentY >= noteListView.contentHeight)
                return NoteListModel.count
            var idx = noteListView.indexAt(10, Math.max(0, contentY))
            if (idx < 0)
                return NoteListModel.count
            var item = noteListView.itemAtIndex(idx)
            if (item && contentY > item.y + item.height / 2)
                return idx + 1
            return idx
        }
        function drop(sceneX, sceneY) {
            if (!active)
                return
            if (reorderGap >= 0) {
                // Positions after removal: dropping below itself shifts
                // the target up by one.
                var from = NoteListModel.rowOf(relPath)
                var position = reorderGap > from ? reorderGap - 1 : reorderGap
                NoteCollection.setManualPosition(relPath, position)
            } else {
                var target = noteListPane.sidebar
                    ? noteListPane.sidebar.folderDropTargetAt(sceneX, sceneY) : ""
                if (target !== "") {
                    for (var i = 0; i < dragPaths.length; i++)
                        NoteCollection.moveNote(dragPaths[i], target)
                }
            }
            end()
        }
        function end() {
            active = false
            relPath = ""
            dragPaths = []
            reorderGap = -1
            if (noteListPane.sidebar)
                noteListPane.sidebar.clearDropHover()
        }
    }

    // Floating proxy while a note row is dragged; lives in the window
    // content item (a positioner parent would override x/y), so it can
    // travel over the noteListPane.sidebar.
    Rectangle {
        id: noteDragProxy
        objectName: "noteDragProxy"
        parent: noteListPane.appWindow ? noteListPane.appWindow.contentItem : noteListPane
        visible: noteDrag.active
        z: 1000
        width: 180
        height: 28
        radius: 4
        color: Theme.popupBackground
        border.color: Theme.accent
        opacity: 0.9
        Label {
            anchors.fill: parent
            anchors.margins: 6
            text: noteDrag.title
            font.pixelSize: 11
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        Rectangle {
            visible: noteDrag.dragPaths.length > 1
            anchors.left: parent.right
            anchors.top: parent.top
            anchors.leftMargin: -10
            anchors.topMargin: -8
            width: 20
            height: 20
            radius: 10
            color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: noteDrag.dragPaths.length
                color: Theme.onAccent
                font.pixelSize: 10
                font.bold: true
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0

        // ---- Header -----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            Layout.bottomMargin: 4
            spacing: 4

            Label {
                objectName: "noteListScopeLabel"
                text: {
                    if (NoteListModel.scope === "favorites")
                        return qsTr("Favorites")
                    if (NoteListModel.scope === "folder") {
                        var path = NoteListModel.folderPath
                        if (path === "")
                            return qsTr("Notes") // the collection root
                        var slash = path.lastIndexOf("/")
                        return slash < 0 ? path : path.substring(slash + 1)
                    }
                    return qsTr("All Notes")
                }
                font.pixelSize: 13
                font.bold: true
                color: Theme.textSecondary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            ToolButton {
                objectName: "newNoteButton"
                text: "+"
                font.pixelSize: 14
                implicitHeight: 24
                implicitWidth: 26
                ToolTip.visible: hovered
                ToolTip.text: qsTr("New note (Ctrl+N)")
                onClicked: noteListPane.appWindow.createNoteInCurrentScope()
            }
        }

        // ---- Sort controls (§8.3: four sorts, both directions) ----------
        RowLayout {
            visible: !noteListPane.searching
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 4
            spacing: 4

            ComboBox {
                id: sortModeCombo
                objectName: "sortModeCombo"
                Layout.fillWidth: true
                implicitHeight: 24
                font.pixelSize: 11
                // Values parallel to the display strings.
                readonly property var modes:
                    ["modified", "created", "title", "manual"]
                model: [qsTr("Modified"), qsTr("Created"), qsTr("Title"),
                        qsTr("Manual")]
                currentIndex: Math.max(0,
                    modes.indexOf(NoteListModel.sortMode))
                onActivated: function(index) {
                    NoteListModel.sortMode = modes[index]
                    if (modes[index] === "manual")
                        NoteListModel.ascending = true // the stored order
                }
            }
            ToolButton {
                objectName: "sortDirectionButton"
                text: NoteListModel.ascending ? "↑" : "↓"
                font.pixelSize: 12
                implicitHeight: 24
                implicitWidth: 24
                ToolTip.visible: hovered
                ToolTip.text: NoteListModel.ascending
                              ? qsTr("Ascending") : qsTr("Descending")
                onClicked: NoteListModel.ascending = !NoteListModel.ascending
            }
            ToolButton {
                objectName: "noteListCollapseButton"
                text: "«"
                font.pixelSize: 12
                implicitHeight: 24
                implicitWidth: 22
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Collapse note list")
                onClicked: if (noteListPane.appWindow)
                               noteListPane.appWindow.noteListCollapsed = true
            }
        }

        // ---- Crash-recovery banner: journal files found when the root
        // opened are unsaved changes from a crashed session; each offers
        // Restore or Discard, with the journal's content previewed
        // inline. -------------------------------------------------------
        Rectangle {
            objectName: "recoveryBanner"
            Layout.fillWidth: true
            visible: recoveryColumn.entries.length > 0
            implicitHeight: visible ? recoveryColumn.implicitHeight + 12 : 0
            color: Theme.bannerBackground

            ColumnLayout {
                id: recoveryColumn
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4

                readonly property var entries: {
                    var revision = NoteCollection.revision
                    return NoteCollection.isOpen
                           ? NoteCollection.recoveryEntries() : []
                }

                Label {
                    text: qsTr("Recovered unsaved changes")
                    font.pixelSize: 11
                    font.bold: true
                    color: Theme.bannerText
                }

                Repeater {
                    model: recoveryColumn.entries
                    delegate: ColumnLayout {
                        id: recoveryEntry
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label {
                                text: recoveryEntry.modelData.title
                                font.pixelSize: 11
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            ToolButton {
                                objectName: "recoveryRestoreButton"
                                text: qsTr("Restore")
                                font.pixelSize: 10
                                implicitHeight: 22
                                onClicked: noteListPane.appWindow
                                    .restoreRecoveredNote(recoveryEntry.modelData.relPath)
                            }
                            ToolButton {
                                objectName: "recoveryDiscardButton"
                                text: qsTr("Discard")
                                font.pixelSize: 10
                                implicitHeight: 22
                                onClicked: NoteCollection.discardRecovery(
                                               recoveryEntry.modelData.relPath)
                            }
                        }
                        Label {
                            text: recoveryEntry.modelData.preview
                            font.pixelSize: 10
                            color: Theme.bannerText
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        // ---- Bulk action bar (§8.3 bulk operations) ---------------------
        Rectangle {
            objectName: "bulkActionBar"
            Layout.fillWidth: true
            visible: noteListPane.selectedPaths.length > 1
                     && !noteListPane.searching
            height: visible ? 30 : 0
            color: Theme.focusTint

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 2

                Label {
                    objectName: "bulkCountLabel"
                    text: qsTr("%1 selected")
                          .arg(noteListPane.selectedPaths.length)
                    font.pixelSize: 11
                    Layout.fillWidth: true
                }
                ToolButton {
                    objectName: "bulkPinButton"
                    text: "⚑"
                    font.pixelSize: 10
                    implicitHeight: 24
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Pin")
                    onClicked: {
                        var paths = noteListPane.selectedPaths
                        for (var i = 0; i < paths.length; i++)
                            NoteCollection.setPinned(paths[i], true)
                    }
                }
                ToolButton {
                    objectName: "bulkFavoriteButton"
                    text: "★"
                    font.pixelSize: 11
                    implicitHeight: 24
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Favorite")
                    onClicked: {
                        var paths = noteListPane.selectedPaths
                        for (var i = 0; i < paths.length; i++)
                            NoteCollection.setFavorite(paths[i], true)
                    }
                }
                ToolButton {
                    objectName: "bulkTagButton"
                    text: "#"
                    font.pixelSize: 11
                    implicitHeight: 24
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Add tag")
                    onClicked: bulkTagDialog.openFor(
                                   noteListPane.selectedPaths)
                }
                ToolButton {
                    objectName: "bulkDeleteButton"
                    text: "✖"
                    font.pixelSize: 10
                    implicitHeight: 24
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Delete")
                    onClicked: bulkDeleteDialog.openFor(
                                   noteListPane.selectedPaths)
                }
                ToolButton {
                    objectName: "bulkClearButton"
                    text: "✕"
                    font.pixelSize: 10
                    implicitHeight: 24
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Clear selection")
                    onClicked: noteListPane.clearSelection()
                }
            }
        }

        // ---- Global-search results (replace the rows while active) ------
        SearchResultsView {
            visible: noteListPane.searching
            Layout.fillWidth: true
            Layout.fillHeight: true
            appWindow: noteListPane.appWindow
        }

        // ---- Rows -------------------------------------------------------
        ListView {
            id: noteListView
            objectName: "noteListView"
            visible: !noteListPane.searching
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: NoteListModel

            delegate: Rectangle {
                id: noteRow
                // NoteListModel's roles, declared rather than injected, so
                // the nested rows and handlers read them through noteRow.
                required property int index
                required property string relPath
                required property string title
                required property string snippet
                required property string modified
                required property string created
                required property int wordCount
                required property bool pinned
                required property bool favorite
                required property var tags
                width: noteListView.width
                // Content-derived with a floor: font metrics differ per
                // platform (DirectWrite lines run taller than fontconfig's),
                // and a hardcoded height overflows where they are taller.
                height: Math.max(58, noteRowContent.implicitHeight + 14)
                // Screen-reader name/role for each note (§14.2).
                Accessible.role: Accessible.ListItem
                Accessible.name: noteRow.title
                Accessible.selected: noteListPane.appWindow
                    && noteListPane.appWindow.currentNoteRelPath === noteRow.relPath
                color: {
                    if (noteListPane.isSelected(noteRow.relPath)
                        && noteListPane.selectedPaths.length > 1)
                        return Theme.selectionActiveTint
                    if (noteListPane.appWindow
                        && noteListPane.appWindow.currentNoteRelPath === noteRow.relPath)
                        return Theme.selectionTint
                    return noteRowHover.hovered ? Theme.hoverTint : "transparent"
                }

                HoverHandler { id: noteRowHover }

                ColumnLayout {
                    id: noteRowContent
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.topMargin: 7
                    anchors.bottomMargin: 7
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            visible: noteRow.pinned
                            text: "⚑"
                            font.pixelSize: 11
                            color: Theme.accent
                        }
                        Label {
                            visible: noteRow.favorite
                            text: "★"
                            font.pixelSize: 11
                            color: Theme.pinColor
                        }
                        Label {
                            visible: noteListPane.renamingPath !== noteRow.relPath
                            text: noteRow.title
                            font.pixelSize: 12
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            // The hover toggles overlay the top-right corner
                            // (outside the layout, so revealing them cannot
                            // change the row's height); reserve their width
                            // so the title elides instead of underlapping.
                            Layout.rightMargin: noteRowToggles.visible ? 48 : 0
                        }
                        TextField {
                            id: renameField
                            objectName: "noteRenameField"
                            visible: noteListPane.renamingPath === noteRow.relPath
                            Layout.fillWidth: true
                            font.pixelSize: 12
                            implicitHeight: 22

                            onVisibleChanged: {
                                if (visible) {
                                    text = noteRow.title
                                    forceActiveFocus()
                                    selectAll()
                                }
                            }
                            onAccepted: {
                                var path = noteRow.relPath
                                noteListPane.renamingPath = ""
                                if (text !== noteRow.title)
                                    noteListPane.appWindow.requestNoteRename(path, text)
                            }
                            Keys.onEscapePressed: noteListPane.renamingPath = ""
                            onActiveFocusChanged: {
                                if (!activeFocus
                                    && noteListPane.renamingPath === noteRow.relPath)
                                    noteListPane.renamingPath = ""
                            }
                        }

                    }
                    Label {
                        text: noteRow.snippet !== "" ? noteRow.snippet
                                                   : qsTr("Empty note")
                        font.pixelSize: 11
                        color: Theme.textFaint
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        objectName: "noteDetailsLabel"
                        // The date shown follows the sort: created dates
                        // under the created sort, modified otherwise.
                        text: Qt.formatDateTime(
                                  NoteListModel.sortMode === "created"
                                      ? noteRow.created : noteRow.modified,
                                  "MMM d, yyyy hh:mm")
                              + " · " + noteRow.wordCount + " "
                              + qsTr("words")
                        font.pixelSize: 10
                        color: Theme.textDisabled
                    }
                }

                // Hover toggles (features.md §8.3 pin / star). Overlaid on
                // the top-right corner rather than participating in the title
                // row, so their taller click targets never change the row's
                // layout height on hover.
                Row {
                    id: noteRowToggles
                    visible: noteRowHover.hovered
                             && noteListPane.renamingPath === ""
                    anchors.top: parent.top
                    anchors.topMargin: 4
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    spacing: 2

                    ToolButton {
                        objectName: "notePinButton"
                        text: "⚑"
                        opacity: noteRow.pinned ? 1.0 : 0.4
                        font.pixelSize: 10
                        implicitWidth: 22
                        implicitHeight: 22
                        ToolTip.visible: hovered
                        ToolTip.text: noteRow.pinned ? qsTr("Unpin")
                                                   : qsTr("Pin to top")
                        onClicked: NoteCollection.setPinned(
                                       noteRow.relPath, !noteRow.pinned)
                    }
                    ToolButton {
                        objectName: "noteFavoriteButton"
                        text: noteRow.favorite ? "★" : "☆"
                        font.pixelSize: 11
                        implicitWidth: 22
                        implicitHeight: 22
                        ToolTip.visible: hovered
                        ToolTip.text: noteRow.favorite
                                      ? qsTr("Remove from favorites")
                                      : qsTr("Add to favorites")
                        onClicked: NoteCollection.setFavorite(
                                       noteRow.relPath, !noteRow.favorite)
                    }
                }

                Rectangle { // separator
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.hoverTint
                }

                MouseArea {
                    id: noteRowArea
                    anchors.fill: parent
                    // Leave the hover toggles clickable: they span the
                    // last ~48px plus the 12px content margin.
                    anchors.rightMargin: noteRowHover.hovered ? 64 : 0
                    visible: noteListPane.renamingPath !== noteRow.relPath

                    property real pressSceneX: 0
                    property real pressSceneY: 0
                    property bool dragging: false

                    onPressed: function(mouse) {
                        var scene = mapToItem(null, mouse.x, mouse.y)
                        pressSceneX = scene.x
                        pressSceneY = scene.y
                        dragging = false
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        var scene = mapToItem(null, mouse.x, mouse.y)
                        if (!dragging
                            && Math.abs(scene.x - pressSceneX)
                               + Math.abs(scene.y - pressSceneY) > 8) {
                            dragging = true
                            noteDrag.begin(noteRow.relPath, noteRow.title)
                        }
                        if (dragging)
                            noteDrag.update(scene.x, scene.y)
                    }
                    onReleased: function(mouse) {
                        if (dragging) {
                            var scene = mapToItem(null, mouse.x, mouse.y)
                            noteDrag.drop(scene.x, scene.y)
                            dragging = false
                        }
                    }
                    onCanceled: {
                        noteDrag.end()
                        dragging = false
                    }
                    onClicked: function(mouse) {
                        if (!dragging)
                            noteListPane.selectionClick(noteRow.relPath,
                                                        mouse.modifiers)
                    }
                    onDoubleClicked: noteListPane.startRename(noteRow.relPath)
                }

                // §9.5 note context menu (right button passes through
                // the left-button MouseArea above).
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: noteContextMenu.openFor(
                        noteRow.relPath, noteRow.pinned, noteRow.favorite)
                }
            }

            // Manual-order insertion line.
            Rectangle {
                objectName: "reorderIndicator"
                visible: noteDrag.active && noteDrag.reorderGap >= 0
                x: 6
                width: noteListView.width - 12
                height: 3
                radius: 1.5
                color: Theme.accent
                y: {
                    var gap = noteDrag.reorderGap
                    if (gap < 0)
                        return 0
                    var yContent = 0
                    if (gap < noteListView.count) {
                        var item = noteListView.itemAtIndex(gap)
                        yContent = item ? item.y : 0
                    } else {
                        var last = noteListView.itemAtIndex(
                            noteListView.count - 1)
                        yContent = last ? last.y + last.height : 0
                    }
                    return yContent - noteListView.contentY - height / 2
                }
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        }
    }

    // ---- Bulk dialogs ----------------------------------------------------
    // ---- §9.5 note context menu -------------------------------------
    Menu {
        id: noteContextMenu
        objectName: "noteContextMenu"
        property string relPath: ""
        property bool notePinned: false
        property bool noteFavorite: false

        function openFor(path, pinned, favorite) {
            relPath = path
            notePinned = pinned
            noteFavorite = favorite
            popup()
        }

        MenuItem {
            objectName: "ctxNoteOpen"
            text: qsTr("Open")
            onTriggered: noteListPane.appWindow.openNoteByPath(noteContextMenu.relPath)
        }
        MenuItem {
            objectName: "ctxNoteRename"
            text: qsTr("Rename")
            onTriggered: noteListPane.startRename(noteContextMenu.relPath)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "ctxNotePin"
            text: noteContextMenu.notePinned ? qsTr("Unpin") : qsTr("Pin")
            onTriggered: NoteCollection.setPinned(
                noteContextMenu.relPath, !noteContextMenu.notePinned)
        }
        MenuItem {
            objectName: "ctxNoteFavorite"
            text: noteContextMenu.noteFavorite
                ? qsTr("Remove from favorites") : qsTr("Add to favorites")
            onTriggered: NoteCollection.setFavorite(
                noteContextMenu.relPath, !noteContextMenu.noteFavorite)
        }
        Menu {
            objectName: "ctxNoteMoveMenu"
            title: qsTr("Move to")
            MenuItem {
                text: qsTr("Notes root")
                onTriggered: noteListPane.appWindow.requestNoteMove(
                    noteContextMenu.relPath, "")
            }
            Repeater {
                model: noteContextMenu.visible
                    ? NoteCollection.folderRelPaths() : []
                MenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: noteListPane.appWindow.requestNoteMove(
                        noteContextMenu.relPath, modelData)
                }
            }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "ctxNoteDelete"
            text: qsTr("Delete…")
            onTriggered: bulkDeleteDialog.openFor([noteContextMenu.relPath])
        }
    }

    Dialog {
        id: bulkDeleteDialog
        objectName: "bulkDeleteDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 320
        title: qsTr("Delete Notes")

        property var targets: []

        function openFor(paths) {
            targets = paths.slice()
            bulkDeleteText.text = qsTr(
                "Delete %1 notes? They move to the trash.").arg(targets.length)
            open()
        }

        onAccepted: {
            for (var i = 0; i < targets.length; i++)
                NoteCollection.deleteNote(targets[i])
            noteListPane.clearSelection()
        }

        contentItem: Label {
            id: bulkDeleteText
            wrapMode: Text.WordWrap
            leftPadding: 12
            rightPadding: 12
            topPadding: 8
            bottomPadding: 8
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    Dialog {
        id: bulkTagDialog
        objectName: "bulkTagDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 300
        title: qsTr("Add Tag")

        property var targets: []

        function openFor(paths) {
            targets = paths.slice()
            bulkTagField.text = ""
            open()
            bulkTagField.forceActiveFocus()
        }

        onAccepted: {
            var tag = bulkTagField.text.trim()
            if (tag === "")
                return
            for (var i = 0; i < targets.length; i++)
                NoteCollection.addTag(targets[i], tag)
        }

        contentItem: TextField {
            id: bulkTagField
            objectName: "bulkTagField"
            placeholderText: qsTr("Tag name")
            onAccepted: bulkTagDialog.accept()
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    // F2 renames the open note (features.md §8.3's rename path).
    Shortcut {
        sequence: "F2"
        enabled: noteListPane.visible && noteListPane.appWindow
                 && noteListPane.appWindow.currentNoteRelPath !== ""
        onActivated: noteListPane.startRename(noteListPane.appWindow.currentNoteRelPath)
    }
}
