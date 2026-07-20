// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// The navigation sidebar: scopes, the folder tree, and the tag list.
// Functional Fusion styling only. All state lives in noteCollection /
// noteListModel; this pane renders and forwards.
Rectangle {
    id: sidebar
    objectName: "sidebar"

    color: theme.panelBackground

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
        width: 1
        color: theme.border
    }

    // Recent searches (features.md §8.4), persisted through the settings
    // store; read back by applyPersistedSessionState.
    property var recentSearches: []

    function applyPersistedSearchHistory() {
        recentSearches = appSettings.value("search.recent", [])
    }

    function commitRecentSearch(query) {
        var q = query.trim()
        if (q === "")
            return
        var list = recentSearches.filter(function(item) { return item !== q })
        list.unshift(q)
        recentSearches = list.slice(0, 6)
        appSettings.setValue("search.recent", recentSearches)
    }

    function focusSearch() {
        globalSearchField.forceActiveFocus()
        globalSearchField.selectAll()
    }

    // Pane focus entry (§14.1 tab order): land on the folder tree.
    function focusPane() {
        folderTreeView.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0

        // ---- Global search (§8.4; Ctrl+Shift+F) --------------------------
        TextField {
            id: globalSearchField
            objectName: "globalSearchField"
            Layout.fillWidth: true
            Layout.margins: 8
            Layout.bottomMargin: 4
            implicitHeight: 26
            font.pixelSize: 11
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
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 0
            visible: globalSearchField.activeFocus
                     && globalSearchField.text === ""
                     && sidebar.recentSearches.length > 0

            Repeater {
                model: sidebar.recentSearches
                Rectangle {
                    Layout.fillWidth: true
                    height: 20
                    color: recentHover.hovered ? theme.hoverTint : "transparent"
                    HoverHandler { id: recentHover }
                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        verticalAlignment: Text.AlignVCenter
                        text: "↺ " + modelData
                        font.pixelSize: 11
                        color: theme.textMuted
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            globalSearchField.text = modelData
                            CollectionSearch.query = modelData
                        }
                    }
                }
            }
        }

        // ---- Header ----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 4

            Label {
                text: qsTr("Notes")
                font.pixelSize: 13
                font.bold: true
                color: theme.textSecondary
                Layout.fillWidth: true
            }
            ToolButton {
                objectName: "newFolderButton"
                text: "+▤"
                font.pixelSize: 11
                implicitHeight: 24
                ToolTip.visible: hovered
                ToolTip.text: qsTr("New folder")
                onClicked: folderDialog.openForCreate("")
            }
            ToolButton {
                objectName: "sidebarCollapseButton"
                text: "«"
                font.pixelSize: 12
                implicitWidth: 22
                implicitHeight: 24
                ToolTip.visible: hovered
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
            height: 28
            color: noteListModel.scope === "all" ? theme.selectionTint : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                Label {
                    text: qsTr("All Notes")
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
                Label {
                    text: noteCollection.revision >= 0
                          ? noteCollection.noteCountInFolder("", true) : 0
                    font.pixelSize: 11
                    color: theme.textFaint
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: noteListModel.scope = "all"
            }
        }

        Rectangle {
            id: favoritesRow
            objectName: "favoritesRow"
            Layout.fillWidth: true
            height: 28
            color: noteListModel.scope === "favorites" ? theme.selectionTint : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                Label {
                    text: "★ " + qsTr("Favorites")
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: noteListModel.scope = "favorites"
            }
        }

        // ---- Folder tree -----------------------------------------------
        Label {
            text: qsTr("Folders")
            font.pixelSize: 10
            font.bold: true
            color: theme.textFaint
            Layout.leftMargin: 12
            Layout.topMargin: 10
            Layout.bottomMargin: 2
        }

        ListView {
            id: folderTreeView
            objectName: "folderTreeView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: FolderTreeModel

            delegate: Rectangle {
                id: folderRow
                width: folderTreeView.width
                height: 28
                color: {
                    if (sidebar.dropTargetActive
                        && sidebar.dropTargetFolder === model.relPath)
                        return theme.selectionActiveTint
                    if (noteListModel.scope === "folder"
                        && noteListModel.folderPath === model.relPath)
                        return theme.selectionTint
                    return rowHover.hovered ? theme.hoverTint : "transparent"
                }

                HoverHandler { id: rowHover }
            // §9.5 folder context menu.
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: folderContextMenu.openFor(
                    model.relPath, model.name, model.folderColor)
            }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8 + model.depth * 14
                    anchors.rightMargin: 6
                    spacing: 4

                    // Chevron: expand/collapse (§8.1)
                    Text {
                        text: model.expanded ? "▾" : "▸"
                        font.pixelSize: 10
                        color: theme.textMuted
                        visible: model.hasChildren
                        width: 10
                        TapHandler {
                            onTapped: FolderTreeModel.toggleExpanded(index)
                        }
                    }
                    Item { width: 10; visible: !model.hasChildren }

                    // Folder glyph, tinted by the folder color (§8.1)
                    Rectangle {
                        width: 10
                        height: 8
                        radius: 2
                        color: model.folderColor !== ""
                               ? model.folderColor : theme.mutedGlyph
                    }

                    Label {
                        text: model.name
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // Hover actions.
                    ToolButton {
                        objectName: "folderRenameButton"
                        visible: rowHover.hovered
                        text: "✎"
                        font.pixelSize: 10
                        implicitWidth: 20
                        implicitHeight: 22
                        onClicked: folderDialog.openForRename(
                                       model.relPath, model.name,
                                       model.folderColor)
                    }
                    ToolButton {
                        objectName: "folderNewChildButton"
                        visible: rowHover.hovered
                        text: "+"
                        font.pixelSize: 11
                        implicitWidth: 20
                        implicitHeight: 22
                        onClicked: folderDialog.openForCreate(model.relPath)
                    }
                    ToolButton {
                        objectName: "folderDeleteButton"
                        visible: rowHover.hovered
                        text: "✕"
                        font.pixelSize: 10
                        implicitWidth: 20
                        implicitHeight: 22
                        onClicked: deleteFolderDialog.openFor(
                                       model.relPath, model.name,
                                       model.noteCount)
                    }

                    Label {
                        visible: !rowHover.hovered
                        text: model.noteCount
                        font.pixelSize: 11
                        color: theme.textFaint
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: {
                        noteListModel.folderPath = model.relPath
                        noteListModel.scope = "folder"
                    }
                }
            }
        }

        // ---- Tags (§8.2: sidebar with counts; click filters) -----------
        Label {
            text: qsTr("Tags")
            font.pixelSize: 10
            font.bold: true
            color: theme.textFaint
            Layout.leftMargin: 12
            Layout.topMargin: 6
            Layout.bottomMargin: 2
            visible: tagListView.count > 0
        }

        ListView {
            id: tagListView
            objectName: "tagListView"
            Layout.fillWidth: true
            // Fixed-height rows: computable before any delegate exists
            // (the folder tree above takes the leftover height).
            Layout.preferredHeight: Math.min(count * 24, 170)
            Layout.bottomMargin: 4
            clip: true

            // Array-of-maps model, live under the collection revision.
            model: {
                var revision = noteCollection.revision
                return noteCollection.isOpen ? noteCollection.tagListing() : []
            }

            delegate: Rectangle {
                width: tagListView.width
                height: 24
                color: noteListModel.tagFilter === modelData.name
                       ? theme.selectionTint
                       : (tagHover.hovered ? theme.hoverTint : "transparent")

                HoverHandler { id: tagHover }
            // §9.5 tag context menu.
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: tagContextMenu.openFor(
                    modelData.name, modelData.color, modelData.count)
            }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 6
                    spacing: 6

                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: modelData.color !== "" ? modelData.color
                                                      : theme.mutedGlyph
                    }
                    Label {
                        text: modelData.name
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        objectName: "tagEditButton"
                        visible: tagHover.hovered
                        text: "✎"
                        font.pixelSize: 10
                        implicitWidth: 20
                        implicitHeight: 20
                        onClicked: tagDialog.openFor(modelData.name,
                                                     modelData.color)
                    }
                    ToolButton {
                        objectName: "tagDeleteButton"
                        visible: tagHover.hovered
                        text: "✕"
                        font.pixelSize: 10
                        implicitWidth: 20
                        implicitHeight: 20
                        onClicked: deleteTagDialog.openFor(modelData.name,
                                                           modelData.count)
                    }
                    Label {
                        visible: !tagHover.hovered
                        text: modelData.count
                        font.pixelSize: 11
                        color: theme.textFaint
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    // Toggle: clicking the active tag clears the filter.
                    onClicked: noteListModel.tagFilter =
                        noteListModel.tagFilter === modelData.name
                            ? "" : modelData.name
                }
            }
        }

        // ---- Trash: item count and the empty action, behind a
        // count-naming confirmation. --------------------------------
        Rectangle {
            objectName: "trashRow"
            Layout.fillWidth: true
            height: 26
            color: trashHover.hovered ? theme.hoverTint : "transparent"

            readonly property int trashCount: {
                var revision = noteCollection.revision
                return noteCollection.isOpen
                    ? noteCollection.trashItemCount() : 0
            }

            HoverHandler { id: trashHover }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                Label {
                    text: qsTr("Trash")
                    font.pixelSize: 11
                    color: theme.textMuted
                    Layout.fillWidth: true
                }
                Label {
                    objectName: "trashCountLabel"
                    text: parent.parent.trashCount
                    font.pixelSize: 11
                    color: theme.textFaint
                }
            }
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
            text: qsTr("Empty trash…")
            enabled: noteCollection.isOpen
                     && noteCollection.trashItemCount() > 0
            onTriggered: emptyTrashDialog.openFor(
                noteCollection.trashItemCount())
        }
    }

    Dialog {
        id: emptyTrashDialog
        objectName: "emptyTrashDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 320
        title: qsTr("Empty Trash")
        standardButtons: Dialog.Ok | Dialog.Cancel

        function openFor(count) {
            emptyTrashText.text = qsTr(
                "Permanently delete %1 item(s) from the trash? "
                + "This cannot be undone.").arg(count)
            open()
        }
        onAccepted: noteCollection.emptyTrash()

        Label {
            id: emptyTrashText
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: 12
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
            text: qsTr("New note")
            onTriggered: {
                var created = noteCollection.createNote(
                    folderContextMenu.relPath, "")
                if (created !== "" && sidebar.appWindow)
                    sidebar.appWindow.openNoteByPath(created)
            }
        }
        MenuItem {
            text: qsTr("New subfolder…")
            onTriggered: folderDialog.openForCreate(folderContextMenu.relPath)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "ctxFolderRename"
            text: qsTr("Rename / color…")
            onTriggered: folderDialog.openForRename(
                folderContextMenu.relPath, folderContextMenu.folderName,
                folderContextMenu.folderColor)
        }
        MenuItem {
            objectName: "ctxFolderDelete"
            text: qsTr("Delete…")
            onTriggered: deleteFolderDialog.openFor(
                folderContextMenu.relPath, folderContextMenu.folderName,
                noteCollection.noteCountInFolder(
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
            text: qsTr("Rename / color…")
            onTriggered: tagDialog.openFor(tagContextMenu.tagName,
                                           tagContextMenu.tagColor)
        }
        MenuItem {
            objectName: "ctxTagDelete"
            text: qsTr("Delete…")
            onTriggered: deleteTagDialog.openFor(tagContextMenu.tagName,
                                                 tagContextMenu.tagCount)
        }
    }

    // ---- Folder create/rename dialog with the color palette ------------
    Dialog {
        id: folderDialog
        objectName: "folderDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 300
        title: mode === "create" ? qsTr("New Folder") : qsTr("Rename Folder")

        property string mode: "create"   // "create" | "rename"
        property string targetPath: ""   // parent (create) or folder (rename)
        property string selectedColor: ""

        // "" first = default gray; the rest is a small fixed palette
        // (features.md §8.1 folder colors).
        readonly property var palette: [""].concat(theme.colorPalette)

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
                var created = noteCollection.createFolder(targetPath, name)
                if (created !== "" && selectedColor !== "")
                    noteCollection.setFolderColor(created, selectedColor)
            } else {
                var color = selectedColor
                var oldPath = targetPath
                sidebar.appWindow.requestFolderRename(
                    oldPath, name, function(result) {
                        noteCollection.setFolderColor(result.newPath, color)
                    })
            }
        }

        contentItem: ColumnLayout {
            spacing: 8
            TextField {
                id: folderNameField
                objectName: "folderDialogNameField"
                Layout.fillWidth: true
                placeholderText: qsTr("Folder name")
                onAccepted: folderDialog.accept()
            }
            Row {
                spacing: 6
                Repeater {
                    model: folderDialog.palette
                    Rectangle {
                        width: 20
                        height: 20
                        radius: 10
                        color: modelData === "" ? theme.mutedGlyph : modelData
                        border.width: folderDialog.selectedColor === modelData ? 2 : 0
                        border.color: theme.textPrimary
                        TapHandler {
                            onTapped: folderDialog.selectedColor = modelData
                        }
                    }
                }
            }
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    // ---- Tag manage dialog: rename (merge when the target exists) and
    // color (features.md §8.2 tag management) --------------------------
    Dialog {
        id: tagDialog
        objectName: "tagDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 300
        title: qsTr("Edit Tag")

        property string originalName: ""
        property string selectedColor: ""

        readonly property var palette: theme.colorPalette

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
                && noteCollection.tagCount(newName) > 0) {
                // Renaming onto an existing tag is a merge — confirm with
                // the blast radius before touching files.
                mergeTagDialog.openFor(originalName, newName)
                return
            }
            if (newName !== originalName)
                noteCollection.renameTag(originalName, newName)
            noteCollection.setTagColor(newName, selectedColor)
            if (noteListModel.tagFilter === originalName)
                noteListModel.tagFilter = newName
        }

        contentItem: ColumnLayout {
            spacing: 8
            TextField {
                id: tagNameField
                objectName: "tagDialogNameField"
                Layout.fillWidth: true
                onAccepted: tagDialog.accept()
            }
            Row {
                spacing: 6
                Repeater {
                    model: tagDialog.palette
                    Rectangle {
                        width: 20
                        height: 20
                        radius: 10
                        color: modelData
                        border.width: tagDialog.selectedColor === modelData ? 2 : 0
                        border.color: theme.textPrimary
                        TapHandler {
                            onTapped: tagDialog.selectedColor = modelData
                        }
                    }
                }
            }
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    Dialog {
        id: mergeTagDialog
        objectName: "mergeTagDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 320
        title: qsTr("Merge Tags")

        property string fromName: ""
        property string intoName: ""

        function openFor(from, into) {
            fromName = from
            intoName = into
            mergeText.text = qsTr(
                "Merge \"%1\" into \"%2\"? %3 note(s) will be retagged.")
                .arg(from).arg(into)
                .arg(noteCollection.tagCount(from))
            open()
        }

        onAccepted: {
            noteCollection.renameTag(fromName, intoName)
            if (noteListModel.tagFilter === fromName)
                noteListModel.tagFilter = intoName
        }

        contentItem: Label {
            id: mergeText
            wrapMode: Text.WordWrap
            leftPadding: 12
            rightPadding: 12
            topPadding: 8
            bottomPadding: 8
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    Dialog {
        id: deleteTagDialog
        objectName: "deleteTagDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 320
        title: qsTr("Delete Tag")

        property string targetName: ""

        function openFor(name, count) {
            targetName = name
            deleteTagText.text = qsTr(
                "Remove tag \"%1\" from %2 note(s)?").arg(name).arg(count)
            open()
        }

        onAccepted: {
            noteCollection.deleteTag(targetName)
            if (noteListModel.tagFilter === targetName)
                noteListModel.tagFilter = ""
        }

        contentItem: Label {
            id: deleteTagText
            wrapMode: Text.WordWrap
            leftPadding: 12
            rightPadding: 12
            topPadding: 8
            bottomPadding: 8
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    // ---- Delete-folder confirmation (to trash, confirmed) -------------
    Dialog {
        id: deleteFolderDialog
        objectName: "deleteFolderDialog"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 320
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
            if (noteListModel.scope === "folder"
                && (noteListModel.folderPath === targetPath
                    || noteListModel.folderPath.startsWith(targetPath + "/")))
                noteListModel.scope = "all"
            noteCollection.deleteFolder(targetPath)
        }

        contentItem: Label {
            id: messageText
            wrapMode: Text.WordWrap
            leftPadding: 12
            rightPadding: 12
            topPadding: 8
            bottomPadding: 8
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
    }
}
