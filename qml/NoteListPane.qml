// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
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

    color: theme.listBackground

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
        appWindow.openNoteByPath(relPath)
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
        color: theme.border
    }

    // ---- Note drag: onto a sidebar folder (move — the whole selection
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
                if (sidebar)
                    sidebar.clearDropHover()
            } else {
                reorderGap = -1
                if (sidebar)
                    sidebar.setDropHover(sceneX, sceneY)
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
                var target = sidebar
                    ? sidebar.folderDropTargetAt(sceneX, sceneY) : ""
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
            if (sidebar)
                sidebar.clearDropHover()
        }
    }

    // Floating proxy while a note row is dragged; lives in the window
    // content item (a positioner parent would override x/y), so it can
    // travel over the sidebar.
    Rectangle {
        id: noteDragProxy
        objectName: "noteDragProxy"
        parent: appWindow ? appWindow.contentItem : noteListPane
        visible: noteDrag.active
        z: 1000
        width: 180
        height: 28
        radius: 4
        color: theme.popupBackground
        border.color: theme.accent
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
            color: theme.accent
            Text {
                anchors.centerIn: parent
                text: noteDrag.dragPaths.length
                color: theme.onAccent
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
                color: theme.textSecondary
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
                onClicked: appWindow.createNoteInCurrentScope()
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
            color: theme.bannerBackground

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
                    color: theme.bannerText
                }

                Repeater {
                    model: recoveryColumn.entries
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label {
                                text: modelData.title
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
                                    .restoreRecoveredNote(modelData.relPath)
                            }
                            ToolButton {
                                objectName: "recoveryDiscardButton"
                                text: qsTr("Discard")
                                font.pixelSize: 10
                                implicitHeight: 22
                                onClicked: NoteCollection.discardRecovery(
                                               modelData.relPath)
                            }
                        }
                        Label {
                            text: modelData.preview
                            font.pixelSize: 10
                            color: theme.bannerText
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
            color: theme.focusTint

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
                width: noteListView.width
                // Content-derived with a floor: font metrics differ per
                // platform (DirectWrite lines run taller than fontconfig's),
                // and a hardcoded height overflows where they are taller.
                height: Math.max(58, noteRowContent.implicitHeight + 14)
                // Screen-reader name/role for each note (§14.2).
                Accessible.role: Accessible.ListItem
                Accessible.name: model.title
                Accessible.selected: appWindow
                    && appWindow.currentNoteRelPath === model.relPath
                color: {
                    if (noteListPane.isSelected(model.relPath)
                        && noteListPane.selectedPaths.length > 1)
                        return theme.selectionActiveTint
                    if (appWindow
                        && appWindow.currentNoteRelPath === model.relPath)
                        return theme.selectionTint
                    return noteRowHover.hovered ? theme.hoverTint : "transparent"
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
                            visible: model.pinned
                            text: "⚑"
                            font.pixelSize: 11
                            color: theme.accent
                        }
                        Label {
                            visible: model.favorite
                            text: "★"
                            font.pixelSize: 11
                            color: theme.pinColor
                        }
                        Label {
                            visible: noteListPane.renamingPath !== model.relPath
                            text: model.title
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
                            visible: noteListPane.renamingPath === model.relPath
                            Layout.fillWidth: true
                            font.pixelSize: 12
                            implicitHeight: 22

                            onVisibleChanged: {
                                if (visible) {
                                    text = model.title
                                    forceActiveFocus()
                                    selectAll()
                                }
                            }
                            onAccepted: {
                                var path = model.relPath
                                noteListPane.renamingPath = ""
                                if (text !== model.title)
                                    noteListPane.appWindow.requestNoteRename(path, text)
                            }
                            Keys.onEscapePressed: noteListPane.renamingPath = ""
                            onActiveFocusChanged: {
                                if (!activeFocus
                                    && noteListPane.renamingPath === model.relPath)
                                    noteListPane.renamingPath = ""
                            }
                        }

                    }
                    Label {
                        text: model.snippet !== "" ? model.snippet
                                                   : qsTr("Empty note")
                        font.pixelSize: 11
                        color: theme.textFaint
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        objectName: "noteDetailsLabel"
                        // The date shown follows the sort: created dates
                        // under the created sort, modified otherwise.
                        text: Qt.formatDateTime(
                                  NoteListModel.sortMode === "created"
                                      ? model.created : model.modified,
                                  "MMM d, yyyy hh:mm")
                              + " · " + model.wordCount + " "
                              + qsTr("words")
                        font.pixelSize: 10
                        color: theme.textDisabled
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
                        opacity: model.pinned ? 1.0 : 0.4
                        font.pixelSize: 10
                        implicitWidth: 22
                        implicitHeight: 22
                        ToolTip.visible: hovered
                        ToolTip.text: model.pinned ? qsTr("Unpin")
                                                   : qsTr("Pin to top")
                        onClicked: NoteCollection.setPinned(
                                       model.relPath, !model.pinned)
                    }
                    ToolButton {
                        objectName: "noteFavoriteButton"
                        text: model.favorite ? "★" : "☆"
                        font.pixelSize: 11
                        implicitWidth: 22
                        implicitHeight: 22
                        ToolTip.visible: hovered
                        ToolTip.text: model.favorite
                                      ? qsTr("Remove from favorites")
                                      : qsTr("Add to favorites")
                        onClicked: NoteCollection.setFavorite(
                                       model.relPath, !model.favorite)
                    }
                }

                Rectangle { // separator
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: theme.hoverTint
                }

                MouseArea {
                    id: noteRowArea
                    anchors.fill: parent
                    // Leave the hover toggles clickable: they span the
                    // last ~48px plus the 12px content margin.
                    anchors.rightMargin: noteRowHover.hovered ? 64 : 0
                    visible: noteListPane.renamingPath !== model.relPath

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
                            noteDrag.begin(model.relPath, model.title)
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
                            noteListPane.selectionClick(model.relPath,
                                                        mouse.modifiers)
                    }
                    onDoubleClicked: noteListPane.startRename(model.relPath)
                }

                // §9.5 note context menu (right button passes through
                // the left-button MouseArea above).
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: noteContextMenu.openFor(
                        model.relPath, model.pinned, model.favorite)
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
                color: theme.accent
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
            onTriggered: appWindow.openNoteByPath(noteContextMenu.relPath)
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
        enabled: noteListPane.visible && appWindow
                 && appWindow.currentNoteRelPath !== ""
        onActivated: noteListPane.startRename(appWindow.currentNoteRelPath)
    }
}
