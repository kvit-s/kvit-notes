// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Window
import Kvit 1.0

// What a session remembers: the panel layout, the view toggles, the window's
// geometry, the note list's sort order, and the recently-used entries of the
// block and math menus.
//
// Restoring happens once, in one function, so the order is visible: both note
// list sort keys are read before either is assigned, because assigning the
// first fires the projection change whose own handler would write back the
// key not yet read. Geometry is the other ordering constraint — nothing is
// saved until the stored geometry has been applied, or the defaults would
// overwrite it, and a stored position is only reused when its rectangle still
// lands on a connected screen, since monitors change between sessions.
//
// Writing is split by what drives it. The writes a model drives are here; the
// one-line writes the window makes when its own state changes stay beside the
// properties they persist, where a reader of that property can see them.
//
// Multi-window note: the settings store is process-global, so window geometry
// remains last-writer-wins. The state that belongs to what a reader was doing
// in a vault (open note and position, sidebar view, panel widths and note sort)
// is stored in root.viewState keyed by NoteCollection.rootPath below. Two
// windows showing the same root cannot exist because the vault lock and window
// registry prevent that; switching away therefore leaves one unambiguous last
// state for the return trip. Per-vault window geometry remains a follow-up.
Item {
    id: persistence

    // Wired by main.qml. The three panels own state of their own that the
    // restore has to reach.
    property var appWindow
    property var toolbar
    property var findBar
    // Named apart from its id so the wiring cannot resolve to this property.
    property var sidebarPanel
    property string currentRootPath: ""
    property string currentDocumentRelPath: ""
    property real pendingScrollY: 0

    function rootStates() {
        return AppSettings.value("root.viewState", {})
    }
    function saveRootState(rootPath) {
        if (rootPath === "")
            return
        var states = rootStates()
        states[rootPath] = {
            sidebarView: persistence.appWindow.sidebarView,
            sidebarWidth: persistence.appWindow.sidebarWidth,
            noteListWidth: persistence.appWindow.noteListWidth,
            sortMode: NoteListModel.sortMode,
            sortAscending: NoteListModel.ascending,
            openFile: persistence.currentDocumentRelPath,
            scrollY: persistence.appWindow.editorContentY()
        }
        AppSettings.setValue("root.viewState", states)
    }
    function restoreRootState(rootPath) {
        if (rootPath === "")
            return
        var state = rootStates()[rootPath]
        if (!state)
            return
        persistence.appWindow.sidebarView = state.sidebarView || "notes"
        persistence.appWindow.sidebarWidth = Number(state.sidebarWidth || 200)
        persistence.appWindow.noteListWidth = Number(state.noteListWidth || 260)
        NoteListModel.sortMode = state.sortMode || "modified"
        NoteListModel.ascending = state.sortAscending === true
        persistence.currentDocumentRelPath = state.openFile || ""
        if (persistence.currentDocumentRelPath !== ""
                && NoteCollection.noteInfo(
                    persistence.currentDocumentRelPath).relPath !== undefined)
            NoteCollection.setLastOpenNote(persistence.currentDocumentRelPath)
        persistence.pendingScrollY = Number(state.scrollY || 0)
        persistence.applyPendingScroll()
    }
    function applyPendingScroll() {
        if (!StartupController.finished)
            return
        Qt.callLater(function() {
            persistence.appWindow.setEditorContentY(persistence.pendingScrollY)
        })
    }

    // Moving or resizing fires per event, so the write is debounced, and only
    // the ordinary windowed geometry is recorded: a maximized session should
    // restore maximized over the last size it had windowed.
    Timer {
        id: geometrySaveTimer
        interval: 400
        onTriggered: {
            if (!persistence.appWindow.geometryRestored
                || persistence.appWindow.visibility !== Window.Windowed)
                return
            AppSettings.setValue("window.width", persistence.appWindow.width)
            AppSettings.setValue("window.height", persistence.appWindow.height)
            AppSettings.setValue("window.x", persistence.appWindow.x)
            AppSettings.setValue("window.y", persistence.appWindow.y)
        }
    }

    // The window's move and resize handlers call this; they stay on the
    // window because they are its own signals.
    function scheduleGeometrySave() { geometrySaveTimer.restart() }

    // A stored position is only reapplied when its rect still lands on a
    // connected screen; monitors change between sessions.
    function savedRectOnScreen(sx, sy, sw, sh) {
        // Qt.application.screens is documented QML API, but the type
        // description Qt ships for QQmlApplication does not list it, so the
        // linter cannot see it. The suppression below is scoped to this one
        // line rather than disabling the category or excluding this file.
        // qmllint disable missing-property
        var screens = Qt.application.screens
        // qmllint enable missing-property
        for (var i = 0; i < screens.length; i++) {
            var s = screens[i]
            if (sx + sw > s.virtualX + 40
                && sx < s.virtualX + s.width - 40
                && sy + 24 > s.virtualY
                && sy < s.virtualY + s.height - 40)
                return true
        }
        return false
    }

    // Read the whole session back, once, at startup. It is a function rather
    // than inline completion code so the integration suite can preset values
    // and exercise the read path.
    function restore() {
        persistence.appWindow.panelsVisible =
            AppSettings.value("panels.visible", true)
        persistence.appWindow.navigationRailsVisible =
            AppSettings.value("view.navigationRails", false)
        BlockMenuModel.setRecentTypes(
            AppSettings.value("blockMenu.recent", []))
        MathCommandModel.setRecentCommands(
            AppSettings.value("math.recentCommands", []))
        // Read both sort keys before assigning either: the first
        // assignment fires projectionChanged, whose save handler below
        // would overwrite the not-yet-read second key.
        var sortMode = AppSettings.value("noteList.sortMode", "modified")
        var sortAscending = AppSettings.value("noteList.ascending", false)
        NoteListModel.sortMode = sortMode
        NoteListModel.ascending = sortAscending
        persistence.sidebarPanel.applyPersistedSearchHistory()
        persistence.findBar.applyPersistedOptions()
        persistence.appWindow.sidebarWidth =
            AppSettings.value("panels.sidebarWidth", 200)
        persistence.appWindow.noteListWidth =
            AppSettings.value("panels.noteListWidth", 260)
        persistence.appWindow.sidebarCollapsed =
            AppSettings.value("panels.sidebarCollapsed", false)
        persistence.appWindow.noteListCollapsed =
            AppSettings.value("panels.noteListCollapsed", false)
        var rootPath = NoteCollection.isOpen ? NoteCollection.rootPath : ""
        var sidebarViews = AppSettings.value("sidebar.viewByRoot", {})
        persistence.appWindow.sidebarView = rootPath !== ""
            ? (sidebarViews[rootPath] || "notes") : "notes"
        persistence.appWindow.statusBarVisible =
            AppSettings.value("view.statusBar", true)
        persistence.appWindow.bottomDockHeight = Math.max(
            110, Number(AppSettings.value("dock.height", 220)))
        persistence.appWindow.bottomDockCollapsed =
            AppSettings.value("dock.collapsed", false)
        persistence.appWindow.outlineVisible =
            AppSettings.value("view.outline", false)
        persistence.appWindow.backlinksVisible =
            AppSettings.value("view.backlinks", false)
        DocumentOutline.levelMask =
            AppSettings.value("view.outlineLevels", 0xF)
        // Focus/typewriter modes (view states). Focus mode is NOT restored on
        // launch (starting full-screen with no chrome would disorient); it is
        // a per-session toggle. Typewriter mode does restore.
        persistence.appWindow.typewriterMode =
            AppSettings.value("view.typewriterMode", false)
        persistence.toolbar.applyPersistedCustomization()
        // Oversized-file guard cap: adjustable without a rebuild, next
        // to the autosave settings.
        DocumentManager.maxOpenFileSizeMiB =
            AppSettings.value("maxOpenFileSizeMiB", 10)
        // Window geometry: size restores unconditionally (with a sanity
        // floor), position only when still on a connected screen.
        var winW = Number(AppSettings.value("window.width", 0))
        var winH = Number(AppSettings.value("window.height", 0))
        if (winW >= 500 && winH >= 350) {
            persistence.appWindow.width = winW
            persistence.appWindow.height = winH
        }
        var winX = Number(AppSettings.value("window.x", -1e9))
        var winY = Number(AppSettings.value("window.y", -1e9))
        if (winX > -1e8
                && persistence.savedRectOnScreen(
                    winX, winY, persistence.appWindow.width,
                    persistence.appWindow.height)) {
            persistence.appWindow.x = winX
            persistence.appWindow.y = winY
        }
        if (AppSettings.value("window.maximized", false))
            persistence.appWindow.visibility = Window.Maximized
        persistence.appWindow.geometryRestored = true
        persistence.currentRootPath = NoteCollection.isOpen
            ? NoteCollection.rootPath : ""
        if (persistence.currentRootPath !== "") {
            var currentAbs = DocumentManager.currentFilePath
            persistence.currentDocumentRelPath = currentAbs !== ""
                ? NoteCollection.relativePath(currentAbs) : ""
            persistence.restoreRootState(persistence.currentRootPath)
        }
    }

    Connections {
        target: BlockMenuModel
        function onRecentChanged() {
            AppSettings.setValue("blockMenu.recent",
                                 BlockMenuModel.recentTypes())
        }
    }

    Connections {
        target: MathCommandModel
        function onRecentChanged() {
            AppSettings.setValue("math.recentCommands",
                                 MathCommandModel.recentCommands())
        }
    }

    // The outline's level filter (§17.1), a view setting like the panel
    // widths, but changed from inside the outline pane rather than here.
    Connections {
        target: DocumentOutline
        function onLevelMaskChanged() {
            AppSettings.setValue("view.outlineLevels", DocumentOutline.levelMask)
        }
    }

    // projectionChanged also fires for scope and tag-filter changes;
    // setValue no-ops when the value is unchanged, so saving both sort
    // keys on every projection change is idempotent.
    Connections {
        target: NoteListModel
        function onProjectionChanged() {
            AppSettings.setValue("noteList.sortMode", NoteListModel.sortMode)
            AppSettings.setValue("noteList.ascending", NoteListModel.ascending)
        }
    }

    Connections {
        target: NoteCollection
        function onRootChanged() {
            persistence.saveRootState(persistence.currentRootPath)
            persistence.currentRootPath = NoteCollection.isOpen
                ? NoteCollection.rootPath : ""
            persistence.currentDocumentRelPath = ""
            persistence.pendingScrollY = 0
            persistence.restoreRootState(persistence.currentRootPath)
        }
    }

    Connections {
        target: DocumentManager
        function onCurrentFilePathChanged() {
            if (!NoteCollection.isOpen || DocumentManager.currentFilePath === "")
                return
            var rel = NoteCollection.relativePath(DocumentManager.currentFilePath)
            if (rel !== "")
                persistence.currentDocumentRelPath = rel
        }
    }

    Connections {
        target: StartupController
        function onFinishedChanged() {
            if (StartupController.finished)
                persistence.applyPendingScroll()
        }
    }
}
