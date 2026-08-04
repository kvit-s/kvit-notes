// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Delegates and their nested rows are separate scopes throughout this
// file — the folder tree, the tag list, the recent searches and two
// colour palettes. Binding them means each delegate declares the model
// roles it reads and its contents address them through its id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// The navigation sidebar: scopes, the folder tree, and the tag list.
// Functional Fusion styling only. All state lives in NoteCollection /
// NoteListModel; this pane renders and forwards.
Rectangle {
    id: sidebar
    objectName: "sidebar"

    color: Theme.panelBackground

    // Wired by main.qml (the collapse control writes layout state).
    property var appWindow

    // Highlight target while a note row is dragged over a folder.
    property string dropTargetFolder: ""
    property bool dropTargetActive: false

    // Map a scene point to the folder row under it; "" when none.
    // Used by NoteListPane's drag coordinator.
    function folderDropTargetAt(sceneX, sceneY) {
        var pos = folderTreeView.mapFromItem(null, sceneX, sceneY)
        if (pos.x < 0 || pos.x >= folderTreeView.width
            || pos.y < 0 || pos.y >= folderTreeView.height)
            return ""
        var idx = folderTreeView.indexAt(
            pos.x, pos.y + folderTreeView.contentY)
        return idx < 0 ? "" : FolderTreeModel.relPathAt(idx)
    }

    function setDropHover(sceneX, sceneY) {
        dropTargetFolder = folderDropTargetAt(sceneX, sceneY)
        dropTargetActive = true
        return dropTargetFolder
    }

    function clearDropHover() {
        dropTargetFolder = ""
        dropTargetActive = false
    }

    Rectangle { // right edge
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Interface.px(1)
        color: Theme.border
    }

    // Recent searches (features.md §8.4), persisted through the settings
    // store; read back by applyPersistedSessionState.
    property var recentSearches: []

    function applyPersistedSearchHistory() {
        recentSearches = AppSettings.value("search.recent", [])
    }

    function commitRecentSearch(query) {
        var q = query.trim()
        if (q === "")
            return
        var list = recentSearches.filter(function(item) { return item !== q })
        list.unshift(q)
        recentSearches = list.slice(0, 6)
        AppSettings.setValue("search.recent", recentSearches)
    }

    function focusSearch() {
        globalSearchField.forceActiveFocus()
        globalSearchField.selectAll()
    }

    // Pane focus entry (§14.1 tab order): land on the folder tree, with a
    // current row set. Focusing a list whose current index is still -1 left
    // the arrows moving nothing visible and Enter with nothing to choose.
    function focusPane() {
        if (folderTreeView.currentIndex < 0
            || folderTreeView.currentIndex >= folderTreeView.count)
            folderTreeView.currentIndex = folderTreeView.count > 0 ? 0 : -1
        if (folderTreeView.currentIndex >= 0)
            folderTreeView.positionViewAtIndex(folderTreeView.currentIndex,
                                               ListView.Contain)
        folderTreeView.forceActiveFocus(Qt.TabFocusReason)
    }

    // Enter/Space on the focused folder row: scope the note list to it, which
    // is what clicking the row does.
    function activateCurrentFolder() {
        if (folderTreeView.currentIndex < 0)
            return false
        NoteListModel.folderPath =
            FolderTreeModel.relPathAt(folderTreeView.currentIndex)
        NoteListModel.scope = "folder"
        return true
    }

    // Enter/Space on the focused tag row: the same toggle the mouse performs,
    // so pressing it again on the active tag clears the filter.
    function activateCurrentTag() {
        var entry = sidebar.tagEntryAt(tagListView.currentIndex)
        if (!entry)
            return null
        NoteListModel.tagFilter =
            NoteListModel.tagFilter === entry.name ? "" : entry.name
        return entry
    }

    // The tag model is a plain array of maps, so the current row's data comes
    // from the model rather than from a delegate that may not exist.
    function tagEntryAt(index) {
        var listing = tagListView.model
        if (!listing || index < 0 || index >= listing.length)
            return null
        return listing[index]
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: Interface.px(1)
        spacing: 0

        // ---- Global search (§8.4; Ctrl+Shift+F) --------------------------
        TextField {
            id: globalSearchField
            objectName: "globalSearchField"
            Layout.fillWidth: true
            Layout.margins: Interface.px(8)
            Layout.bottomMargin: Interface.px(4)
            implicitHeight: Interface.px(26)
            font.pixelSize: Interface.small
            placeholderText: qsTr("Search all notes")
            onTextEdited: CollectionSearch.query = text
            onAccepted: {
                // Enter runs the current query immediately, bypassing the
                // debounce.
                CollectionSearch.query = text
                CollectionSearch.submitNow()
                sidebar.commitRecentSearch(text)
            }
            Keys.onEscapePressed: {
                text = ""
                CollectionSearch.query = ""
            }
        }

        // Recent searches under the empty, focused field.
        ColumnLayout {
            objectName: "recentSearchesColumn"
            Layout.fillWidth: true
            Layout.leftMargin: Interface.px(8)
            Layout.rightMargin: Interface.px(8)
            spacing: 0
            visible: globalSearchField.activeFocus
                     && globalSearchField.text === ""
                     && sidebar.recentSearches.length > 0

            Repeater {
                model: sidebar.recentSearches
                Rectangle {
                    id: recentRow
                    required property string modelData
                    Layout.fillWidth: true
                    height: Interface.px(20)
                    color: recentHover.hovered ? Theme.hoverTint : "transparent"
                    HoverHandler { id: recentHover }
                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: Interface.px(6)
                        verticalAlignment: Text.AlignVCenter
                        text: "↺ " + recentRow.modelData
                        font.pixelSize: Interface.small
                        color: Theme.textMuted
                        elide: Text.ElideRight
                    }
                    Accessible.role: Accessible.ListItem
                    Accessible.name: qsTr("Recent search: %1")
                                     .arg(recentRow.modelData)
                    Accessible.onPressAction: {
                        globalSearchField.text = recentRow.modelData
                        CollectionSearch.query = recentRow.modelData
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            globalSearchField.text = recentRow.modelData
                            CollectionSearch.query = recentRow.modelData
                        }
                    }
                }
            }
        }

        // ---- Header ----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Interface.px(8)
            spacing: Interface.px(4)

            Label {
                text: qsTr("Notes")
                font.pixelSize: Interface.strong
                font.bold: true
                color: Theme.textSecondary
                Layout.fillWidth: true
            }
            ToolButton {
                objectName: "newFolderButton"
                text: "+▤"
                Accessible.name: qsTr("New folder")
                font.pixelSize: Interface.small
                implicitHeight: Interface.px(24)
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: qsTr("New folder")
                onClicked: folderDialog.openForCreate("")
            }
            ToolButton {
                objectName: "sidebarCollapseButton"
                text: "«"
                Accessible.name: qsTr("Collapse sidebar")
                font.pixelSize: Interface.body
                implicitWidth: Interface.px(22)
                implicitHeight: Interface.px(24)
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: qsTr("Collapse sidebar")
                onClicked: if (sidebar.appWindow)
                               sidebar.appWindow.sidebarCollapsed = true
            }
        }

        // ---- Scopes ----------------------------------------------------
        Rectangle {
            id: allNotesRow
            objectName: "allNotesRow"
            Layout.fillWidth: true
            height: Interface.px(28)
            color: NoteListModel.scope === "all" ? Theme.selectionTint : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Interface.px(12)
                anchors.rightMargin: Interface.px(12)
                Label {
                    text: qsTr("All Notes")
                    font.pixelSize: Interface.body
                    Layout.fillWidth: true
                }
                Label {
                    text: NoteCollection.revision >= 0
                          ? NoteCollection.noteCountInFolder("", true) : 0
                    font.pixelSize: Interface.small
                    color: Theme.textFaint
                }
            }
            Accessible.role: Accessible.ListItem
            Accessible.name: qsTr("All notes")
            Accessible.selected: NoteListModel.scope === "all"
            Accessible.onPressAction: NoteListModel.scope = "all"
            MouseArea {
                anchors.fill: parent
                onClicked: NoteListModel.scope = "all"
            }
        }

        Rectangle {
            id: favoritesRow
            objectName: "favoritesRow"
            Layout.fillWidth: true
            height: Interface.px(28)
            color: NoteListModel.scope === "favorites" ? Theme.selectionTint : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Interface.px(12)
                anchors.rightMargin: Interface.px(12)
                Label {
                    text: "★ " + qsTr("Favorites")
                    font.pixelSize: Interface.body
                    Layout.fillWidth: true
                }
            }
            Accessible.role: Accessible.ListItem
            Accessible.name: qsTr("Favorites")
            Accessible.selected: NoteListModel.scope === "favorites"
            Accessible.onPressAction: NoteListModel.scope = "favorites"
            MouseArea {
                anchors.fill: parent
                onClicked: NoteListModel.scope = "favorites"
            }
        }

        // ---- Folder tree -----------------------------------------------
        Label {
            text: qsTr("Folders")
            font.pixelSize: Interface.caption
            font.bold: true
            color: Theme.textFaint
            Layout.leftMargin: Interface.px(12)
            Layout.topMargin: Interface.px(10)
            Layout.bottomMargin: Interface.px(2)
        }

        ListView {
            id: folderTreeView
            objectName: "folderTreeView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: FolderTreeModel

            // Keyboard operation (§14.1): Up/Down walk the tree, Right/Left
            // expand and collapse it, Enter/Space scope the note list to the
            // current folder, and the context-menu key opens the folder menu.
            activeFocusOnTab: true
            keyNavigationEnabled: true
            highlightMoveDuration: 0
            Accessible.role: Accessible.Tree
            Accessible.name: qsTr("Folders")

            // The current row's model roles, published by that row (below).
            // A key handler cannot read them off itemAtIndex(), whose declared
            // type is a bare Item, and the row it wants may not be built yet.
            property string currentName: ""
            property string currentColor: ""
            property bool currentExpanded: false
            property bool currentHasChildren: false

            Keys.onPressed: function(event) {
                var relPath = folderTreeView.currentIndex >= 0
                    ? FolderTreeModel.relPathAt(folderTreeView.currentIndex) : ""
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    || event.key === Qt.Key_Space) {
                    if (sidebar.activateCurrentFolder())
                        event.accepted = true
                } else if (event.key === Qt.Key_Right) {
                    if (folderTreeView.currentHasChildren
                        && !folderTreeView.currentExpanded) {
                        NoteCollection.setFolderExpanded(relPath, true)
                        event.accepted = true
                    }
                } else if (event.key === Qt.Key_Left) {
                    if (folderTreeView.currentHasChildren
                        && folderTreeView.currentExpanded) {
                        NoteCollection.setFolderExpanded(relPath, false)
                        event.accepted = true
                    }
                } else if (event.key === Qt.Key_Menu
                           || (event.key === Qt.Key_F10
                               && (event.modifiers & Qt.ShiftModifier))) {
                    if (folderTreeView.currentIndex >= 0) {
                        folderContextMenu.openFor(relPath,
                                                  folderTreeView.currentName,
                                                  folderTreeView.currentColor)
                        event.accepted = true
                    }
                }
            }

            delegate: Rectangle {
                id: folderRow
                // FolderTreeModel's roles, declared rather than injected, so
                // the nested Row and its handlers read them through the row.
                required property string relPath
                required property string name
                required property int depth
                required property bool expanded
                required property bool hasChildren
                required property string folderColor
                required property int noteCount
                required property int index
                width: folderTreeView.width
                // Content-derived with a scaled floor, rather than a fixed
                // 28: at a large interface size the label is taller than the
                // row it was pinned inside, and the name clipped
                // (accessibility.md Finding 4).
                height: Math.max(Interface.px(28),
                                 folderRowContent.implicitHeight + Interface.px(6))
                // Screen-reader name/role and the keyboard's position (§14.2).
                Accessible.role: Accessible.TreeItem
                Accessible.name: folderRow.name
                Accessible.description: qsTr("%1 notes").arg(folderRow.noteCount)
                Accessible.selected: NoteListModel.scope === "folder"
                    && NoteListModel.folderPath === folderRow.relPath
                Accessible.focused: folderTreeView.activeFocus
                    && folderTreeView.currentIndex === folderRow.index
                readonly property bool isCurrentRow:
                    folderTreeView.currentIndex === folderRow.index
                color: {
                    if (sidebar.dropTargetActive
                        && sidebar.dropTargetFolder === folderRow.relPath)
                        return Theme.selectionActiveTint
                    if (NoteListModel.scope === "folder"
                        && NoteListModel.folderPath === folderRow.relPath)
                        return Theme.selectionTint
                    if (folderRow.isCurrentRow && folderTreeView.activeFocus)
                        return Theme.focusTint
                    return rowHover.hovered ? Theme.hoverTint : "transparent"
                }

                Rectangle {
                    objectName: "folderRowFocusRing"
                    anchors.fill: parent
                    anchors.margins: Interface.px(1)
                    visible: folderRow.isCurrentRow && folderTreeView.activeFocus
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.focusRing
                    radius: Interface.px(3)
                }

                // What the view's key handler needs from whichever row is
                // current: expand/collapse state for the arrow keys, name and
                // colour for the context menu.
                Binding {
                    target: folderTreeView
                    property: "currentName"
                    value: folderRow.name
                    when: folderRow.isCurrentRow
                }
                Binding {
                    target: folderTreeView
                    property: "currentColor"
                    value: folderRow.folderColor
                    when: folderRow.isCurrentRow
                }
                Binding {
                    target: folderTreeView
                    property: "currentExpanded"
                    value: folderRow.expanded
                    when: folderRow.isCurrentRow
                }
                Binding {
                    target: folderTreeView
                    property: "currentHasChildren"
                    value: folderRow.hasChildren
                    when: folderRow.isCurrentRow
                }

                HoverHandler { id: rowHover }
            // §9.5 folder context menu.
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: folderContextMenu.openFor(
                    folderRow.relPath, folderRow.name, folderRow.folderColor)
            }

                RowLayout {
                    id: folderRowContent
                    anchors.fill: parent
                    anchors.leftMargin: Interface.px(8)
                                        + folderRow.depth * Interface.px(14)
                    anchors.rightMargin: Interface.px(6)
                    spacing: Interface.px(4)

                    // Chevron: expand/collapse (§8.1)
                    Text {
                        text: folderRow.expanded ? "▾" : "▸"
                        font.pixelSize: Interface.caption
                        color: Theme.textMuted
                        visible: folderRow.hasChildren
                        width: Interface.px(10)
                        TapHandler {
                            onTapped: FolderTreeModel.toggleExpanded(folderRow.index)
                        }
                    }
                    Item {
                        width: Interface.px(10)
                        visible: !folderRow.hasChildren
                    }

                    // Folder glyph, tinted by the folder color (§8.1)
                    Rectangle {
                        width: Interface.px(10)
                        height: Interface.px(8)
                        radius: Interface.px(2)
                        color: folderRow.folderColor !== ""
                               ? folderRow.folderColor : Theme.mutedGlyph
                    }

                    Label {
                        text: folderRow.name
                        font.pixelSize: Interface.body
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // Hover actions.
                    ToolButton {
                        objectName: "folderRenameButton"
                        visible: rowHover.hovered
                        text: "✎"
                        font.pixelSize: Interface.caption
                        implicitWidth: Interface.px(20)
                        implicitHeight: Interface.px(22)
                        onClicked: folderDialog.openForRename(
                                       folderRow.relPath, folderRow.name,
                                       folderRow.folderColor)
                    }
                    ToolButton {
                        objectName: "folderNewChildButton"
                        visible: rowHover.hovered
                        text: "+"
                        font.pixelSize: Interface.small
                        implicitWidth: Interface.px(20)
                        implicitHeight: Interface.px(22)
                        onClicked: folderDialog.openForCreate(folderRow.relPath)
                    }
                    ToolButton {
                        objectName: "folderDeleteButton"
                        visible: rowHover.hovered
                        text: "✕"
                        font.pixelSize: Interface.caption
                        implicitWidth: Interface.px(20)
                        implicitHeight: Interface.px(22)
                        onClicked: deleteFolderDialog.openFor(
                                       folderRow.relPath, folderRow.name,
                                       folderRow.noteCount)
                    }

                    Label {
                        visible: !rowHover.hovered
                        text: folderRow.noteCount
                        font.pixelSize: Interface.small
                        color: Theme.textFaint
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: {
                        // Keep the keyboard's row with the pointer's.
                        folderTreeView.currentIndex = folderRow.index
                        NoteListModel.folderPath = folderRow.relPath
                        NoteListModel.scope = "folder"
                    }
                }
            }
        }

        // ---- Tags (§8.2: sidebar with counts; click filters) -----------
        Label {
            text: qsTr("Tags")
            font.pixelSize: Interface.caption
            font.bold: true
            color: Theme.textFaint
            Layout.leftMargin: Interface.px(12)
            Layout.topMargin: Interface.px(6)
            Layout.bottomMargin: Interface.px(2)
            visible: tagListView.count > 0
        }

        ListView {
            id: tagListView
            objectName: "tagListView"
            Layout.fillWidth: true
            // Fixed-height rows: computable before any delegate exists
            // (the folder tree above takes the leftover height). The row
            // height and the cap both follow the interface size, so the list
            // still shows the same number of tags at any of them.
            Layout.preferredHeight: Math.min(count * Interface.px(24),
                                             Interface.px(170))
            Layout.bottomMargin: Interface.px(4)
            clip: true

            // Array-of-maps model, live under the collection revision.
            model: {
                var revision = NoteCollection.revision
                return NoteCollection.isOpen ? NoteCollection.tagListing() : []
            }

            // Keyboard operation (§14.1), matching the folder tree above.
            activeFocusOnTab: true
            keyNavigationEnabled: true
            highlightMoveDuration: 0
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Tags")
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    || event.key === Qt.Key_Space) {
                    if (sidebar.activateCurrentTag())
                        event.accepted = true
                } else if (event.key === Qt.Key_Menu
                           || (event.key === Qt.Key_F10
                               && (event.modifiers & Qt.ShiftModifier))) {
                    var entry = sidebar.tagEntryAt(tagListView.currentIndex)
                    if (entry) {
                        tagContextMenu.openFor(entry.name, entry.color,
                                               entry.count)
                        event.accepted = true
                    }
                }
            }

            delegate: Rectangle {
                id: tagRow
                required property var modelData
                required property int index
                width: tagListView.width
                height: Interface.px(24)
                Accessible.role: Accessible.ListItem
                Accessible.name: tagRow.modelData.name
                Accessible.description: qsTr("%1 notes").arg(tagRow.modelData.count)
                Accessible.selected:
                    NoteListModel.tagFilter === tagRow.modelData.name
                Accessible.focused: tagListView.activeFocus
                    && tagListView.currentIndex === tagRow.index
                readonly property bool isCurrentRow:
                    tagListView.currentIndex === tagRow.index
                color: NoteListModel.tagFilter === tagRow.modelData.name
                       ? Theme.selectionTint
                       : (tagRow.isCurrentRow && tagListView.activeFocus
                          ? Theme.focusTint
                          : (tagHover.hovered ? Theme.hoverTint : "transparent"))

                Rectangle {
                    objectName: "tagRowFocusRing"
                    anchors.fill: parent
                    anchors.margins: Interface.px(1)
                    visible: tagRow.isCurrentRow && tagListView.activeFocus
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.focusRing
                    radius: Interface.px(3)
                }

                HoverHandler { id: tagHover }
            // §9.5 tag context menu.
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: tagContextMenu.openFor(
                    tagRow.modelData.name, tagRow.modelData.color, tagRow.modelData.count)
            }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Interface.px(14)
                    anchors.rightMargin: Interface.px(6)
                    spacing: Interface.px(6)

                    Rectangle {
                        width: Interface.px(8)
                        height: Interface.px(8)
                        radius: Interface.px(4)
                        color: tagRow.modelData.color !== "" ? tagRow.modelData.color
                                                      : Theme.mutedGlyph
                    }
                    Label {
                        text: tagRow.modelData.name
                        font.pixelSize: Interface.body
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        objectName: "tagEditButton"
                        visible: tagHover.hovered
                        text: "✎"
                        font.pixelSize: Interface.caption
                        implicitWidth: Interface.px(20)
                        implicitHeight: Interface.px(20)
                        onClicked: tagDialog.openFor(tagRow.modelData.name,
                                                     tagRow.modelData.color)
                    }
                    ToolButton {
                        objectName: "tagDeleteButton"
                        visible: tagHover.hovered
                        text: "✕"
                        font.pixelSize: Interface.caption
                        implicitWidth: Interface.px(20)
                        implicitHeight: Interface.px(20)
                        onClicked: deleteTagDialog.openFor(tagRow.modelData.name,
                                                           tagRow.modelData.count)
                    }
                    Label {
                        visible: !tagHover.hovered
                        text: tagRow.modelData.count
                        font.pixelSize: Interface.small
                        color: Theme.textFaint
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    // Toggle: clicking the active tag clears the filter.
                    onClicked: {
                        tagListView.currentIndex = tagRow.index
                        NoteListModel.tagFilter =
                            NoteListModel.tagFilter === tagRow.modelData.name
                                ? "" : tagRow.modelData.name
                    }
                }
            }
        }

        // ---- Trash: item count and the empty action, behind a
        // count-naming confirmation. --------------------------------
        Rectangle {
            id: trashRow
            objectName: "trashRow"
            Layout.fillWidth: true
            height: Interface.px(26)
            color: trashHover.hovered ? Theme.hoverTint : "transparent"

            readonly property int trashCount: {
                var revision = NoteCollection.revision
                return NoteCollection.isOpen
                    ? NoteCollection.trashItemCount() : 0
            }

            HoverHandler { id: trashHover }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Interface.px(12)
                anchors.rightMargin: Interface.px(12)
                Label {
                    text: qsTr("Trash")
                    font.pixelSize: Interface.small
                    color: Theme.textMuted
                    Layout.fillWidth: true
                }
                Label {
                    objectName: "trashCountLabel"
                    text: trashRow.trashCount
                    font.pixelSize: Interface.small
                    color: Theme.textFaint
                }
            }
            Accessible.role: Accessible.ButtonMenu
            Accessible.name: qsTr("Trash, %n note(s)", "", trashRow.trashCount)
            Accessible.onPressAction: trashMenu.popup()
            TapHandler {
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onTapped: trashMenu.popup()
            }
        }
    }

    Menu {
        id: trashMenu
        objectName: "trashMenu"
        MenuItem {
            objectName: "emptyTrashItem"
            text: MenuText.label(qsTr("&Empty trash…"))
            enabled: NoteCollection.isOpen
                     && NoteCollection.trashItemCount() > 0
            onTriggered: emptyTrashDialog.openFor(
                NoteCollection.trashItemCount())
        }
    }

    KvitDialog {
        id: emptyTrashDialog
        objectName: "emptyTrashDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Interface.px(320)
        title: qsTr("Empty Trash")
        standardButtons: Dialog.Ok | Dialog.Cancel

        function openFor(count) {
            emptyTrashText.text = qsTr(
                "Permanently delete %1 item(s) from the trash? "
                + "This cannot be undone.").arg(count)
            open()
        }
        onAccepted: NoteCollection.emptyTrash()

        Label {
            id: emptyTrashText
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Interface.body
        }
    }

    // ---- §9.5 folder and tag context menus --------------------------
    Menu {
        id: folderContextMenu
        objectName: "folderContextMenu"
        property string relPath: ""
        property string folderName: ""
        property string folderColor: ""

        function openFor(path, name, color) {
            relPath = path
            folderName = name
            folderColor = color
            popup()
        }

        MenuItem {
            objectName: "ctxFolderNewNote"
            text: MenuText.label(qsTr("&New note"))
            onTriggered: {
                var created = NoteCollection.createNote(
                    folderContextMenu.relPath, "")
                if (created !== "" && sidebar.appWindow)
                    sidebar.appWindow.openNoteByPath(created)
            }
        }
        MenuItem {
            text: MenuText.label(qsTr("New &subfolder…"))
            onTriggered: folderDialog.openForCreate(folderContextMenu.relPath)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "ctxFolderRename"
            text: MenuText.label(qsTr("&Rename / color…"))
            onTriggered: folderDialog.openForRename(
                folderContextMenu.relPath, folderContextMenu.folderName,
                folderContextMenu.folderColor)
        }
        MenuItem {
            objectName: "ctxFolderDelete"
            text: MenuText.label(qsTr("&Delete…"))
            onTriggered: deleteFolderDialog.openFor(
                folderContextMenu.relPath, folderContextMenu.folderName,
                NoteCollection.noteCountInFolder(
                    folderContextMenu.relPath, true))
        }
    }

    Menu {
        id: tagContextMenu
        objectName: "tagContextMenu"
        property string tagName: ""
        property string tagColor: ""
        property int tagCount: 0

        function openFor(name, color, count) {
            tagName = name
            tagColor = color
            tagCount = count
            popup()
        }

        MenuItem {
            objectName: "ctxTagRename"
            text: MenuText.label(qsTr("&Rename / color…"))
            onTriggered: tagDialog.openFor(tagContextMenu.tagName,
                                           tagContextMenu.tagColor)
        }
        MenuItem {
            objectName: "ctxTagDelete"
            text: MenuText.label(qsTr("&Delete…"))
            onTriggered: deleteTagDialog.openFor(tagContextMenu.tagName,
                                                 tagContextMenu.tagCount)
        }
    }

    // ---- Folder create/rename dialog with the color palette ------------
    KvitDialog {
        id: folderDialog
        // Opens on the field it is about, so a screen reader
        // announces something to type into and a keyboard user
        // does not have to guess how many tabs reach it.
        initialFocusItem: folderNameField
        objectName: "folderDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Interface.px(300)
        title: mode === "create" ? qsTr("New Folder") : qsTr("Rename Folder")

        property string mode: "create"   // "create" | "rename"
        property string targetPath: ""   // parent (create) or folder (rename)
        property string selectedColor: ""

        // "" first = default gray; the rest is a small fixed palette
        // (features.md §8.1 folder colors).
        readonly property var palette: [""].concat(Theme.colorPalette)

        function openForCreate(parentPath) {
            mode = "create"
            targetPath = parentPath
            selectedColor = ""
            folderNameField.text = ""
            open()
            folderNameField.forceActiveFocus()
        }

        function openForRename(relPath, currentName, currentColor) {
            mode = "rename"
            targetPath = relPath
            selectedColor = currentColor
            folderNameField.text = currentName
            open()
            folderNameField.forceActiveFocus()
            folderNameField.selectAll()
        }

        onAccepted: {
            var name = folderNameField.text
            if (mode === "create") {
                var created = NoteCollection.createFolder(targetPath, name)
                if (created !== "" && selectedColor !== "")
                    NoteCollection.setFolderColor(created, selectedColor)
            } else {
                var color = selectedColor
                var oldPath = targetPath
                sidebar.appWindow.requestFolderRename(
                    oldPath, name, function(result) {
                        NoteCollection.setFolderColor(result.newPath, color)
                    })
            }
        }

        contentItem: ColumnLayout {
            spacing: Interface.px(8)
            TextField {
                id: folderNameField
                objectName: "folderDialogNameField"
                Layout.fillWidth: true
                placeholderText: qsTr("Folder name")
                onAccepted: folderDialog.accept()
            }
            Row {
                spacing: Interface.px(6)
                Repeater {
                    model: folderDialog.palette
                    Rectangle {
                        id: folderSwatch
                        required property string modelData
                        width: Interface.px(20)
                        height: Interface.px(20)
                        radius: Interface.px(10)
                        color: folderSwatch.modelData === "" ? Theme.mutedGlyph : folderSwatch.modelData
                        border.width: folderDialog.selectedColor === folderSwatch.modelData ? 2 : 0
                        border.color: Theme.textPrimary
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: Theme.colorName(folderSwatch.modelData)
                        Accessible.checkable: true
                        Accessible.checked: folderDialog.selectedColor
                                            === folderSwatch.modelData
                        Accessible.onPressAction:
                            folderDialog.selectedColor = folderSwatch.modelData
                        TapHandler {
                            onTapped: folderDialog.selectedColor = folderSwatch.modelData
                        }
                    }
                }
            }
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    // ---- Tag manage dialog: rename (merge when the target exists) and
    // color (features.md §8.2 tag management) --------------------------
    KvitDialog {
        id: tagDialog
        // Opens on the field it is about, so a screen reader
        // announces something to type into and a keyboard user
        // does not have to guess how many tabs reach it.
        initialFocusItem: tagNameField
        objectName: "tagDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Interface.px(300)
        title: qsTr("Edit Tag")

        property string originalName: ""
        property string selectedColor: ""

        readonly property var palette: Theme.colorPalette

        function openFor(name, color) {
            originalName = name
            selectedColor = color
            tagNameField.text = name
            open()
            tagNameField.forceActiveFocus()
            tagNameField.selectAll()
        }

        onAccepted: {
            var newName = tagNameField.text.trim()
            if (newName === "")
                return
            if (newName !== originalName
                && NoteCollection.tagCount(newName) > 0) {
                // Renaming onto an existing tag is a merge — confirm with
                // the blast radius before touching files.
                mergeTagDialog.openFor(originalName, newName)
                return
            }
            if (newName !== originalName)
                NoteCollection.renameTag(originalName, newName)
            NoteCollection.setTagColor(newName, selectedColor)
            if (NoteListModel.tagFilter === originalName)
                NoteListModel.tagFilter = newName
        }

        contentItem: ColumnLayout {
            spacing: Interface.px(8)
            TextField {
                id: tagNameField
                objectName: "tagDialogNameField"
                Layout.fillWidth: true
                onAccepted: tagDialog.accept()
            }
            Row {
                spacing: Interface.px(6)
                Repeater {
                    model: tagDialog.palette
                    Rectangle {
                        id: tagSwatch
                        required property string modelData
                        width: Interface.px(20)
                        height: Interface.px(20)
                        radius: Interface.px(10)
                        color: tagSwatch.modelData
                        border.width: tagDialog.selectedColor === tagSwatch.modelData ? 2 : 0
                        border.color: Theme.textPrimary
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: Theme.colorName(tagSwatch.modelData)
                        Accessible.checkable: true
                        Accessible.checked: tagDialog.selectedColor
                                            === tagSwatch.modelData
                        Accessible.onPressAction:
                            tagDialog.selectedColor = tagSwatch.modelData
                        TapHandler {
                            onTapped: tagDialog.selectedColor = tagSwatch.modelData
                        }
                    }
                }
            }
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    KvitDialog {
        id: mergeTagDialog
        objectName: "mergeTagDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Interface.px(320)
        title: qsTr("Merge Tags")

        property string fromName: ""
        property string intoName: ""

        function openFor(from, into) {
            fromName = from
            intoName = into
            mergeText.text = qsTr(
                "Merge \"%1\" into \"%2\"? %3 note(s) will be retagged.")
                .arg(from).arg(into)
                .arg(NoteCollection.tagCount(from))
            open()
        }

        onAccepted: {
            NoteCollection.renameTag(fromName, intoName)
            if (NoteListModel.tagFilter === fromName)
                NoteListModel.tagFilter = intoName
        }

        contentItem: Label {
            id: mergeText
            wrapMode: Text.WordWrap
            leftPadding: Interface.px(12)
            rightPadding: Interface.px(12)
            topPadding: Interface.px(8)
            bottomPadding: Interface.px(8)
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    KvitDialog {
        id: deleteTagDialog
        objectName: "deleteTagDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Interface.px(320)
        title: qsTr("Delete Tag")

        property string targetName: ""

        function openFor(name, count) {
            targetName = name
            deleteTagText.text = qsTr(
                "Remove tag \"%1\" from %2 note(s)?").arg(name).arg(count)
            open()
        }

        onAccepted: {
            NoteCollection.deleteTag(targetName)
            if (NoteListModel.tagFilter === targetName)
                NoteListModel.tagFilter = ""
        }

        contentItem: Label {
            id: deleteTagText
            wrapMode: Text.WordWrap
            leftPadding: Interface.px(12)
            rightPadding: Interface.px(12)
            topPadding: Interface.px(8)
            bottomPadding: Interface.px(8)
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    // ---- Delete-folder confirmation (to trash, confirmed) -------------
    KvitDialog {
        id: deleteFolderDialog
        objectName: "deleteFolderDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Interface.px(320)
        title: qsTr("Delete Folder")

        property string targetPath: ""

        function openFor(relPath, name, noteCount) {
            targetPath = relPath
            messageText.text = noteCount > 0
                ? qsTr("Delete \"%1\" and its %2 note(s)? They move to the trash.")
                      .arg(name).arg(noteCount)
                : qsTr("Delete the empty folder \"%1\"?").arg(name)
            open()
        }

        onAccepted: {
            if (NoteListModel.scope === "folder"
                && (NoteListModel.folderPath === targetPath
                    || NoteListModel.folderPath.startsWith(targetPath + "/")))
                NoteListModel.scope = "all"
            NoteCollection.deleteFolder(targetPath)
        }

        contentItem: Label {
            id: messageText
            wrapMode: Text.WordWrap
            leftPadding: Interface.px(12)
            rightPadding: Interface.px(12)
            topPadding: Interface.px(8)
            bottomPadding: Interface.px(8)
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }
}
