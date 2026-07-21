// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Delegates and Loaders throughout this file are separate component
// scopes. Binding them lets each address the ids and model roles it
// uses instead of relying on injection.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import Qt.labs.qmlmodels
import Kvit 1.0

KvitShell {
    id: root

    // First-run default; every later launch restores the persisted
    // geometry below. 800x600 clipped the toolbar's right end.
    width: Math.min(1100, Screen.desktopAvailableWidth > 0
                    ? Screen.desktopAvailableWidth - 40 : 1100)
    height: Math.min(720, Screen.desktopAvailableHeight > 0
                     ? Screen.desktopAvailableHeight - 60 : 720)
    visible: true
    title: {
        var name = DocumentManager ? DocumentManager.currentFileName : "Kvit Notes"
        // Collection mode: the note title is the file name without ".md".
        if (currentNoteRelPath !== "" && name.toLowerCase().endsWith(".md"))
            name = name.substring(0, name.length - 3)
        return (DocumentManager && DocumentManager.isDirty ? "* " : "")
            + name + " - Kvit Notes"
    }

    // The block that most recently held editing focus (§3.1's "current
    // block"): the Shift+Click block-range anchor. Maintained by the
    // delegates on focus gain — not listView.currentIndex, whose
    // assignment moves focus into the delegate root.
    property int lastFocusedBlock: 0

    // ---- The notes collection -------------------------------------------
    // Collection mode shows the sidebar and note list; single-file mode
    // (file argument, or the test harness's unopened collection) keeps
    // the pre-Phase-8 editor-only geometry.
    readonly property bool collectionOpen: NoteCollection && NoteCollection.isOpen
    property bool panelsVisible: true

    // Layout state (features.md §9.1): per-panel widths set by the seam
    // handles, and independent collapse; all persisted.
    property int sidebarWidth: 200
    property int noteListWidth: 260
    property bool sidebarCollapsed: false
    property bool noteListCollapsed: false

    onSidebarWidthChanged:
        AppSettings.setValue("panels.sidebarWidth", sidebarWidth)
    onNoteListWidthChanged:
        AppSettings.setValue("panels.noteListWidth", noteListWidth)
    onSidebarCollapsedChanged:
        AppSettings.setValue("panels.sidebarCollapsed", sidebarCollapsed)
    onNoteListCollapsedChanged:
        AppSettings.setValue("panels.noteListCollapsed", noteListCollapsed)

    // Window geometry, persisted like the panel layout. Saves debounce
    // through a timer (move/resize fire per-event) and record only the
    // normal windowed geometry, so a maximized session restores maximized
    // over the last windowed size. Nothing saves until the persisted
    // geometry has been applied, or the defaults would overwrite it.
    property bool geometryRestored: false
    Timer {
        id: geometrySaveTimer
        interval: 400
        onTriggered: {
            if (!root.geometryRestored
                || root.visibility !== Window.Windowed)
                return
            AppSettings.setValue("window.width", root.width)
            AppSettings.setValue("window.height", root.height)
            AppSettings.setValue("window.x", root.x)
            AppSettings.setValue("window.y", root.y)
        }
    }
    onWidthChanged: geometrySaveTimer.restart()
    onHeightChanged: geometrySaveTimer.restart()
    onXChanged: geometrySaveTimer.restart()
    onYChanged: geometrySaveTimer.restart()
    onVisibilityChanged: {
        if (!geometryRestored)
            return
        // Full screen (focus mode) and minimized leave the flag alone.
        if (visibility === Window.Maximized)
            AppSettings.setValue("window.maximized", true)
        else if (visibility === Window.Windowed)
            AppSettings.setValue("window.maximized", false)
    }
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

    // §9.7 status-bar visibility (view menu), persisted.
    property bool statusBarVisible: true
    // What every bottom-anchored region has to clear: the status bar plus an
    // extension bottom bar when a module fills that slot (zero otherwise).
    readonly property int bottomChromeHeight:
        (statusBar.visible ? statusBar.height : 0)
        + (extensionBottomBar.visible ? extensionBottomBar.height : 0)
    onStatusBarVisibleChanged:
        AppSettings.setValue("view.statusBar", statusBarVisible)

    // features.md §17.1 document outline pane: a right-side dock listing
    // the document's headings, toggled from the view menu (Ctrl+Shift+O),
    // persisted. Document-level, so it works in single-file mode too.
    property bool outlineVisible: false
    property int outlineWidth: 240
    // Backlinks pane; sits left of the outline when both are open.
    property bool backlinksVisible: false
    property int backlinksWidth: 260
    onBacklinksVisibleChanged:
        AppSettings.setValue("view.backlinks", backlinksVisible)
    onOutlineVisibleChanged:
        AppSettings.setValue("view.outline", outlineVisible)

    // features.md §16.1 focus mode: hide all chrome (toolbar, side
    // panels, outline, status bar), center the editor column, and go
    // full-screen. A composition of the panel toggles plus a window-
    // state flip — presentation only, no document behavior. Escape or the
    // shortcut exits. §16.2 typewriter mode is independent and composable:
    // it keeps the caret line centered and fades non-caret blocks. Both
    // persist.
    property bool focusMode: false
    property bool typewriterMode: false
    onFocusModeChanged: {
        AppSettings.setValue("view.focusMode", focusMode)
        root.visibility = focusMode ? Window.FullScreen : Window.Windowed
        A11y.announceMode(qsTr("Focus mode"), focusMode)   // §14.2
    }
    onTypewriterModeChanged: {
        AppSettings.setValue("view.typewriterMode", typewriterMode)
        A11y.announceMode(qsTr("Typewriter mode"), typewriterMode)   // §14.2
        if (typewriterMode)
            Qt.callLater(function() {
                if (appToolbar.targetBlock)
                    root.centerCaretLine(appToolbar.targetBlock)
            })
    }
    // The block index holding the caret (-1 when the editor is unfocused);
    // typewriter mode fades every other block. Off the keystroke path — it
    // changes only when focus moves between blocks.
    readonly property int caretBlockIndex:
        appToolbar.targetBlock ? appToolbar.targetBlock.index : -1

    // When the caret moves to a new block (not just within one), recenter it.
    // onCursorRectangleChanged in the delegate catches within-block moves, but
    // a focus change can settle after that signal, so this covers the handoff.
    onCaretBlockIndexChanged: {
        if (typewriterMode && caretBlockIndex >= 0)
            Qt.callLater(function() {
                if (appToolbar.targetBlock)
                    root.centerCaretLine(appToolbar.targetBlock)
            })
    }

    // Center the caret's line in the editor viewport (typewriter mode).
    // Generalizes the find bar's scroll-into-view to "put the caret line at
    // mid-viewport"; the scroll animates only while typewriter mode is on.
    function centerCaretLine(item) {
        if (!item || !item.rectForMarkdownPosition || !item.markdownCursor)
            return
        var rect = item.rectForMarkdownPosition(item.markdownCursor())
        var yInContent = item.y + rect.y
        var target = yInContent - blockListView.height / 2 + rect.height / 2
        var maxY = Math.max(0, blockListView.contentHeight - blockListView.height)
        blockListView.contentY = Math.max(0, Math.min(target, maxY))
    }

    function openSettingsDialog() { settingsDialog.open() }

    // ---- Keyboard accessibility: focus and pane navigation (§14.1) ----
    // Skip-navigation: land on the current (or first) editor block, bypassing
    // the chrome. Bound to F6's pane cycle and the View menu.
    function focusEditor() {
        var idx = Math.max(0, Math.min(root.lastFocusedBlock, BlockModel.count - 1))
        blockListView.currentIndex = idx
        var item = (blockListView.itemAtIndex(idx) as BlockDelegateBase)
        if (item && item.focusAtStart)
            item.focusAtStart()
    }
    // Which major pane last took focus (0 sidebar, 1 note list, 2 editor), so
    // F6 can cycle to the next visible one — the standard desktop region key.
    property int focusedPane: 2
    function focusPane(p) {
        root.focusedPane = p
        if (p === 0 && !root.sidebarCollapsed && root.collectionOpen)
            sidebar.focusPane()
        else if (p === 1 && !root.noteListCollapsed && root.collectionOpen)
            noteListPane.focusPane()
        else
            focusEditor()
    }
    function cyclePane() {
        var order = []
        if (root.collectionOpen && !root.sidebarCollapsed) order.push(0)
        if (root.collectionOpen && !root.noteListCollapsed) order.push(1)
        order.push(2)  // the editor is always present
        var cur = order.indexOf(root.focusedPane)
        focusPane(order[(cur + 1) % order.length])
    }
    Shortcut {
        sequence: "F6"
        onActivated: root.cyclePane()
    }

    // Live-region announcements for dynamic changes (§14.2). Save state speaks
    // only the meaningful "Saved" transition (not every keystroke's dirtying);
    // the search match count speaks while the find bar is active.
    Connections {
        target: DocumentManager
        function onCurrentFilePathChanged() {
            Qt.callLater(root.refreshSessionBaseline)
        }

        function onIsDirtyChanged() {
            if (!DocumentManager.isDirty)
                A11y.announceSaveState(false)
        }
    }
    Connections {
        target: DocumentSearch
        function onRevisionChanged() {
            if (DocumentSearch.query !== "")
                A11y.announceMatchCount(DocumentSearch.matchCount)
        }
    }

    // features.md §19.2 session word-count tracker: the document's
    // word count when the note opened; the statistics popover shows the delta.
    // Ephemeral, reset per note.
    property int sessionStartWords: 0
    function refreshSessionBaseline() {
        sessionStartWords = BlockModel ? BlockModel.documentWordCount : 0
    }

    // A transient status-bar note: shown briefly, e.g.
    // when an internal link resolves or dangles. Cleared by its timer.
    property string transientStatus: ""
    Timer {
        id: transientStatusTimer
        interval: 3500
        onTriggered: root.transientStatus = ""
    }
    function showTransientStatus(msg) {
        root.transientStatus = msg
        transientStatusTimer.restart()
    }

    // The note-list's bulk selection, for the export dialog's selection scope.
    function noteListSelectedPaths() {
        return noteListPane && noteListPane.selectedPaths
            ? noteListPane.selectedPaths : []
    }
    // A file:// URL to a local filesystem path.

    // Delegates ask for shell-level actions through AppActions rather than
    // reaching this window by name. Each handler forwards to the function
    // that already implemented it, so the behaviour is the same code as
    // before — only the route changed.
    Connections {
        target: AppActions
        function onScrollToBlockRequested(index) { root.scrollToBlock(index) }
        function onOpenNoteByPathRequested(relPath) { root.openNoteByPath(relPath) }
        function onCenterCaretLineRequested(item) { root.centerCaretLine(item) }
        function onTextContextMenuRequested(target) { root.openTextContextMenu(target) }
        function onLinkContextMenuRequested(target) { root.openLinkContextMenu(target) }
        function onBlockHandleMenuRequested(target) { root.openBlockHandleMenu(target) }
        function onInsertImageRequested(index) { root.insertImageIntoBlock(index) }
        function onInsertEmbedRequested(index) { root.insertEmbedIntoBlock(index) }
        function onInsertTableRequested(index) { root.insertTableIntoBlock(index) }
        function onLightboxRequested(source, alt) { root.openLightbox(source, alt) }
        function onTransientStatusRequested(message) { root.showTransientStatus(message) }
        // Objects this window owns. A delegate asks for the effect; which
        // child provides it stays private to the shell.
        function onSelectionFocusRequested() { selectionKeyHandler.forceActiveFocus() }
        function onOpenLinkRequested(url) { linkOpener.activate(url) }
        function onBlockMenuRequested(index, mode, area) { blockMenu.openForBlock(index, mode, area) }
        function onMathCommandMenuRequested(host, area, displayMath) {
            mathCommandMenu.openForHost(host, area, displayMath)
        }
        function onWikiLinkMenuRequested(host, area) { wikiLinkMenu.openForHost(host, area) }
        function onEditLinkRequested(index, start, end, text, url, removable) {
            linkDialog.openForEdit(index, start, end, text, url, removable)
        }
        function onInsertLinkRequested(index, start, end, text) {
            linkDialog.openForInsert(index, start, end, text)
        }
    }

    // Scroll a block to the top of the editor viewport and focus it — the
    // find-bar's scroll-into-view generalized, reused by internal-link
    // navigation and the outline/TOC click-to-scroll.
    function scrollToBlock(idx) {
        if (idx < 0 || !BlockModel || idx >= BlockModel.count)
            return
        blockListView.currentIndex = idx
        blockListView.positionViewAtIndex(idx, ListView.Beginning)
        Qt.callLater(function() {
            var item = (blockListView.itemAtIndex(idx) as BlockDelegateBase)
            if (item && item.focusAtStart)
                item.focusAtStart()
        })
    }

    // ---- Persisted session state --------------------------------------
    // The states earlier phases left session-scoped, read back from the
    // settings store once at startup. A separate function (rather than
    // inline onCompleted code) so integration tests can preset values
    // and exercise the read path. Writes happen where each state
    // changes: the handlers and Connections below.
    function applyPersistedSessionState() {
        panelsVisible = AppSettings.value("panels.visible", true)
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
        sidebar.applyPersistedSearchHistory()
        findBar.applyPersistedOptions()
        sidebarWidth = AppSettings.value("panels.sidebarWidth", 200)
        noteListWidth = AppSettings.value("panels.noteListWidth", 260)
        sidebarCollapsed =
            AppSettings.value("panels.sidebarCollapsed", false)
        noteListCollapsed =
            AppSettings.value("panels.noteListCollapsed", false)
        statusBarVisible = AppSettings.value("view.statusBar", true)
        outlineVisible = AppSettings.value("view.outline", false)
        backlinksVisible = AppSettings.value("view.backlinks", false)
        DocumentOutline.levelMask =
            AppSettings.value("view.outlineLevels", 0xF)
        // Focus/typewriter modes (view states). Focus mode is NOT restored on
        // launch (starting full-screen with no chrome would disorient); it is
        // a per-session toggle. Typewriter mode does restore.
        typewriterMode = AppSettings.value("view.typewriterMode", false)
        appToolbar.applyPersistedCustomization()
        // Oversized-file guard cap: adjustable without a rebuild, next
        // to the autosave settings.
        DocumentManager.maxOpenFileSizeMiB =
            AppSettings.value("maxOpenFileSizeMiB", 10)
        // Window geometry: size restores unconditionally (with a sanity
        // floor), position only when still on a connected screen.
        var winW = Number(AppSettings.value("window.width", 0))
        var winH = Number(AppSettings.value("window.height", 0))
        if (winW >= 500 && winH >= 350) {
            width = winW
            height = winH
        }
        var winX = Number(AppSettings.value("window.x", -1e9))
        var winY = Number(AppSettings.value("window.y", -1e9))
        if (winX > -1e8 && savedRectOnScreen(winX, winY, width, height)) {
            x = winX
            y = winY
        }
        if (AppSettings.value("window.maximized", false))
            root.visibility = Window.Maximized
        geometryRestored = true
    }
    Component.onCompleted: {
        applyPersistedSessionState()
        refreshSessionBaseline()
    }

    onPanelsVisibleChanged:
        AppSettings.setValue("panels.visible", panelsVisible)

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

    // Persist the outline level filter; keep the caret's section lit as the
    // current block changes (the section highlight is off the keystroke path).
    Connections {
        target: DocumentOutline
        function onLevelMaskChanged() {
            AppSettings.setValue("view.outlineLevels", DocumentOutline.levelMask)
        }
        // A table-of-contents fence's stored body is derived from the
        // headings: keep it current so the file reads correctly
        // elsewhere and export/serialize see the right list. The delegate
        // renders the live outline directly, so this is persistence only —
        // and it bypasses the undo stack (updateContentSilently), so it never
        // spawns undo entries or dirties a freshly-loaded note.
        function onRevisionChanged() { tocSyncTimer.restart() }
    }
    Connections {
        target: BlockModel
        function onTocBlockIndexesChanged() { tocSyncTimer.restart() }
    }
    Timer {
        id: tocSyncTimer
        interval: 50
        onTriggered: root.syncTocBlocks()
    }
    function syncTocBlocks() {
        if (!BlockModel || BlockModel.tocBlockCount === 0)
            return
        var tocIndexes = BlockModel.tocBlockIndexes()
        if (tocIndexes.length === 0)
            return

        var perfOn = PerfLog && PerfLog.enabled
        var scanned = 0
        var updated = 0
        if (perfOn)
            PerfLog.begin("toc.sync", {
                "blocks": BlockModel.count,
                "tocBlocks": tocIndexes.length
            })
        try {
            var toc = DocumentOutline.tocMarkdown()
            for (var n = 0; n < tocIndexes.length; n++) {
                var i = tocIndexes[n]
                scanned++
                var b = BlockModel.blockAt(i)
                if (b && b.blockType === 8 && b.language === "toc"
                    && b.content !== toc) {
                    BlockModel.updateContentSilently(i, toc)
                    updated++
                }
            }
        } finally {
            if (perfOn)
                PerfLog.end("toc.sync", {
                    "scanned": scanned,
                    "updated": updated
                })
        }
    }
    Connections {
        target: blockListView
        function onCurrentIndexChanged() {
            DocumentOutline.setCurrentBlock(blockListView.currentIndex)
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

    // relPath of the open note ("" outside collection mode).
    readonly property string currentNoteRelPath:
        collectionOpen && DocumentManager.hasFile
            ? NoteCollection.relativePath(DocumentManager.currentFilePath) : ""

    // Switch notes: save-on-blur, load, undo clears (the existing open()
    // contract), search and selections reset.
    function openNoteByPath(relPath) {
        if (!collectionOpen || relPath === "")
            return false
        var abs = NoteCollection.absolutePath(relPath)
        if (DocumentManager.currentFilePath === abs)
            return true
        DocumentManager.flushPendingEdits()
        // The departing note's scroll position, captured before the
        // switch so back/forward return the reader to it (§3.3). A
        // history-driven reopen is a no-op inside visit().
        var departingY = blockListView.contentY
        // Opening the next note replaces the model, which is the only copy of
        // the current one's unsaved content and undo history. If the save did
        // not succeed - unwritable file, full disk - going ahead destroys work
        // the user never agreed to lose, so stay put and let the error stand.
        if (DocumentManager.isDirty && DocumentManager.hasFile) {
            if (!DocumentManager.save())
                return false
        }
        if (findBar.visible)
            findBar.close()
        if (DocumentSelection.hasBlockSelection
            || DocumentSelection.hasTextSelection)
            DocumentSelection.clear()
        if (!DocumentManager.open(DocumentManager.toLocalFileUrl(abs)))
            return false
        NavigationHistory.visit(relPath, departingY)
        NoteCollection.setLastOpenNote(relPath)
        root.lastFocusedBlock = 0
        blockListView.currentIndex = 0
        // Reset the session word tracker to the just-loaded document (the model
        // has finished loading synchronously here).
        Qt.callLater(root.refreshSessionBaseline)
        return true
    }

    // ---- Wiki-link navigation -----------------------------------------

    // Back/forward over the note history; scroll positions restore after
    // the (synchronous) model load settles.
    function navigateBack() {
        var entry = NavigationHistory.goBack(blockListView.contentY)
        if (entry.ok)
            openHistoryEntry(entry)
    }
    function navigateForward() {
        var entry = NavigationHistory.goForward(blockListView.contentY)
        if (entry.ok)
            openHistoryEntry(entry)
    }
    function openHistoryEntry(entry) {
        if (!openNoteByPath(entry.relPath))
            return
        Qt.callLater(function() {
            var maxY = Math.max(0, blockListView.contentHeight
                                   - blockListView.height)
            blockListView.contentY = Math.min(entry.position, maxY)
        })
    }

    // Follow a [[wiki-link]] spec ("target#heading", either part
    // optional). Resolved targets open (then scroll to the heading);
    // unresolved ones are created on click, as Obsidian does — a bare
    // name in the current note's folder, a path-qualified target at its
    // own path.
    function followWikiLink(spec) {
        var hashIdx = spec.indexOf("#")
        var target = (hashIdx >= 0 ? spec.substring(0, hashIdx) : spec).trim()
        var heading = hashIdx >= 0 ? spec.substring(hashIdx + 1).trim() : ""
        if (target === "") {
            if (heading !== "")
                scrollToHeadingText(heading)
            return
        }
        if (!collectionOpen) {
            showTransientStatus(qsTr("Wiki-links need an open collection"))
            return
        }
        var resolution = NoteCollection.wikiTargetResolution(target)
        if (resolution.status === "ambiguous") {
            showTransientStatus(qsTr("Ambiguous link “%1”: %2")
                                .arg(target)
                                .arg(resolution.candidates.join(", ")))
            return
        }
        var relPath = resolution.relPath
        if (resolution.status === "missing") {
            relPath = createWikiTarget(target)
            if (relPath === "")
                return
            showTransientStatus(qsTr("Created “%1”").arg(relPath))
        }
        if (!openNoteByPath(relPath))
            return
        if (heading !== "") {
            Qt.callLater(function() {
                DocumentOutline.rebuildNow()
                scrollToHeadingText(heading)
            })
        }
    }

    // Rename-safe wiki links: planning is read-only; this dialog is the only
    // UI path that authorizes multi-file edits.  The open note is rewritten
    // through DocumentManager as one undoable body replacement.
    property var pendingRenamePlan: null
    property var pendingRenameAfter: null

    function requestNoteRename(relPath, newTitle) {
        beginRenamePlan(NoteCollection.planNoteRename(relPath, newTitle), null)
    }
    function requestNoteMove(relPath, targetFolder) {
        beginRenamePlan(NoteCollection.planNoteMove(relPath, targetFolder), null)
    }
    function requestFolderRename(relPath, newName, afterApply) {
        beginRenamePlan(NoteCollection.planFolderRename(relPath, newName), afterApply)
    }
    function beginRenamePlan(plan, afterApply) {
        if (!plan || !plan.ok)
            return
        pendingRenamePlan = plan
        pendingRenameAfter = afterApply
        if (plan.linkCount > 0)
            renameLinksDialog.open()
        else
            finishRenamePlan(false)
    }
    function executeRenamePlan(planId, updateLinks) {
        DocumentManager.flushPendingEdits()
        var openRelPath = root.currentNoteRelPath
        var openBody = openRelPath !== ""
            ? DocumentSerializer.serialize(BlockModel) : ""
        var wasDirty = DocumentManager.isDirty
        var result = NoteCollection.applyRenamePlan(
            planId, updateLinks, openRelPath, openBody)
        if (!result.ok)
            return result
        if (result.openRewriteCount > 0
                && DocumentManager.restoreBody(result.openBody)
                && !wasDirty)
            DocumentManager.save()
        return result
    }
    function finishRenamePlan(updateLinks) {
        var plan = pendingRenamePlan
        var after = pendingRenameAfter
        if (!plan)
            return
        var result = executeRenamePlan(plan.id, updateLinks)
        pendingRenamePlan = null
        pendingRenameAfter = null
        if (result && result.ok && after)
            after(result)
        if (result && ((result.skipped && result.skipped.length > 0)
                       || (result.failed && result.failed.length > 0))) {
            rewriteResultDialog.planId = plan.id
            rewriteResultDialog.skipped = result.skipped
            rewriteResultDialog.failed = result.failed
            rewriteResultDialog.open()
        }
    }

    // A wiki-link's #heading part is raw heading text; slug it through
    // the shared slug function before outline lookup.
    function scrollToHeadingText(heading) {
        var idx = DocumentOutline.blockIndexForSlug(
            DocumentOutline.slugForText(heading))
        if (idx >= 0)
            scrollToBlock(idx)
        else
            showTransientStatus(qsTr("No heading “%1”").arg(heading))
    }

    function createWikiTarget(target) {
        var folder
        var title
        var slash = target.lastIndexOf("/")
        if (slash >= 0) {
            // Path-qualified target: materialize its folder chain first.
            var parts = target.substring(0, slash).split("/")
            var accumulated = ""
            for (var i = 0; i < parts.length; ++i) {
                var next = accumulated === ""
                    ? parts[i] : accumulated + "/" + parts[i]
                if (NoteCollection.folderRelPaths().indexOf(next) < 0)
                    NoteCollection.createFolder(accumulated, parts[i])
                accumulated = next
            }
            folder = accumulated
            title = target.substring(slash + 1)
        } else {
            folder = currentNoteRelPath.indexOf("/") >= 0
                ? currentNoteRelPath.substring(
                      0, currentNoteRelPath.lastIndexOf("/"))
                : ""
            title = target
        }
        if (title.toLowerCase().lastIndexOf(".md")
                === title.length - 3 && title.length > 3)
            title = title.substring(0, title.length - 3)
        return NoteCollection.createNote(folder, title)
    }

    // A clicked global-search result (§8.4 "open note at match
    // location"): open the note, then hand off to the find bar —
    // the query seeds DocumentSearch and the clicked occurrence becomes
    // the current match through the cursor-seeding rule.
    function openSearchResult(relPath, blockIndex, displayStart) {
        var mdPos = CollectionSearch.markdownPosition(relPath, blockIndex,
                                                      displayStart)
        if (!openNoteByPath(relPath))
            return
        sidebar.commitRecentSearch(CollectionSearch.query)
        findBar.openAt(CollectionSearch.query, blockIndex, mdPos)
    }

    // Ctrl+N in collection mode: a new note in the current folder scope
    // (§13.4 New Note), opened immediately.
    function createNoteInCurrentScope() {
        if (!collectionOpen)
            return
        var folder = NoteListModel.scope === "folder"
            ? NoteListModel.folderPath : ""
        var relPath = NoteCollection.createNote(folder, "")
        if (relPath !== "") {
            openNoteByPath(relPath)
            var item = (blockListView.itemAtIndex(0) as BlockDelegateBase)
            if (item && item.focusAtStart)
                item.focusAtStart()
        }
    }

    // features.md §18 create a note from a template: a new note in
    // the current scope, titled by the template, with the template's expanded
    // body loaded and its front-matter (tags, favorite) carried through.
    function createFromTemplate(templateName) {
        if (!collectionOpen)
            return ""
        var folder = NoteListModel.scope === "folder"
            ? NoteListModel.folderPath : ""
        var relPath = NoteCollection.createNote(folder, templateName)
        if (relPath === "")
            return ""
        var title = NoteCollection.noteInfo(relPath).title
        var inst = NoteTemplates.instantiate(templateName, title)
        if (!openNoteByPath(relPath))
            return relPath
        // The note is open and empty; load the expanded body, then apply the
        // template's metadata and save through the normal path.
        DocumentSerializer.loadIntoModel(BlockModel, inst.body || "")
        var tags = inst.tags || []
        for (var i = 0; i < tags.length; i++)
            NoteCollection.addTag(relPath, tags[i])
        if (inst.favorite === true)
            NoteCollection.setFavorite(relPath, true)
        DocumentManager.save()
        Qt.callLater(function() {
            var item = (blockListView.itemAtIndex(0) as BlockDelegateBase)
            if (item && item.focusAtStart)
                item.focusAtStart()
        })
        return relPath
    }

    // "Save current note as template": copy the open note (front-matter and
    // body) into .kvit/templates under the given name.
    function saveCurrentNoteAsTemplate(name) {
        if (!collectionOpen || currentNoteRelPath === "")
            return false
        DocumentManager.flushPendingEdits()
        // The on-disk note text (front-matter + serialized body) is the
        // template; save first so the file reflects the current buffer.
        if (DocumentManager.isDirty)
            DocumentManager.save()
        var fm = NoteCollection.frontMatterFor(currentNoteRelPath)
        var full = (fm ? fm : "") + DocumentSerializer.serialize(BlockModel)
        return NoteTemplates.writeTemplate(name, full)
    }

    // Map a scene point to {index, mdPos, inText} on the block list.
    // Pointer positions above, below, or between blocks resolve to the
    // nearest block edge so a selection drag never loses its target.
    function blockPositionAt(sceneX, sceneY) {
        if (!BlockModel || BlockModel.count === 0)
            return null
        var pos = blockListView.contentItem.mapFromItem(null, sceneX, sceneY)
        if (pos.y < 0)
            return { index: 0, mdPos: 0, inText: false }
        if (pos.y >= blockListView.contentHeight) {
            var last = BlockModel.count - 1
            return { index: last, mdPos: BlockModel.getContent(last).length,
                     inText: false }
        }
        var cx = Math.max(1, Math.min(pos.x, blockListView.width - 1))
        var idx = blockListView.indexAt(cx, pos.y)
        if (idx < 0) {
            // In the spacing gap: attach to the block just above
            idx = blockListView.indexAt(cx, Math.max(0, pos.y - blockListView.spacing))
            if (idx < 0)
                return null
            return { index: idx, mdPos: BlockModel.getContent(idx).length,
                     inText: false }
        }
        var item = (blockListView.itemAtIndex(idx) as BlockDelegateBase)
        if (!item || !item.markdownPositionAt)
            return { index: idx, mdPos: 0, inText: false }
        return { index: idx,
                 mdPos: item.markdownPositionAt(sceneX, sceneY),
                 inText: item.pointInText ? item.pointInText(sceneX, sceneY) : false }
    }

    readonly property color backgroundColor: Theme.windowBackground
    readonly property color blockBackgroundColor: Theme.windowBackground
    readonly property color blockBorderColor: Theme.border
    readonly property color focusedBorderColor: Theme.accent
    readonly property color textColor: Theme.textPrimary

    color: root.backgroundColor

    // Qt Quick Controls (buttons, fields, scrollbars, menus) restyle
    // through palette propagation — one binding set instead of
    // per-control color work.
    palette {
        window: Theme.panelBackground
        windowText: Theme.textPrimary
        base: Theme.windowBackground
        alternateBase: Theme.listBackground
        text: Theme.textPrimary
        button: Theme.footerBackground
        buttonText: Theme.textPrimary
        highlight: Theme.accent
        highlightedText: Theme.onAccent
        placeholderText: Theme.textDisabled
        mid: Theme.borderStrong
        dark: Theme.textSecondary
        light: Theme.hoverTint
        toolTipBase: Theme.popupBackground
        toolTipText: Theme.textPrimary
    }

    // Global keyboard shortcuts for undo/redo
    // Note: These are backup shortcuts when no TextArea has focus.
    // When a TextArea is focused, BlockDelegate handles Ctrl+Z/Y/Shift+Z directly.
    Shortcut {
        sequences: [StandardKey.Undo]  // Ctrl+Z (and platform variants)
        onActivated: {
            if (UndoStack && UndoStack.canUndo) {
                UndoStack.undo()
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Redo]  // Ctrl+Y or Ctrl+Shift+Z depending on platform
        onActivated: {
            if (UndoStack && UndoStack.canRedo) {
                UndoStack.redo()
            }
        }
    }

    // File shortcuts
    Shortcut {
        sequences: [StandardKey.Save]  // Ctrl+S
        onActivated: {
            if (DocumentManager.hasFile) {
                DocumentManager.saveAsync()
            } else {
                DocumentManager.saveFileDialog()
            }
        }
    }

    // No Save As shortcut: StandardKey.SaveAs resolves to Ctrl+Shift+S,
    // which features.md §13 assigns to strikethrough (the spec's shortcut
    // table gives Save As no binding). Ctrl+S on an untitled document
    // still opens the save dialog.

    // Find bar (features.md §7.1).
    // Application context: these work from a focused block, from the
    // bar's own fields, and from block-selection mode alike.
    Shortcut {
        sequences: [StandardKey.Find] // Ctrl+F
        context: Qt.ApplicationShortcut
        onActivated: findBar.open(false)
    }
    Shortcut {
        // Explicit, not StandardKey.Replace: the platform theme maps
        // that to Ctrl+R or nothing on some Linux desktops, and §7.2
        // names Ctrl+H on Windows/Linux and Cmd+Option+F on macOS.
        sequence: Qt.platform.os === "osx" ? "Meta+Alt+F" : "Ctrl+H"
        context: Qt.ApplicationShortcut
        onActivated: findBar.open(true)
    }
    Shortcut {
        sequences: [StandardKey.FindNext] // F3
        context: Qt.ApplicationShortcut
        onActivated: findBar.findNextShortcut()
    }
    Shortcut {
        sequences: [StandardKey.FindPrevious] // Shift+F3
        context: Qt.ApplicationShortcut
        onActivated: findBar.findPreviousShortcut()
    }

    // Wiki-link navigation: history and the quick switcher, collection
    // mode only.
    Shortcut {
        sequence: "Alt+Left"
        context: Qt.ApplicationShortcut
        enabled: root.collectionOpen
        onActivated: root.navigateBack()
    }
    Shortcut {
        sequence: "Alt+Right"
        context: Qt.ApplicationShortcut
        enabled: root.collectionOpen
        onActivated: root.navigateForward()
    }
    Shortcut {
        sequence: "Ctrl+P"
        context: Qt.ApplicationShortcut
        enabled: root.collectionOpen
        onActivated: quickSwitcher.toggle()
    }
    // Mouse back/forward buttons navigate too. The area accepts ONLY
    // those buttons, so ordinary clicks pass straight through to the UI
    // beneath it.
    MouseArea {
        anchors.fill: parent
        z: 10000
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        enabled: root.collectionOpen
        onClicked: function(mouse) {
            if (mouse.button === Qt.BackButton)
                root.navigateBack()
            else if (mouse.button === Qt.ForwardButton)
                root.navigateForward()
        }
    }

    // Opens link targets (features.md §2.4). Routed through one object so
    // tests can observe activations without launching a browser.
    property alias linkOpener: linkOpener
    QtObject {
        id: linkOpener
        property bool openExternally: true
        signal activated(string url)
        function activate(url) {
            if (!url || url.length === 0)
                return
            activated(url)
            // Wiki-link: kvit-note:target#heading resolves through the
            // collection and opens in-app — creating the note when the
            // target dangles — never a browser.
            if (url.indexOf("kvit-note:") === 0) {
                root.followWikiLink(url.substring(10))
                return
            }
            // Internal document link: #slug resolves
            // through the shared slug function to a heading and scrolls there,
            // rather than opening a browser. An unresolved slug is a
            // recoverable no-op with a status-bar note, never an error.
            if (url.charAt(0) === "#") {
                var slug = url.substring(1)
                var idx = DocumentOutline.blockIndexForSlug(slug)
                if (idx >= 0) {
                    root.scrollToBlock(idx)
                } else {
                    root.showTransientStatus(
                        qsTr("No heading “") + slug + qsTr("”"))
                }
                return
            }
            if (openExternally)
                Qt.openUrlExternally(url)
        }
    }

    // The Ctrl+K link dialog (features.md §2.4): display-text and URL
    // fields; prefilled when invoked inside an existing link; "Remove
    // link" replaces the span with its bare text. All edits go through
    // the model, like every formatting command.
    property alias linkDialog: linkDialog
    Dialog {
        id: linkDialog
        objectName: "linkDialog"
        modal: true
        title: editing ? qsTr("Edit Link") : qsTr("Insert Link")
        anchors.centerIn: parent
        width: 380

        property alias textField: linkTextField
        property alias urlField: linkUrlField
        property int blockIndex: -1
        property int mdStart: -1
        property int mdEnd: -1
        property bool editing: false
        property bool removable: false
        // The document's headings for the "link to heading" mode
        // (features.md §2.4's deferred jump-to-heading). Refreshed on open.
        property var headingTargets: []

        function openForInsert(index, start, end, initialText) {
            editing = false
            removable = false
            blockIndex = index
            mdStart = start
            mdEnd = end
            headingTargets = DocumentOutline.headings()
            linkTextField.text = initialText
            linkUrlField.text = ""
            open()
            linkUrlField.forceActiveFocus()
        }

        function openForEdit(index, start, end, text, url, canRemove) {
            editing = true
            removable = canRemove
            blockIndex = index
            mdStart = start
            mdEnd = end
            headingTargets = DocumentOutline.headings()
            linkTextField.text = text
            linkUrlField.text = url
            open()
            linkUrlField.forceActiveFocus()
        }

        function spliceAndFocus(replacement, cursorMd) {
            var md = BlockModel.getContent(blockIndex)
            BlockModel.updateContent(blockIndex,
                md.substring(0, mdStart) + replacement + md.substring(mdEnd))
            var idx = blockIndex
            Qt.callLater(function() {
                blockListView.currentIndex = idx
                var item = (blockListView.itemAtIndex(idx) as BlockDelegateBase)
                if (item)
                    item.focusAtPosition(cursorMd)
            })
        }

        function removeLink() {
            var text = linkTextField.text
            spliceAndFocus(text, mdStart + text.length)
            close()
        }

        onAccepted: {
            // Brackets in the text or spaces/parens in the URL would
            // produce markdown that no longer parses as one link.
            var text = linkTextField.text.replace(/[\[\]]/g, "")
            var url = linkUrlField.text.replace(/ /g, "%20").replace(/[()]/g, "")
            if (text.length === 0)
                text = url
            if (text.length === 0)
                return
            var link = "[" + text + "](" + url + ")"
            spliceAndFocus(link, mdStart + link.length)
        }

        contentItem: ColumnLayout {
            spacing: 8
            Label { text: qsTr("Text") }
            TextField {
                id: linkTextField
                objectName: "linkDialogTextField"
                Layout.fillWidth: true
            }
            Label { text: qsTr("URL") }
            TextField {
                id: linkUrlField
                objectName: "linkDialogUrlField"
                placeholderText: "https:// or #heading"
                Layout.fillWidth: true
                onAccepted: linkDialog.accept()
            }
            // Link-to-heading mode (§2.4): choosing a heading fills the URL
            // with its #slug (and the text, if empty). Present only when the
            // document has headings.
            Label {
                text: qsTr("Or link to a heading")
                visible: linkDialog.headingTargets.length > 0
            }
            ComboBox {
                id: headingCombo
                objectName: "linkDialogHeadingCombo"
                visible: linkDialog.headingTargets.length > 0
                Layout.fillWidth: true
                textRole: "label"
                model: {
                    var out = [{ label: qsTr("— choose a heading —"),
                                 slug: "", text: "" }]
                    var hs = linkDialog.headingTargets
                    for (var i = 0; i < hs.length; i++) {
                        var indent = ""
                        for (var j = 1; j < hs[i].level; j++)
                            indent += "   "
                        out.push({ label: indent + hs[i].text,
                                   slug: hs[i].slug, text: hs[i].text })
                    }
                    return out
                }
                currentIndex: 0
                onActivated: function(index) {
                    if (index <= 0)
                        return
                    var item = model[index]
                    linkUrlField.text = "#" + item.slug
                    if (linkTextField.text.length === 0)
                        linkTextField.text = item.text
                }
            }
        }

        footer: DialogButtonBox {
            Button {
                objectName: "linkDialogRemoveButton"
                text: qsTr("Remove link")
                visible: linkDialog.editing && linkDialog.removable
                DialogButtonBox.buttonRole: DialogButtonBox.ResetRole
                onClicked: linkDialog.removeLink()
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: qsTr("OK")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }

    // The block-type menu (features.md §4): opened by "/" on an empty
    // block or the gutter plus-button; the target delegate feeds it
    // content changes and forwards its keys. Selection converts through
    // the model; focus is re-established by index afterwards because
    // the conversion may recreate the delegate.
    // The math-command menu: opened by a backslash keystroke in a
    // math-editing context — a MathBlock source editor or a revealed
    // inline $…$ span. The host editor feeds it the query and
    // forwards its keys; selection inserts through the host's
    // applyMathCommand, so this popup owns no text.
    property alias mathCommandMenu: mathCommandMenu
    MathCommandMenu {
        id: mathCommandMenu
    }

    // The [[ completion popup, hosted like the math command menu:
    // passive, driven by the focused block's editor.
    property alias wikiLinkMenu: wikiLinkMenu
    WikiLinkMenu {
        id: wikiLinkMenu
    }

    // The quick switcher: Ctrl+P. Creation via Shift+Enter lands in the
    // current note-list folder scope, like Ctrl+N.
    property alias quickSwitcher: quickSwitcher
    QuickSwitcher {
        id: quickSwitcher
        onNoteChosen: function(relPath) { root.openNoteByPath(relPath) }
        onCreateRequested: function(title) {
            var folder = NoteListModel.scope === "folder"
                ? NoteListModel.folderPath : ""
            var relPath = NoteCollection.createNote(folder, title)
            if (relPath !== "")
                root.openNoteByPath(relPath)
        }
    }

    property alias blockMenu: blockMenu
    BlockMenu {
        id: blockMenu
        onApplied: function(blockIndex, type) {
            Qt.callLater(function() {
                blockListView.currentIndex = blockIndex
                var item = (blockListView.itemAtIndex(blockIndex) as BlockDelegateBase)
                if (item)
                    item.focusAtStart()
            })
        }
    }

    // Cross-block text selection, mouse path (features.md §2.5, §21.3).
    // Each block's TextArea hosts a passive PointHandler that reports its
    // presses and drag moves here — an ancestor-level handler never sees
    // a press the TextArea accepts (pinned by a feasibility test), but
    // the TextArea's OWN
    // handlers do, and a passive grab keeps reporting moves while the
    // TextArea drags its native in-block selection (which IS the anchor
    // block's portion). The coordinator engages when the pointer
    // crosses into another block and disengages when it returns; while
    // engaged it also feeds the edge auto-scroller. Presses in the
    // gutter never reach a TextArea, so they never seed a text drag.
    property alias crossBlockDrag: crossBlockDrag
    QtObject {
        id: crossBlockDrag

        property int pressIndex: -1
        property int pressMd: 0
        property bool engaged: false
        property int clickCount: 1
        property double lastPressAt: 0
        property real lastPressX: 0
        property real lastPressY: 0

        function beginPress(index, mdPos, sceneX, sceneY) {
            // Click multiplicity sets the drag granularity (§21.3):
            // 1 character, 2 word, 3 whole-block
            var now = Date.now()
            var near = Math.abs(sceneX - lastPressX) < 8
                    && Math.abs(sceneY - lastPressY) < 8
            clickCount = (now - lastPressAt < 400 && near)
                ? Math.min(clickCount + 1, 3) : 1
            lastPressAt = now
            lastPressX = sceneX
            lastPressY = sceneY
            pressIndex = index
            pressMd = mdPos
            engaged = false
        }

        function update(sceneX, sceneY) {
            if (pressIndex < 0)
                return
            var hit = root.blockPositionAt(sceneX, sceneY)
            if (hit) {
                if (!engaged && hit.index !== pressIndex) {
                    DocumentSelection.beginTextSelection(pressIndex, pressMd,
                        clickCount >= 3 ? 2 : clickCount === 2 ? 1 : 0)
                    engaged = true
                }
                if (engaged) {
                    if (hit.index === pressIndex) {
                        // Back inside the anchor block: the native
                        // in-block selection takes over again
                        DocumentSelection.clearTextSelection()
                        engaged = false
                    } else {
                        DocumentSelection.updateTextSelectionHead(
                            hit.index, hit.mdPos)
                    }
                }
            }
            if (engaged) {
                edgeScroller.pointerY =
                    blockListView.mapFromItem(null, sceneX, sceneY).y
                edgeScroller.active = true
            } else {
                edgeScroller.active = false
            }
        }

        function endPress() {
            pressIndex = -1
            engaged = false
            edgeScroller.active = false
        }
    }

    // Block drag-and-drop reordering (features.md §3.2, §5.4).
    // Single-block drags live-move the row with undo-bypassing preview
    // moves — the real delegate is
    // §21.4's space-holder while the floating proxy follows the
    // pointer, and the ListView's move/displaced transitions animate
    // the make-room. The drop commits ONE pre-applied command.
    // Multi-block drags (the handle of a selected block) show a drop
    // indicator instead and commit one compound move.
    // Declared by KvitShell; this instance supplies the behaviour and keeps
    // main.qml's scope, so it still drives blockListView and the edge
    // scroller directly.
    blockDrag: BlockDragState {
        id: blockDragState

        property bool active: false
        property bool isMulti: false
        property int sourceIndex: -1    // live position of the dragged row
        property int originalIndex: -1
        property var dragIndexes: []
        property int dragCount: 0
        property int indicatorGap: -1   // multi: gap BEFORE this index

        function begin(index, sceneX, sceneY) {
            isMulti = DocumentSelection.hasBlockSelection
                      && DocumentSelection.isBlockSelected(index)
            if (DocumentSelection.hasBlockSelection && !isMulti)
                DocumentSelection.clear()
            if (DocumentSelection.hasTextSelection)
                DocumentSelection.clearTextSelection()
            sourceIndex = index
            originalIndex = index
            dragIndexes = isMulti ? DocumentSelection.selectedIndexes()
                                  : [index]
            dragCount = dragIndexes.length
            indicatorGap = -1
            active = true
            dragProxy.buildFrom(dragIndexes)
            update(sceneX, sceneY)
        }

        function update(sceneX, sceneY) {
            if (!active)
                return
            dragProxy.moveTo(sceneX, sceneY)
            var pos = blockListView.contentItem.mapFromItem(null, sceneX, sceneY)
            var cx = Math.max(1, Math.min(pos.x, blockListView.width - 1))
            if (isMulti) {
                indicatorGap = gapAt(cx, pos.y)
            } else {
                var idx = blockListView.indexAt(cx,
                    Math.max(0, Math.min(pos.y, blockListView.contentHeight - 1)))
                if (idx >= 0 && idx !== sourceIndex) {
                    var item = (blockListView.itemAtIndex(idx) as BlockDelegateBase)
                    // Move only once the pointer passes the target row's
                    // midpoint, so unequal row heights cannot oscillate
                    if (item) {
                        var centerY = item.y + item.height / 2
                        if ((idx > sourceIndex && pos.y > centerY)
                            || (idx < sourceIndex && pos.y < centerY)) {
                            BlockModel.previewMoveBlock(sourceIndex, idx)
                            sourceIndex = idx
                        }
                    }
                }
            }
            edgeScroller.pointerY =
                blockListView.mapFromItem(null, sceneX, sceneY).y
            edgeScroller.active = true
        }

        function gapAt(cx, cy) {
            if (cy <= 0)
                return 0
            if (cy >= blockListView.contentHeight)
                return BlockModel.count
            var idx = blockListView.indexAt(cx, cy)
            if (idx < 0) {
                idx = blockListView.indexAt(cx,
                    Math.max(0, cy - blockListView.spacing))
                return idx < 0 ? -1 : idx + 1
            }
            var item = (blockListView.itemAtIndex(idx) as BlockDelegateBase)
            if (!item)
                return idx
            return cy > item.y + item.height / 2 ? idx + 1 : idx
        }

        function drop() {
            if (!active)
                return
            if (isMulti) {
                if (indicatorGap >= 0)
                    BlockModel.moveBlocksTo(dragIndexes, indicatorGap)
                // The selection follows the moved blocks by id; keys
                // stay with the selection handler
                selectionKeyHandler.forceActiveFocus()
            } else {
                BlockModel.commitDragMove(originalIndex, sourceIndex)
                blockListView.currentIndex = sourceIndex
            }
            end()
        }

        // Escape cancels (§5.4): the row returns to where the drag
        // started and nothing lands on the undo stack
        function cancel() {
            if (!active)
                return
            if (!isMulti && sourceIndex !== originalIndex)
                BlockModel.previewMoveBlock(sourceIndex, originalIndex)
            end()
        }

        function end() {
            active = false
            sourceIndex = -1
            originalIndex = -1
            indicatorGap = -1
            dragProxy.clear()
            edgeScroller.active = false
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: blockDragState.active
        onActivated: blockDragState.cancel()
    }

    // The floating drag proxy: snapshots of up to three dragged blocks
    // stacked under the pointer, with a count badge for larger
    // selections.
    Item {
        id: dragProxy
        objectName: "dragProxy"
        visible: blockDragState.active
        z: 1000
        width: 300
        height: proxyColumn.height
        opacity: 0.85

        function grabShot(slot, sourceItem) {
            sourceItem.grabToImage(function(result) {
                if (slot < proxyImages.count)
                    proxyImages.setProperty(slot, "shotUrl", result.url.toString())
            })
        }

        function buildFrom(indexes) {
            proxyImages.clear()
            var shots = Math.min(3, indexes.length)
            for (var i = 0; i < shots; i++) {
                var item = (blockListView.itemAtIndex(Number(indexes[i]) as BlockDelegateBase))
                if (!item)
                    continue
                // A delegate can nominate its content item for the shot
                // (the math block nominates the rendered formula): grabbing
                // a full-width row and fitting it into the proxy shrinks
                // narrow content far below the intended 60%.
                var src = (item.dragGrabItem && item.dragGrabItem.visible)
                    ? item.dragGrabItem : item
                proxyImages.append({ shotUrl: "",
                                     shotHeight: Math.round(src.height * 0.6) })
                grabShot(proxyImages.count - 1, src)
            }
        }

        function moveTo(sceneX, sceneY) {
            x = sceneX + 12
            y = sceneY - 10
        }

        function clear() {
            proxyImages.clear()
        }

        ListModel { id: proxyImages }

        Column {
            id: proxyColumn
            spacing: 2
            Repeater {
                model: proxyImages
                Image {
                    id: proxyShot
                    required property real shotHeight
                    required property url shotUrl
                    width: dragProxy.width
                    height: proxyShot.shotHeight
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignLeft
                    source: proxyShot.shotUrl
                }
            }
        }

        Rectangle {
            visible: blockDragState.dragCount > 1
            anchors.left: proxyColumn.right
            anchors.top: proxyColumn.top
            anchors.leftMargin: -12
            anchors.topMargin: -8
            width: 22
            height: 22
            radius: 11
            color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: blockDragState.dragCount
                color: Theme.onAccent
                font.pixelSize: 11
                font.bold: true
            }
        }
    }

    // Keys while a block selection is active (features.md §3.1).
    // Entering block selection focuses this item — the blurred
    // TextArea's reveal collapses and any open block
    // menu dismisses, both intended. Escape/Enter return to editing;
    // plain Up/Down move the collapsed selection; Ctrl+Shift+Up/Down
    // extend it; printable keys are deliberately inert (typing never
    // replaces a block selection).
    property alias findBar: findBar

    property alias selectionKeyHandler: selectionKeyHandler
    Item {
        id: selectionKeyHandler
        objectName: "selectionKeyHandler"

        // Leave selection mode and edit the given block.
        function exitToBlock(idx) {
            DocumentSelection.clear()
            if (idx < 0 || idx >= BlockModel.count)
                idx = Math.max(0, Math.min(blockListView.currentIndex,
                                           BlockModel.count - 1))
            blockListView.currentIndex = idx
            var item = (blockListView.itemAtIndex(idx) as BlockDelegateBase)
            if (item)
                item.focusAtEnd()
        }

        function revealSelectionEdge() {
            var idx = DocumentSelection.lastActiveIndex()
            if (idx >= 0) {
                blockListView.currentIndex = idx
                blockListView.positionViewAtIndex(idx, ListView.Contain)
            }
        }

        // Focus a block on the next tick (delegates may need a frame
        // after a structural change).
        function focusBlockLater(idx, atEnd) {
            Qt.callLater(function() {
                if (BlockModel.count === 0)
                    return
                var i = Math.max(0, Math.min(idx, BlockModel.count - 1))
                blockListView.currentIndex = i
                var item = (blockListView.itemAtIndex(i) as BlockDelegateBase)
                if (item) {
                    if (atEnd)
                        item.focusAtEnd()
                    else
                        item.focusAtStart()
                }
            })
        }

        // The selected blocks as structural markdown, in every Clipboard
        // flavor (§5.1): plain text, rendered HTML for rich-text targets, and
        // the internal marker so pasting back into Kvit is lossless.
        function copyBlocksToClipboard() {
            var md = DocumentSerializer.serializeBlocks(
                BlockModel, DocumentSelection.selectedIndexes())
            Clipboard.setMarkdown(md, MarkdownFormatter.toHtml(md))
        }

        // Remove the selected blocks and land the cursor on the block
        // before the removed run (§3.5).
        function removeSelectedBlocks() {
            var indexes = DocumentSelection.selectedIndexes()
            if (indexes.length === 0)
                return
            var first = Number(indexes[0])
            DocumentSelection.clear()
            BlockModel.removeBlocks(indexes)
            focusBlockLater(first > 0 ? first - 1 : 0, first > 0)
        }

        function selectRange(first, last) {
            DocumentSelection.selectBlock(first)
            if (last > first)
                DocumentSelection.extendBlockSelectionTo(last)
            revealSelectionEdge()
        }

        Keys.onPressed: function(event) {
            if (!DocumentSelection.hasBlockSelection)
                return
            var ctrl = event.modifiers & Qt.ControlModifier
            var shift = event.modifiers & Qt.ShiftModifier

            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
                exitToBlock(DocumentSelection.lastActiveIndex())
                event.accepted = true
                return
            }

            // Delete/Backspace — and Ctrl+Shift+D, §13.3's delete-block
            // shortcut — remove the selection as one undo step (§3.5)
            if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace
                || (event.key === Qt.Key_D && ctrl && shift)) {
                removeSelectedBlocks()
                event.accepted = true
                return
            }

            // Ctrl+D: duplicate the selection below itself; the
            // selection moves to the clones (features.md §3.6)
            if (event.key === Qt.Key_D && ctrl) {
                var clones = BlockModel.duplicateBlocks(
                    DocumentSelection.selectedIndexes())
                if (clones.length > 0)
                    selectRange(Number(clones[0]),
                                Number(clones[clones.length - 1]))
                event.accepted = true
                return
            }

            // Alt+Up/Down: move the selection as a unit (§3.2); the
            // selection follows the moved blocks by id
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && (event.modifiers & Qt.AltModifier)) {
                BlockModel.moveBlocksBy(DocumentSelection.selectedIndexes(),
                                        event.key === Qt.Key_Down ? 1 : -1)
                revealSelectionEdge()
                event.accepted = true
                return
            }

            // Tab/Shift+Tab: indent/outdent the selection's list-family
            // blocks together (§3.3)
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                BlockModel.changeIndentForBlocks(
                    DocumentSelection.selectedIndexes(),
                    event.key === Qt.Key_Tab ? 1 : -1)
                event.accepted = true
                return
            }

            // Ctrl+C / Ctrl+X: the selected blocks as structural
            // markdown (§5.1, §5.2)
            if (event.key === Qt.Key_C && ctrl) {
                copyBlocksToClipboard()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_X && ctrl) {
                copyBlocksToClipboard()
                removeSelectedBlocks()
                event.accepted = true
                return
            }

            // Ctrl+V: paste the Clipboard as typed blocks after the
            // selection (§5.3); the new blocks become the selection
            if (event.key === Qt.Key_V && ctrl) {
                if (Clipboard.hasText) {
                    var indexes = DocumentSelection.selectedIndexes()
                    var insertAt = indexes.length > 0
                        ? Number(indexes[indexes.length - 1]) + 1
                        : BlockModel.count
                    // §5.3 format matrix (internal / HTML / plain); paste-plain
                    // deliberately takes the source's own plain text instead.
                    var pasteText = (shift ? Clipboard.text
                                           : Clipboard.markdown())
                                        .replace(/\r\n/g, "\n")
                    // Oversized-paste guard: the same threshold as file
                    // open — a whale payload gets a confirm dialog instead
                    // of a silent multi-second stall.
                    var capBytes = DocumentManager.maxOpenFileSizeMiB > 0
                        ? DocumentManager.maxOpenFileSizeMiB * 1024 * 1024
                        : 0
                    if (capBytes > 0 && pasteText.length > capBytes) {
                        largePasteConfirmDialog.pendingText = pasteText
                        largePasteConfirmDialog.pendingIndex = insertAt
                        largePasteConfirmDialog.pendingPlain = shift ? true : false
                        largePasteConfirmDialog.open()
                    } else if (shift) {
                        // Ctrl+Shift+V: strip inline formatting and drop the
                        // structure too, so the payload lands as plain
                        // paragraphs (§5.3).
                        // `editorEngine` is an id inside EditableBlock.qml
                        // and has never existed in this scope, so this threw
                        // ReferenceError and the paste silently did nothing.
                        // DocumentStats.displayTextFor is the same operation
                        // as BlockEditorEngine::stripFormatting — both return
                        // displayText(markdown) for non-verbatim content —
                        // and there is no block here, so verbatim is false.
                        var plain = pasteText.split("\n").map(function(line) {
                            return DocumentStats.displayTextFor(line, false)
                        }).join("\n")
                        var plainCount = DocumentSerializer.insertPlainTextAt(
                            BlockModel, insertAt, plain)
                        if (plainCount > 0)
                            selectRange(insertAt, insertAt + plainCount - 1)
                    } else {
                        var count = DocumentSerializer.insertMarkdownAt(
                            BlockModel, insertAt, pasteText)
                        if (count > 0)
                            selectRange(insertAt, insertAt + count - 1)
                    }
                }
                event.accepted = true
                return
            }

            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && ctrl && shift) {
                DocumentSelection.extendBlockSelection(
                    event.key === Qt.Key_Down ? 1 : -1)
                revealSelectionEdge()
                event.accepted = true
                return
            }
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && !ctrl && !shift && !(event.modifiers & Qt.AltModifier)) {
                DocumentSelection.collapseBlockSelection(
                    event.key === Qt.Key_Down ? 1 : -1)
                revealSelectionEdge()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_A && ctrl) {
                DocumentSelection.selectAllBlocks()
                event.accepted = true
                return
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Open]  // Ctrl+O
        onActivated: {
            DocumentManager.flushPendingEdits()
            if (DocumentManager.isDirty) {
                unsavedChangesBeforeOpenDialog.open()
            } else if (root.collectionOpen) {
                // In collection mode, offer to import rather than only open a
                // standalone file.
                openOrImportChoiceDialog.open()
            } else {
                DocumentManager.openFileDialog()
            }
        }
    }

    // Ctrl+O in collection mode: open a file standalone, or import it into the
    // collection (§12.6).
    Dialog {
        id: openOrImportChoiceDialog
        objectName: "openOrImportChoiceDialog"
        modal: true
        title: qsTr("Open a file")
        anchors.centerIn: parent
        width: 320
        contentItem: Label {
            text: qsTr("Open the file on its own, or import it into your "
                + "collection?")
            wrapMode: Text.WordWrap
            padding: 10
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Open standalone")
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: {
                    openOrImportChoiceDialog.close()
                    DocumentManager.openFileDialog()
                }
            }
            Button {
                objectName: "chooseImportButton"
                text: qsTr("Import…")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    openOrImportChoiceDialog.close()
                    importDialog.openDialog()
                }
            }
        }
    }

    // Single-file mode → vault upgrade: confirms, then opens the current
    // file's folder as the collection root. The open file stays open and
    // becomes a note of the new vault.
    Dialog {
        id: createVaultDialog
        objectName: "createVaultDialog"
        modal: true
        title: qsTr("Create a vault")
        anchors.centerIn: parent
        width: 360
        contentItem: Label {
            text: qsTr("Use this file's folder as a vault? The sidebar, "
                + "tags, global search, and wiki links will then work "
                + "across every markdown file in it. The folder's files "
                + "are left exactly as they are.")
            wrapMode: Text.WordWrap
            padding: 10
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: createVaultDialog.close()
            }
            Button {
                objectName: "createVaultConfirmButton"
                text: qsTr("Create vault")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    createVaultDialog.close()
                    var dir = root.currentNoteDir()
                    if (dir !== "")
                        NoteCollection.openRootAsync(dir)
                }
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.New]  // Ctrl+N — New Note (§13.4)
        onActivated: {
            DocumentManager.flushPendingEdits()
            if (root.collectionOpen) {
                root.createNoteInCurrentScope()
            } else if (DocumentManager.isDirty) {
                unsavedChangesBeforeNewDialog.open()
            } else {
                DocumentManager.newDocument()
            }
        }
    }

    // Toggle Sidebar (features.md §13.4): hides both panels for focused
    // writing.
    Shortcut {
        sequence: "Ctrl+\\"
        context: Qt.ApplicationShortcut
        onActivated: root.panelsVisible = !root.panelsVisible
    }

    // Settings (the platform convention — the shortcut table in
    // features.md §13 assigns no key).
    Shortcut {
        sequence: "Ctrl+,"
        context: Qt.ApplicationShortcut
        onActivated: settingsDialog.open()
    }

    // Toggle the document outline (features.md §17.1).
    Shortcut {
        sequence: "Ctrl+Shift+O"
        context: Qt.ApplicationShortcut
        onActivated: root.outlineVisible = !root.outlineVisible
    }

    // Toggle the backlinks pane.
    Shortcut {
        sequence: "Ctrl+Shift+B"
        context: Qt.ApplicationShortcut
        enabled: root.collectionOpen
        onActivated: root.backlinksVisible = !root.backlinksVisible
    }

    // Focus mode (§16.1): F11 toggles on Windows/Linux; Cmd+Ctrl+F does so on
    // macOS. Escape exits when active (a single-key
    // exit, as the plan requires). Typewriter mode has no default shortcut.
    Shortcut {
        sequence: Qt.platform.os === "osx" ? "Meta+Ctrl+F" : "F11"
        context: Qt.ApplicationShortcut
        onActivated: root.focusMode = !root.focusMode
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: root.focusMode
        onActivated: root.focusMode = false
    }

    SettingsDialog {
        id: settingsDialog
    }

    // features.md §13 discoverable keyboard-shortcut reference.
    function openShortcutReference() { shortcutReference.open() }
    ShortcutReference {
        id: shortcutReference
    }
    Shortcut {
        sequence: "F1"
        onActivated: shortcutReference.open()
    }

    // Oversized-file guard: a file over the size cap is refused before any
    // read; the placeholder names the file, its size, and the cap, and
    // offers the informed-consent "Open anyway".
    property string oversizedFilePath: ""
    property real oversizedFileBytes: 0
    property real oversizedFileCap: 0
    function formatMiB(bytes) {
        return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
    }

    // features.md §12.1 external-change conflict: when the open note is
    // changed on disk outside the app while it is dirty here, offer keep-mine /
    // load-theirs rather than silently clobbering either side.
    property bool externalConflict: false
    property string conflictPath: ""
    Connections {
        target: FileWatcher
        function onNoteChangedExternally(absPath) {
            if (absPath !== DocumentManager.currentFilePath)
                return   // not the open note — the tree re-scan handles the rest
            DocumentManager.flushPendingEdits()
            if (DocumentManager.isDirty) {
                root.conflictPath = absPath
                root.externalConflict = true
                A11y.announce(qsTr("This note changed on disk"))
            } else {
                // Not dirty here: loading theirs is lossless, so do it silently.
                root.loadTheirs(absPath)
            }
        }
    }
    function keepMine() {
        // Re-write the editor's content, overwriting the external change.
        DocumentManager.save()
        root.externalConflict = false
    }
    function loadTheirs(absPath) {
        var target = absPath !== undefined ? absPath : root.conflictPath
        // Force a reload past openNoteByPath's same-path short-circuit.
        DocumentManager.open(DocumentManager.toLocalFileUrl(target))
        root.lastFocusedBlock = 0
        blockListView.currentIndex = 0
        Qt.callLater(root.refreshSessionBaseline)
        root.externalConflict = false
    }

    // features.md §15 system integration: quick capture + tray + global hotkey.
    function openQuickCapture() {
        if (root.collectionOpen)
            quickCaptureWindow.openCapture()
    }
    QuickCaptureWindow {
        id: quickCaptureWindow
        onCaptured: function(relPath) {
            // Surface the captured note in the running window.
            if (root.collectionOpen)
                root.openNoteByPath(relPath)
        }
    }
    Connections {
        target: GlobalHotkey
        function onActivated() { root.openQuickCapture() }
    }
    // In-app quick-capture chord (works while the window is focused, so capture
    // is reachable even where the system-wide grab is unavailable). It reads
    // the same setting the system-wide registration uses, so changing the chord
    // moves both; hard-coding it here left the setting appearing to do nothing
    // on every platform without a working grab, which is all of them today.
    Shortcut {
        sequence: {
            var r = AppSettings.revision // re-evaluate when a setting changes
            return AppSettings.value("hotkey.quickCapture", "Ctrl+Alt+N")
        }
        onActivated: root.openQuickCapture()
    }
    Connections {
        target: SystemTray
        function onQuickCaptureRequested() { root.openQuickCapture() }
        function onNewNoteRequested() { root.createNoteInCurrentScope() }
        function onShowWindowRequested() {
            root.show(); root.raise(); root.requestActivate()
        }
    }

    // features.md §18 template management dialog.
    property alias templateDialog: templateDialog
    TemplateDialog {
        id: templateDialog
        appWindow: root
    }

    // features.md §12.5 export dialog.
    property alias exportDialog: exportDialog
    ExportDialog {
        id: exportDialog
        appWindow: root
    }

    // features.md §12.6 import dialog.
    property alias importDialog: importDialog
    ImportDialog {
        id: importDialog
        appWindow: root
    }

    // features.md §19.1 statistics popover: opened from the status
    // bar's counts. Parented to the window overlay so it floats above the
    // status bar.
    property alias statisticsPanel: statisticsPanel
    StatisticsPanel {
        id: statisticsPanel
        appWindow: root
        targetBlock: appToolbar.targetBlock
    }

    // §19.2 writing-goal dialog: set or clear the open note's word target.
    Dialog {
        id: goalDialog
        objectName: "goalDialog"
        modal: true
        title: qsTr("Writing goal")
        anchors.centerIn: parent
        width: 300
        standardButtons: Dialog.Ok | Dialog.Cancel
        property string relPath: ""
        function openFor(rel) {
            relPath = rel
            goalField.value = NoteCollection.goalFor(rel)
            open()
        }
        onAccepted: NoteCollection.setGoal(relPath, goalField.value)
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: qsTr("Target word count for this note (0 to clear):")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            SpinBox {
                id: goalField
                objectName: "goalSpinBox"
                from: 0
                to: 1000000
                stepSize: 100
                editable: true
                Layout.fillWidth: true
            }
        }
    }

    // ---- Context menus (features.md §9.5): shared window-level Menu
    // instances triggering the same tested operations the shortcuts
    // drive. A right-click on a selected block routes to
    // the selection menu; on a link, the link menu wins by specificity.
    // A visible text/link menu keeps its target's selection alive
    // through the menu's focus grab (the delegate consults this in its
    // focus-loss deselect).
    function contextMenuHoldsSelection(target) {
        return (textContextMenu.visible && textContextMenu.target === target)
            || (linkContextMenu.visible && linkContextMenu.target === target)
    }

    // KvitShell query overrides: a delegate asks whether its completion menu
    // is open for it, and gets the menu back to drive. The menus are this
    // window's own objects; the delegate never names them.
    function activeBlockMenu(index) {
        return (blockMenu.visible && blockMenu.targetIndex === index)
            ? blockMenu : null
    }
    function activeMathMenu(host) {
        return (mathCommandMenu.visible && mathCommandMenu.targets(host))
            ? mathCommandMenu : null
    }
    function activeWikiMenu(host) {
        return (wikiLinkMenu.visible && wikiLinkMenu.targets(host))
            ? wikiLinkMenu : null
    }
    function openLink(url) {
        linkOpener.activate(url)
        return true
    }

    function openTextContextMenu(target) {
        if (DocumentSelection.hasBlockSelection
            && DocumentSelection.isBlockSelected(target.index)) {
            selectionContextMenu.popup()
            return
        }
        textContextMenu.target = target
        textContextMenu.popup()
    }

    // The image lightbox (§1.2.8): an image block opens it with a resolved
    // source. Declared below over the whole window at a high z.
    function openLightbox(source, alt) {
        lightbox.open(source, alt)
    }

    // Insert an image into an (empty) block by file or URL (features.md §4.3).
    function insertImageIntoBlock(idx) {
        imageInsertDialog.targetIndex = idx
        imagePathField.text = ""
        imageInsertDialog.open()
        imagePathField.forceActiveFocus()
    }

    // §1.2.14 web embed: prompt for a URL and insert an ![](url) image
    // expression, which the content classifier renders as a preview card.
    function insertEmbedIntoBlock(idx) {
        embedInsertDialog.targetIndex = idx
        embedUrlField.text = ""
        embedInsertDialog.open()
        embedUrlField.forceActiveFocus()
    }
    Dialog {
        id: embedInsertDialog
        objectName: "embedInsertDialog"
        title: qsTr("Insert web embed")
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.Ok | Dialog.Cancel
        property int targetIndex: -1
        function commit() {
            var url = embedUrlField.text.trim()
            if (url === "" || targetIndex < 0)
                return
            BlockModel.convertBlock(targetIndex, Block.Image, "![](" + url + ")")
            var idx = targetIndex
            Qt.callLater(function() {
                var item = (blockListView.itemAtIndex(idx) as BlockDelegateBase)
                if (item && item.focusAtStart) item.focusAtStart()
            })
        }
        onAccepted: commit()
        contentItem: TextField {
            id: embedUrlField
            objectName: "embedUrlField"
            placeholderText: qsTr("Web page or video URL (https://…)")
            onAccepted: { embedInsertDialog.commit(); embedInsertDialog.close() }
        }
    }

    // Insert a table via the grid-size picker (features.md §4.2).
    function insertTableIntoBlock(idx) {
        tableSizePicker.targetIndex = idx
        tableSizePicker.open()
    }

    // ---- External drop ingestion (features.md §5.3, §5.4) ----
    function currentNoteDir() {
        var p = DocumentManager.currentFilePath
        var idx = p.lastIndexOf("/")
        return idx >= 0 ? p.substring(0, idx) : ""
    }
    function currentNoteSlug() {
        var p = DocumentManager.currentFilePath
        var fn = p.substring(p.lastIndexOf("/") + 1).replace(/\.[^.]+$/, "")
        var slug = fn.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "")
        return slug === "" ? "image" : slug
    }
    function assetRoot() {
        return NoteCollection.isOpen ? NoteCollection.rootPath : ""
    }
    // The block index a drop at content-y lands after (-1 → append).
    function dropTargetIndex(dropY) {
        var p = editorDropArea.mapToItem(blockListView.contentItem, 0, dropY)
        var idx = blockListView.indexAt(10, p.y)
        return idx  // -1 when below the last block
    }
    function insertBlocksAt(afterIndex, typedBlocks) {
        // typedBlocks: [{type, content}]. Insert after `afterIndex` (append
        // when -1); focus the last inserted block.
        var at = afterIndex < 0 ? BlockModel.count : afterIndex + 1
        var last = at
        for (var i = 0; i < typedBlocks.length; ++i) {
            BlockModel.insertBlock(at, typedBlocks[i].type, typedBlocks[i].content)
            last = at
            at++
        }
        Qt.callLater(function() {
            var item = (blockListView.itemAtIndex(last) as BlockDelegateBase)
            if (item && item.focusAtStart) item.focusAtStart()
        })
    }
    // Turn a stored image/media path into the right block type by extension.
    function blockForPath(stored) {
        var kind = ImageAssets.kindOf(stored)
        var type = kind === "media" ? Block.Media
                 : Block.Image  // default images (and unknown local files show placeholder)
        return { type: type, content: ImageAssets.build(stored, "", "", 0) }
    }
    function handleEditorDrop(drop) {
        var afterIndex = dropTargetIndex(drop.y)
        var slug = currentNoteSlug()
        var root2 = assetRoot()
        var nd = currentNoteDir()
        var blocks = []

        // 1) Raw image bytes (spike b's bytes arm), if delivered.
        var fmts = drop.formats || []
        for (var f = 0; f < fmts.length; ++f) {
            if (fmts[f] === "application/x-qt-image"
                || fmts[f].indexOf("image/") === 0) {
                var buf = drop.getDataAsArrayBuffer(fmts[f])
                if (buf) {
                    var storedB = AssetStore.ingestImageBytes(buf, slug, root2, nd)
                    if (storedB !== "") {
                        blocks.push({ type: Block.Image,
                            content: ImageAssets.build(storedB, "", "", 0) })
                        insertBlocksAt(afterIndex, blocks)
                        return
                    }
                }
            }
        }

        // 2) URLs: local files ingest/link; http(s) image URLs stay remote.
        if (drop.hasUrls) {
            for (var u = 0; u < drop.urls.length; ++u) {
                var url = "" + drop.urls[u]
                if (url.indexOf("file://") === 0) {
                    // Hand the whole file:// URL over and let ingestLocalFile
                    // decode it with QUrl::toLocalFile(). Stripping the scheme
                    // here left %23 and %25 in the path, so a file named
                    // "photo #2.png" resolved to nothing and the drop was
                    // silently ignored.
                    if (ImageAssets.kindOf(url) === "none")
                        continue  // not an image/media file
                    var stored = AssetStore.ingestLocalFile(url, slug, root2, nd)
                    if (stored !== "")
                        blocks.push(blockForPath(stored))
                } else if (url.indexOf("http") === 0) {
                    if (ImageAssets.kindOf(url) === "media")
                        blocks.push({ type: Block.Media, content: ImageAssets.build(url, "", "", 0) })
                    else
                        blocks.push({ type: Block.Image, content: ImageAssets.build(url, "", "", 0) })
                }
            }
            if (blocks.length > 0) {
                insertBlocksAt(afterIndex, blocks)
                return
            }
        }

        // 3) Plain text: a bare image URL becomes a remote image; otherwise
        //    the text splits into paragraph blocks at the drop point (§5.4).
        if (drop.hasText) {
            var txt = ("" + drop.text).trim()
            if (txt.indexOf("http") === 0 && ImageAssets.kindOf(txt) !== "none") {
                blocks.push(blockForPath(txt))
            } else {
                var lines = txt.split("\n")
                for (var l = 0; l < lines.length; ++l)
                    blocks.push({ type: Block.Paragraph, content: lines[l] })
            }
            if (blocks.length > 0)
                insertBlocksAt(afterIndex, blocks)
        }
    }
    function openLinkContextMenu(target) {
        linkContextMenu.target = target
        linkContextMenu.popup()
    }
    function openBlockHandleMenu(target) {
        if (DocumentSelection.hasBlockSelection
            && DocumentSelection.isBlockSelected(target.index)) {
            selectionContextMenu.popup()
            return
        }
        blockContextMenu.target = target
        blockContextMenu.popup()
    }

    Menu {
        id: textContextMenu
        objectName: "textContextMenu"
        property var target: null
        readonly property bool hasSel: target
            && target.selectionEndDoc > target.selectionStartDoc

        MenuItem {
            objectName: "ctxCut"
            text: qsTr("Cut")
            enabled: textContextMenu.hasSel
            onTriggered: textContextMenu.target.cutSelection()
        }
        MenuItem {
            objectName: "ctxCopy"
            text: qsTr("Copy")
            enabled: textContextMenu.hasSel
            onTriggered: textContextMenu.target.copySelection()
        }
        MenuItem {
            objectName: "ctxPaste"
            text: qsTr("Paste")
            enabled: Clipboard.hasText
            onTriggered: textContextMenu.target.pasteClipboard(false)
        }
        MenuItem {
            objectName: "ctxPastePlain"
            text: qsTr("Paste as plain text")
            enabled: Clipboard.hasText
            onTriggered: textContextMenu.target.pasteClipboard(true)
        }
        MenuSeparator {}
        Menu {
            title: qsTr("Formatting")
            enabled: textContextMenu.target
                     && !textContextMenu.target.verbatimEditing
            Repeater {
                model: [
                    { name: qsTr("Bold"), type: "bold" },
                    { name: qsTr("Italic"), type: "italic" },
                    { name: qsTr("Underline"), type: "underline" },
                    { name: qsTr("Strikethrough"), type: "strike" },
                    { name: qsTr("Inline code"), type: "code" },
                    { name: qsTr("Highlight"), type: "highlight" },
                    { name: qsTr("Superscript"), type: "superscript" },
                    { name: qsTr("Subscript"), type: "subscript" },
                    { name: qsTr("Inline math"), type: "math" }]
                MenuItem {
                    id: spanTypeItem
                    required property var modelData
                    text: spanTypeItem.modelData.name
                    onTriggered: textContextMenu.target.toggleSpanType(
                        spanTypeItem.modelData.type)
                }
            }
        }
        Menu {
            title: qsTr("Text color")
            enabled: textContextMenu.target
                     && !textContextMenu.target.verbatimEditing
            Repeater {
                model: [
                    { name: qsTr("Red"), value: "#e05c5c" },
                    { name: qsTr("Orange"), value: "#e0a04c" },
                    { name: qsTr("Green"), value: "#58a866" },
                    { name: qsTr("Blue"), value: "#4a90d9" },
                    { name: qsTr("Purple"), value: "#9068c8" },
                    { name: qsTr("Pink"), value: "#d06ca8" }]
                MenuItem {
                    id: colorItem
                    required property var modelData
                    text: colorItem.modelData.name
                    // A leading swatch of the color the item applies.
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        width: 14; height: 14; radius: 3
                        color: colorItem.modelData.value
                        border.color: Theme.border
                    }
                    onTriggered: textContextMenu.target.applyColor(colorItem.modelData.value)
                }
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Custom…")
                onTriggered: {
                    textColorDialog.target = textContextMenu.target
                    textColorDialog.open()
                }
            }
            MenuItem {
                text: qsTr("Remove color")
                enabled: textContextMenu.target
                         && textContextMenu.target.currentColor !== ""
                onTriggered: textContextMenu.target.removeColor()
            }
        }
        MenuItem {
            text: qsTr("Link…")
            enabled: textContextMenu.target
                     && !textContextMenu.target.verbatimEditing
            onTriggered: textContextMenu.target.openLinkDialog()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Select all")
            onTriggered: textContextMenu.target.selectAllText()
        }
    }

    // The image lightbox overlay (§1.2.8), over the whole window.
    Lightbox {
        id: lightbox
        objectName: "lightbox"
    }

    // Table grid-size picker (§4.2). On a size choice it converts the target
    // block to a Table with an empty grid of that size.
    TableSizePicker {
        id: tableSizePicker
        objectName: "tableSizePicker"
        anchors.centerIn: parent
        property int targetIndex: -1
        onSizePicked: function(cols, rows) {
            if (targetIndex < 0) return
            BlockModel.convertBlock(targetIndex, Block.Table,
                                    TableTools.emptyTable(cols, rows))
            var idx = targetIndex
            Qt.callLater(function() {
                var item = (blockListView.itemAtIndex(idx) as BlockDelegateBase)
                if (item && item.focusAtStart) item.focusAtStart()
            })
        }
    }

    // Insert-image dialog (§4.3): a path/URL field with a file browser. On
    // accept it converts the target block into an Image block whose content
    // is the built markdown expression (one undo step).
    Dialog {
        id: imageInsertDialog
        objectName: "imageInsertDialog"
        title: qsTr("Insert image")
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.Ok | Dialog.Cancel
        property int targetIndex: -1

        function commit() {
            var path = imagePathField.text.trim()
            if (path === "" || targetIndex < 0)
                return
            var md = ImageAssets.build(path, "", "", 0)
            // An audio/video path lands a Media block; everything else an
            // Image. The dialog is shared.
            var type = ImageAssets.parse(md).kind === "media"
                     ? Block.Media : Block.Image
            BlockModel.convertBlock(targetIndex, type, md)
            var idx = targetIndex
            Qt.callLater(function() {
                var item = (blockListView.itemAtIndex(idx) as BlockDelegateBase)
                if (item && item.focusAtStart) item.focusAtStart()
            })
        }
        onAccepted: commit()

        contentItem: Row {
            spacing: 6
            TextField {
                id: imagePathField
                objectName: "imagePathField"
                width: 320
                placeholderText: qsTr("Image file path or URL")
                onAccepted: { imageInsertDialog.commit(); imageInsertDialog.close() }
            }
            Button {
                text: qsTr("Browse…")
                onClicked: imageFileDialog.open()
            }
        }
    }

    FileDialog {
        id: imageFileDialog
        objectName: "imageFileDialog"
        title: qsTr("Choose an image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.webp *.svg *.bmp)"),
                      qsTr("All files (*)")]
        onAccepted: {
            // Store the chosen file's path; ingestion/copy comes later.
            var p = selectedFile.toString().replace(/^file:\/\//, "")
            imagePathField.text = p
        }
    }

    // The custom-color picker for the text context menu.
    // The target block is captured when the menu opens, since the dialog is
    // asynchronous.
    ColorDialog {
        id: textColorDialog
        property var target: null
        onAccepted: {
            if (!target) return
            var s = selectedColor.toString()
            if (s.length === 9)
                s = "#" + s.substr(3)
            target.applyColor(s)
        }
    }

    Menu {
        id: linkContextMenu
        objectName: "linkContextMenu"
        property var target: null

        MenuItem {
            objectName: "ctxOpenLink"
            text: qsTr("Open link")
            onTriggered: linkContextMenu.target.openLinkUnderCursor()
        }
        MenuItem {
            objectName: "ctxEditLink"
            text: qsTr("Edit link…")
            onTriggered: linkContextMenu.target.openLinkDialog()
        }
        MenuItem {
            objectName: "ctxRemoveLink"
            text: qsTr("Remove link")
            onTriggered: linkContextMenu.target.removeLinkAtCursor()
        }
    }

    Menu {
        id: blockContextMenu
        objectName: "blockContextMenu"
        property var target: null

        Menu {
            title: qsTr("Turn into")
            Repeater {
                model: appToolbar.typeNames
                MenuItem {
                    required property int index
                    required property string modelData
                    text: modelData
                    onTriggered: blockContextMenu.target.convertBlockType(
                        appToolbar.typeValues[index])
                }
            }
        }
        // Alignment (§9.2): paragraphs, headings, and images.
        Menu {
            objectName: "ctxAlignMenu"
            title: qsTr("Align")
            enabled: blockContextMenu.target
                && blockContextMenu.target.setBlockAlignment !== undefined
                && [0, 1, 2, 3, 10, 11].indexOf(blockContextMenu.target.blockType) >= 0
            MenuItem {
                text: qsTr("Left")
                onTriggered: blockContextMenu.target.setBlockAlignment("left")
            }
            MenuItem {
                text: qsTr("Center")
                onTriggered: blockContextMenu.target.setBlockAlignment("center")
            }
            MenuItem {
                text: qsTr("Right")
                onTriggered: blockContextMenu.target.setBlockAlignment("right")
            }
        }
        // Drop cap (§1.2.16): a paragraph-only enlarged initial.
        Menu {
            objectName: "ctxDropCapMenu"
            title: qsTr("Drop cap")
            enabled: blockContextMenu.target
                && blockContextMenu.target.setDropCap !== undefined
                && blockContextMenu.target.blockType === 0   // Paragraph
            MenuItem {
                text: qsTr("None")
                onTriggered: blockContextMenu.target.setDropCap(0)
            }
            MenuItem {
                text: qsTr("2 lines")
                onTriggered: blockContextMenu.target.setDropCap(2)
            }
            MenuItem {
                text: qsTr("3 lines")
                onTriggered: blockContextMenu.target.setDropCap(3)
            }
            MenuItem {
                text: qsTr("5 lines")
                onTriggered: blockContextMenu.target.setDropCap(5)
            }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "ctxBlockDuplicate"
            text: qsTr("Duplicate")
            onTriggered: BlockModel.duplicateBlocks(
                [blockContextMenu.target.index])
        }
        MenuItem {
            objectName: "ctxBlockDelete"
            text: qsTr("Delete")
            onTriggered: BlockModel.removeBlocks(
                [blockContextMenu.target.index])
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Move up")
            enabled: blockContextMenu.target
                     && blockContextMenu.target.index > 0
            onTriggered: BlockModel.moveBlocksBy(
                [blockContextMenu.target.index], -1)
        }
        MenuItem {
            text: qsTr("Move down")
            enabled: blockContextMenu.target
                     && blockContextMenu.target.index < BlockModel.count - 1
            onTriggered: BlockModel.moveBlocksBy(
                [blockContextMenu.target.index], 1)
        }
        MenuItem {
            text: qsTr("Indent")
            onTriggered: BlockModel.changeIndentForBlocks(
                [blockContextMenu.target.index], 1)
        }
        MenuItem {
            text: qsTr("Outdent")
            onTriggered: BlockModel.changeIndentForBlocks(
                [blockContextMenu.target.index], -1)
        }
    }

    Menu {
        id: selectionContextMenu
        objectName: "selectionContextMenu"

        MenuItem {
            objectName: "ctxSelCopy"
            text: qsTr("Copy")
            onTriggered: selectionKeyHandler.copyBlocksToClipboard()
        }
        MenuItem {
            text: qsTr("Cut")
            onTriggered: {
                selectionKeyHandler.copyBlocksToClipboard()
                selectionKeyHandler.removeSelectedBlocks()
            }
        }
        MenuItem {
            objectName: "ctxSelDuplicate"
            text: qsTr("Duplicate")
            onTriggered: {
                var clones = BlockModel.duplicateBlocks(
                    DocumentSelection.selectedIndexes())
                if (clones.length > 0)
                    selectionKeyHandler.selectRange(
                        Number(clones[0]),
                        Number(clones[clones.length - 1]))
            }
        }
        MenuItem {
            objectName: "ctxSelDelete"
            text: qsTr("Delete")
            onTriggered: selectionKeyHandler.removeSelectedBlocks()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Move up")
            onTriggered: {
                BlockModel.moveBlocksBy(
                    DocumentSelection.selectedIndexes(), -1)
                selectionKeyHandler.revealSelectionEdge()
            }
        }
        MenuItem {
            text: qsTr("Move down")
            onTriggered: {
                BlockModel.moveBlocksBy(
                    DocumentSelection.selectedIndexes(), 1)
                selectionKeyHandler.revealSelectionEdge()
            }
        }
        MenuItem {
            text: qsTr("Indent")
            onTriggered: BlockModel.changeIndentForBlocks(
                DocumentSelection.selectedIndexes(), 1)
        }
        MenuItem {
            text: qsTr("Outdent")
            onTriggered: BlockModel.changeIndentForBlocks(
                DocumentSelection.selectedIndexes(), -1)
        }
    }

    // Global search (§8.4; the Obsidian/VSCode convention — the spec
    // assigns no key).
    Shortcut {
        sequence: "Ctrl+Shift+F"
        context: Qt.ApplicationShortcut
        enabled: root.collectionOpen
        onActivated: {
            root.panelsVisible = true
            sidebar.focusSearch()
        }
    }

    // Global-search filters follow the sidebar's active scope, so
    // folder-level search composes.
    Binding {
        target: CollectionSearch
        property: "folderScope"
        value: NoteListModel.scope === "folder" ? NoteListModel.folderPath : ""
    }
    Binding {
        target: CollectionSearch
        property: "tagFilter"
        value: NoteListModel.tagFilter
    }

    // The crash-recovery journal follows the open note;
    // "" outside collection mode disables it.
    Binding {
        target: DocumentManager
        property: "journalPath"
        value: root.currentNoteRelPath !== ""
               ? NoteCollection.journalPathFor(root.currentNoteRelPath) : ""
    }

    // Restore a crash-recovered note (the banner's Restore button): the
    // journal content lands on disk; a currently-open note reloads.
    function restoreRecoveredNote(relPath) {
        if (!NoteCollection.restoreRecovery(relPath))
            return
        if (root.currentNoteRelPath === relPath) {
            DocumentManager.open(DocumentManager.toLocalFileUrl(
                NoteCollection.absolutePath(relPath)))
        } else {
            openNoteByPath(relPath)
        }
    }

    // ---- Restore from backup: per-note, previewed, and
    // applied through the block model as ONE undo step — a wrong restore
    // costs one Ctrl+Z, which is why no extra confirmation is needed.
    property alias backupDialog: backupDialog
    Dialog {
        id: backupDialog
        objectName: "backupDialog"
        modal: true
        anchors.centerIn: parent
        width: 420
        title: qsTr("Restore from Backup")

        property var backups: []
        property int selectedRow: 0

        function openForCurrentNote() {
            if (root.currentNoteRelPath === "")
                return
            backups = NoteCollection.backupsFor(root.currentNoteRelPath)
            selectedRow = 0
            open()
        }

        onAccepted: {
            if (selectedRow < 0 || selectedRow >= backups.length)
                return
            var body = NoteCollection.backupBody(
                root.currentNoteRelPath, backups[selectedRow].fileName)
            if (DocumentManager.restoreBody(body))
                DocumentManager.save()
        }

        contentItem: ColumnLayout {
            spacing: 4
            Label {
                visible: backupDialog.backups.length === 0
                text: qsTr("No backups yet — they appear as the note is edited over time.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                padding: 8
            }
            ListView {
                objectName: "backupDialogList"
                visible: backupDialog.backups.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(count * 44, 220)
                clip: true
                model: backupDialog.backups
                delegate: Rectangle {
                    id: backupRow
                    required property int index
                    required property var modelData
                    width: parent ? parent.width : 0
                    height: 44
                    color: backupRow.index === backupDialog.selectedRow
                           ? Theme.selectionTint : "transparent"
                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        Label {
                            text: Qt.formatDateTime(backupRow.modelData.timestamp,
                                                    "MMM d, yyyy hh:mm:ss")
                            font.pixelSize: 12
                            font.bold: true
                        }
                        Label {
                            text: backupRow.modelData.preview !== ""
                                  ? backupRow.modelData.preview
                                  : qsTr("(empty)")
                            font.pixelSize: 11
                            color: Theme.textFaint
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: backupDialog.selectedRow = backupRow.index
                    }
                }
            }
        }

        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                objectName: "backupDialogRestoreButton"
                text: qsTr("Restore")
                enabled: backupDialog.backups.length > 0
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }

    // Quitting with a document that has never been saved. Closing used to
    // discard it silently: there is no file to fall back on and the recovery
    // journal only covers crashes, so this was an unrecoverable loss on an
    // ordinary quit.
    Dialog {
        id: closeConfirmDialog
        title: "Unsaved Changes"
        modal: true
        anchors.centerIn: parent

        contentItem: Item {
            implicitWidth: 360
            implicitHeight: closeDialogText.implicitHeight + 40

            Text {
                id: closeDialogText
                anchors.fill: parent
                anchors.margins: 20
                text: "This document has never been saved. Save it before closing?"
                wrapMode: Text.WordWrap
            }
        }

        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel

        onAccepted: {
            // Only close once the document is actually on disk. If the Save-As
            // dialog is cancelled or the write fails, stay open.
            if (DocumentManager.saveFileDialog())
                root.close()
        }
        // Discard is the user deciding to lose it, which is their call to make.
        onDiscarded: {
            DocumentManager.newDocument()
            root.close()
        }
        // Cancel: nothing happens, the window stays open.
    }

    // Unsaved changes dialog before opening a new file
    Dialog {
        id: unsavedChangesBeforeOpenDialog
        title: "Unsaved Changes"
        modal: true
        anchors.centerIn: parent

        contentItem: Item {
            implicitWidth: 360
            implicitHeight: openDialogText.implicitHeight + 40

            Text {
                id: openDialogText
                anchors.fill: parent
                anchors.margins: 20
                text: "You have unsaved changes. Do you want to save them before opening another file?"
                wrapMode: Text.WordWrap
            }
        }

        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel

        onAccepted: {
            // Save button clicked. Opening another document replaces the model,
            // so it may only proceed once this one is genuinely on disk. A
            // cancelled Save-As and a failed write both mean it is not, and in
            // both cases the right thing is to leave the document alone rather
            // than continue into an action that discards it.
            var saved = DocumentManager.hasFile
                        ? DocumentManager.save()
                        : DocumentManager.saveFileDialog()
            if (!saved)
                return
            DocumentManager.openFileDialog()
        }

        onDiscarded: {
            // Discard button clicked - open without saving
            DocumentManager.openFileDialog()
        }

        // Cancel - do nothing
    }

    // Unsaved changes dialog before creating new document
    Dialog {
        id: unsavedChangesBeforeNewDialog
        title: "Unsaved Changes"
        modal: true
        anchors.centerIn: parent

        contentItem: Item {
            implicitWidth: 360
            implicitHeight: newDialogText.implicitHeight + 40

            Text {
                id: newDialogText
                anchors.fill: parent
                anchors.margins: 20
                text: "You have unsaved changes. Do you want to save them before creating a new document?"
                wrapMode: Text.WordWrap
            }
        }

        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel

        onAccepted: {
            // Save button clicked. Same rule as the Open confirmation: starting
            // a new document throws this one away, so it has to be safely
            // stored first.
            var saved = DocumentManager.hasFile
                        ? DocumentManager.save()
                        : DocumentManager.saveFileDialog()
            if (!saved)
                return
            DocumentManager.newDocument()
        }

        onDiscarded: {
            // Discard button clicked - create new without saving
            DocumentManager.newDocument()
        }

        // Cancel - do nothing
    }

    // Oversized-paste confirm: pasting a payload over the open-size cap
    // is allowed, but only deliberately.
    Dialog {
        id: largePasteConfirmDialog
        objectName: "largePasteConfirmDialog"
        title: qsTr("Paste very large text?")
        modal: true
        anchors.centerIn: parent
        property string pendingText: ""
        property int pendingIndex: 0
        // Carries the Ctrl+Shift+V intent across the confirm step, so a
        // confirmed oversized paste-as-plain stays plain (§5.3).
        property bool pendingPlain: false

        contentItem: Item {
            implicitWidth: 380
            implicitHeight: largePasteText.implicitHeight + 40
            Text {
                id: largePasteText
                anchors.fill: parent
                anchors.margins: 20
                wrapMode: Text.WordWrap
                text: qsTr("The Clipboard holds %1 of text — over the %2 limit. Pasting it may take a while.")
                    .arg(root.formatMiB(largePasteConfirmDialog.pendingText.length))
                    .arg(root.formatMiB(DocumentManager.maxOpenFileSizeMiB
                                        * 1024 * 1024))
            }
        }

        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: {
            var count = pendingPlain
                ? DocumentSerializer.insertPlainTextAt(
                    BlockModel, pendingIndex, pendingText)
                : DocumentSerializer.insertMarkdownAt(
                    BlockModel, pendingIndex, pendingText)
            if (count > 0)
                selectionKeyHandler.selectRange(pendingIndex, pendingIndex + count - 1)
            pendingText = ""
            pendingPlain = false
        }
        onRejected: { pendingText = ""; pendingPlain = false }
    }

    // Error dialog
    Dialog {
        id: renameLinksDialog
        objectName: "renameLinksDialog"
        title: qsTr("Update wiki-links?")
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: 440

        contentItem: Label {
            width: 390
            padding: 18
            wrapMode: Text.WordWrap
            text: root.pendingRenamePlan
                ? qsTr("Update %1 links in %2 notes?")
                    .arg(root.pendingRenamePlan.linkCount)
                    .arg(root.pendingRenamePlan.noteCount)
                : ""
        }
        footer: DialogButtonBox {
            Button {
                objectName: "renameUpdateLinksButton"
                text: qsTr("Update links")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    renameLinksDialog.close()
                    root.finishRenamePlan(true)
                }
            }
            Button {
                objectName: "renameOnlyButton"
                text: qsTr("Rename only")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: {
                    renameLinksDialog.close()
                    root.finishRenamePlan(false)
                }
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: {
                    if (root.pendingRenamePlan)
                        NoteCollection.cancelRenamePlan(root.pendingRenamePlan.id)
                    root.pendingRenamePlan = null
                    root.pendingRenameAfter = null
                    renameLinksDialog.close()
                }
            }
        }
    }

    Dialog {
        id: rewriteResultDialog
        objectName: "rewriteResultDialog"
        title: qsTr("Some links were not updated")
        modal: true
        anchors.centerIn: parent
        width: 460
        property string planId: ""
        property var skipped: []
        property var failed: []

        contentItem: Label {
            width: 420
            padding: 18
            wrapMode: Text.WordWrap
            text: {
                var lines = []
                if (rewriteResultDialog.skipped.length > 0)
                    lines.push(qsTr("Changed externally (skipped):\n%1")
                               .arg(rewriteResultDialog.skipped.join("\n")))
                if (rewriteResultDialog.failed.length > 0)
                    lines.push(qsTr("Could not write:\n%1")
                               .arg(rewriteResultDialog.failed.join("\n")))
                return lines.join("\n\n")
            }
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Retry")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    var result = root.executeRenamePlan(
                        rewriteResultDialog.planId, true)
                    if (result && result.ok
                            && result.skipped.length === 0
                            && result.failed.length === 0)
                        rewriteResultDialog.close()
                    else if (result) {
                        rewriteResultDialog.skipped = result.skipped || []
                        rewriteResultDialog.failed = result.failed || []
                    }
                }
            }
            Button {
                text: qsTr("Close")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: rewriteResultDialog.close()
            }
        }
    }

    Dialog {
        id: errorDialog
        objectName: "errorDialog"
        title: "Error"
        modal: true
        anchors.centerIn: parent
        property string errorMessage: ""

        contentItem: Item {
            implicitWidth: 360
            implicitHeight: errorDialogText.implicitHeight + 40

            Text {
                id: errorDialogText
                anchors.fill: parent
                anchors.margins: 20
                text: errorDialog.errorMessage
                wrapMode: Text.WordWrap
            }
        }

        standardButtons: Dialog.Ok
    }

    // Handle user-facing save/open results. Repository indexing, backup
    // rotation, watcher guards, and metadata synchronization are wired by the
    // C++ application composition rather than ordered here.
    Connections {
        target: DocumentManager

        function onSaveFailed(error) {
            errorDialog.errorMessage = "Failed to save: " + error
            errorDialog.open()
        }

        function onOpenFailed(error) {
            errorDialog.errorMessage = "Failed to open: " + error
            errorDialog.open()
        }

        // Oversized-file guard: the file was refused before any read;
        // show the placeholder with an "Open anyway".
        function onOpenRejectedTooLarge(filePath, sizeBytes, capBytes) {
            root.oversizedFilePath = filePath
            root.oversizedFileBytes = sizeBytes
            root.oversizedFileCap = capBytes
        }
        function onOpenSucceeded(filePath) {
            root.oversizedFilePath = ""
        }
    }

    // Auto-save when window loses focus
    onActiveChanged: {
        if (!active && DocumentManager)
            DocumentManager.flushPendingEdits()
        if (!active && DocumentManager && DocumentManager.isDirty && DocumentManager.hasFile) {
            DocumentManager.saveAsync()
        }
    }

    // Orderly shutdown saves (features.md §12.2). Crash recovery relies
    // on this — the recovery journal only survives real crashes.
    onClosing: function(close) {
        if (!DocumentManager)
            return
        DocumentManager.flushPendingEdits()
        if (!DocumentManager.isDirty)
            return

        if (DocumentManager.hasFile) {
            // A save that fails on the way out is the worst case for data loss:
            // there is no next attempt, and the recovery journal is not meant
            // to cover an orderly quit. Keep the window open so the error is
            // visible and the user can act on it.
            if (!DocumentManager.save())
                close.accepted = false
            return
        }

        // A dirty document that has never been saved had no handling at all:
        // closing simply discarded it. Ask, and treat cancel as "do not close".
        close.accepted = false
        closeConfirmDialog.open()
    }

    // Settings that cannot reach disk (read-only location, full disk).
    // The values are kept and retried, so this warns rather than
    // interrupting: a dialog per debounced write would be unusable.
    Connections {
        target: AppSettings

        function onWriteFailed(filePath, error) {
            root.showTransientStatus(
                qsTr("Could not save settings: %1").arg(error))
        }
    }

    // Collection UI notifications. Open-note rebind/detach and metadata
    // persistence happen inside the C++ session/repository transaction.
    Connections {
        target: NoteCollection
        enabled: root.collectionOpen

        function onOpenNoteRemoved(relPath) {
            Qt.callLater(function() {
                var next = NoteListModel.relPathAt(0)
                if (next !== "")
                    root.openNoteByPath(next)
            })
        }
        function onOperationFailed(message) {
            errorDialog.errorMessage = message
            errorDialog.open()
        }
        // The vault is open in another Kvit process. Only one session may
        // write a vault: both would load the same state and the second to
        // save would discard the first's work. This window keeps running as
        // a plain editor, so File > Open still works on individual notes.
        function onVaultInUse(path, detail) {
            errorDialog.errorMessage =
                qsTr("%1\n\nOnly one Kvit window can have a vault open, "
                     + "because two would overwrite each other's changes. "
                     + "Close the other window and reopen this folder, or "
                     + "keep working here on single files.\n\n%2")
                    .arg(detail).arg(path)
            errorDialog.open()
        }
        function onWikiLinksRewritten(linkCount, noteCount) {
            // Rename-safe wiki-links (§3.3): a passive toast, never a dialog.
            root.showTransientStatus(
                qsTr("Updated %1 %2 in %3 %4")
                    .arg(linkCount)
                    .arg(linkCount === 1 ? qsTr("link") : qsTr("links"))
                    .arg(noteCount)
                    .arg(noteCount === 1 ? qsTr("note") : qsTr("notes")))
        }
    }

    // ---- The three-pane shell: sidebar and note list on the left, the
    // editor filling the rest.
    // The features.md §9.2 toolbar spans the window above all three panes.
    Toolbar {
        id: appToolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        appWindow: root
        listView: blockListView
        // Focus mode (§16.1) hides the toolbar with the rest of the chrome.
        visible: !root.focusMode
    }

    // features.md §12.1 external-change conflict banner: the open note was
    // changed on disk while dirty here. Keep-mine re-saves; load-theirs reloads.
    Rectangle {
        id: conflictBanner
        objectName: "conflictBanner"
        anchors.top: appToolbar.visible ? appToolbar.bottom : parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.externalConflict ? 40 : 0
        visible: root.externalConflict
        z: 50
        color: Theme.bannerBackground
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: 1; color: Theme.border
        }
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("This note changed on disk. Keep your version or load the disk version?")
                color: Theme.bannerText
                font.pixelSize: 13
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Button {
                objectName: "conflictKeepMine"
                text: qsTr("Keep mine")
                onClicked: root.keepMine()
            }
            Button {
                objectName: "conflictLoadTheirs"
                text: qsTr("Load theirs")
                onClicked: root.loadTheirs()
            }
        }
    }

    // Oversized-file placeholder: the file was refused before any read.
    // Honest, cheap, and safe — no degraded text-only mode whose saves
    // could rewrite a file the editor never truly parsed. "Open anyway"
    // is the normal path, unmodified.
    Rectangle {
        id: oversizedFileBanner
        objectName: "oversizedFileBanner"
        anchors.top: conflictBanner.visible ? conflictBanner.bottom
                     : appToolbar.visible ? appToolbar.bottom : parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.oversizedFilePath !== "" ? 44 : 0
        visible: root.oversizedFilePath !== ""
        z: 50
        color: Theme.bannerBackground
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: 1; color: Theme.border
        }
        Label {
            objectName: "oversizedFileLabel"
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.right: oversizedActions.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideMiddle
            text: {
                var name = root.oversizedFilePath.split("/").pop()
                return qsTr("“%1” is %2 — over the %3 open limit, so it was not loaded.")
                    .arg(name)
                    .arg(root.formatMiB(root.oversizedFileBytes))
                    .arg(root.formatMiB(root.oversizedFileCap))
            }
            color: Theme.bannerText
            font.pixelSize: 13
        }
        Row {
            id: oversizedActions
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Button {
                objectName: "oversizedOpenAnyway"
                text: qsTr("Open anyway")
                onClicked: {
                    var path = root.oversizedFilePath
                    root.oversizedFilePath = ""
                    DocumentManager.openAsync(
                        DocumentManager.toLocalFileUrl(path), true)
                }
            }
            Button {
                objectName: "oversizedDismiss"
                text: qsTr("Dismiss")
                onClicked: root.oversizedFilePath = ""
            }
        }
    }

    // ── Extension slots ───────────────────────────────────────────────────
    // Three empty Loaders a linked module fills through ExtensionRegistry:
    // a banner strip below the built-in banners, a bar between the editor and
    // the status bar, and a panel beside the outline and backlinks panes.
    // With no module installed every source is empty, so each Loader stays
    // inactive and zero-sized and the shell lays out exactly as before.
    Loader {
        id: extensionBanner
        objectName: "extensionBanner"
        source: Extensions.slotSource("banner")
        active: source != ""
        anchors.top: oversizedFileBanner.visible ? oversizedFileBanner.bottom
                     : conflictBanner.visible ? conflictBanner.bottom
                     : appToolbar.visible ? appToolbar.bottom : parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: active && item ? (item as Item).implicitHeight : 0
        z: 50
    }

    Row {
        id: sidePanels
        objectName: "sidePanels"
        anchors.top: appToolbar.bottom
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.bottomChromeHeight
        visible: root.collectionOpen && root.panelsVisible && !root.focusMode

        // Explicit sum (not implicitWidth): the editor pane's
        // leftMargin binds here, and a hidden Row must reserve nothing.
        readonly property int seamWidth: 6
        readonly property int stripWidth: 22
        width: visible
            ? (root.sidebarCollapsed
                   ? stripWidth : root.sidebarWidth + seamWidth)
              + (root.noteListCollapsed
                     ? stripWidth : root.noteListWidth + seamWidth)
            : 0

        // Collapsed sidebar: a slim strip holding the expand chevron.
        Rectangle {
            objectName: "sidebarStrip"
            visible: root.sidebarCollapsed
            width: visible ? sidePanels.stripWidth : 0
            height: parent.height
            color: Theme.panelBackground
            Rectangle {
                anchors.right: parent.right
                height: parent.height
                width: 1
                color: Theme.border
            }
            ToolButton {
                objectName: "sidebarExpandButton"
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                text: "»"
                font.pixelSize: 12
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Expand sidebar")
                onClicked: root.sidebarCollapsed = false
            }
        }
        Sidebar {
            id: sidebar
            visible: !root.sidebarCollapsed
            width: visible ? root.sidebarWidth : 0
            height: parent.height
            appWindow: root
            // Reinstated panel-collapse animation (§14.3), gated by the reduced-
            // motion source and suppressed during a seam drag so the two never
            // fight over width.
            Behavior on width {
                enabled: Theme.motionScale > 0 && !sidebarSeam.dragging
                NumberAnimation { duration: 160 * Theme.motionScale
                                  easing.type: Easing.OutCubic }
            }
        }
        PanelSeam {
            id: sidebarSeam
            objectName: "sidebarSeam"
            visible: !root.sidebarCollapsed
            width: visible ? sidePanels.seamWidth : 0
            height: parent.height
            minWidth: 140
            maxWidth: 400
            panelWidth: root.sidebarWidth
            onResized: function(newWidth) { root.sidebarWidth = newWidth }
        }

        Rectangle {
            objectName: "noteListStrip"
            visible: root.noteListCollapsed
            width: visible ? sidePanels.stripWidth : 0
            height: parent.height
            color: Theme.listBackground
            Rectangle {
                anchors.right: parent.right
                height: parent.height
                width: 1
                color: Theme.border
            }
            ToolButton {
                objectName: "noteListExpandButton"
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                text: "»"
                font.pixelSize: 12
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Expand note list")
                onClicked: root.noteListCollapsed = false
            }
        }
        NoteListPane {
            id: noteListPane
            visible: !root.noteListCollapsed
            width: visible ? root.noteListWidth : 0
            height: parent.height
            appWindow: root
            sidebar: sidebar
            Behavior on width {
                enabled: Theme.motionScale > 0 && !noteListSeam.dragging
                NumberAnimation { duration: 160 * Theme.motionScale
                                  easing.type: Easing.OutCubic }
            }
        }
        PanelSeam {
            id: noteListSeam
            objectName: "noteListSeam"
            visible: !root.noteListCollapsed
            width: visible ? sidePanels.seamWidth : 0
            height: parent.height
            minWidth: 180
            maxWidth: 520
            panelWidth: root.noteListWidth
            onResized: function(newWidth) { root.noteListWidth = newWidth }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: sidePanels.width
        anchors.topMargin: appToolbar.visible ? appToolbar.height : 0
        anchors.bottomMargin: root.bottomChromeHeight
        anchors.rightMargin: (outlinePanel.visible ? root.outlineWidth : 0)
            + (backlinksPanel.visible ? root.backlinksWidth : 0)
            + extensionSidePanel.width
        color: root.backgroundColor

        // A press that no block claimed (margins, the gap between
        // blocks, below the last block) ends any document-level
        // selection (§3.1 clicking-elsewhere behavior).
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: function(mouse) {
                if (DocumentSelection.hasBlockSelection
                    || DocumentSelection.hasTextSelection)
                    DocumentSelection.clear()
                mouse.accepted = false
            }
        }

        // Shared edge auto-scroller: while a selection or block drag
        // holds the pointer inside the viewport's top/bottom band,
        // scroll with speed scaling with edge proximity (features.md
        // §21.3 "smooth accelerated scrolling").
        QtObject {
            id: edgeScroller
            property bool active: false
            property real pointerY: 0 // in blockListView viewport coordinates
            readonly property int band: 48
        }

        Timer {
            interval: 16
            repeat: true
            running: edgeScroller.active
            onTriggered: {
                var lv = blockListView
                if (lv.contentHeight <= lv.height)
                    return
                var speed = 0
                if (edgeScroller.pointerY < edgeScroller.band)
                    speed = -(edgeScroller.band - edgeScroller.pointerY) / 4
                else if (edgeScroller.pointerY > lv.height - edgeScroller.band)
                    speed = (edgeScroller.pointerY - (lv.height - edgeScroller.band)) / 4
                if (speed === 0)
                    return
                lv.contentY = Math.max(0, Math.min(lv.contentY + speed,
                                                   lv.contentHeight - lv.height))
            }
        }

        // The open note's tags. Stacked above the ScrollView (like the
        // find bar): the ScrollView's anchor
        // re-layout when the strip appears is a polish-frame behind, and
        // chip clicks must never fall into the document during that frame.
        TagStrip {
            id: tagStrip
            z: 5
            appWindow: root
            visible: root.collectionOpen && root.currentNoteRelPath !== ""
            height: visible ? 30 : 0
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 20
            anchors.rightMargin: 52 // room for the backup button
            anchors.topMargin: visible ? 8 : 0
        }

        ToolButton {
            objectName: "restoreBackupButton"
            z: 5
            visible: tagStrip.visible
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 10
            anchors.rightMargin: 16
            text: "↺"
            font.pixelSize: 13
            implicitWidth: 26
            implicitHeight: 26
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Restore from backup…")
            onClicked: backupDialog.openForCurrentNote()
        }

        // External-drag ingestion (§5.4; spike b's contract): OS file drops,
        // image bytes, image URLs, and plain text land as blocks at the drop
        // point. The interactive XDND gesture is screenshot-verified (it can
        // not be scripted); the ingestion core is unit-tested.
        DropArea {
            id: editorDropArea
            objectName: "editorDropArea"
            anchors.fill: scrollView
            z: 40
            property real dropY: -1
            onEntered: function(drag) { drag.accepted = true; dropY = drag.y }
            onPositionChanged: function(drag) { dropY = drag.y }
            onExited: dropY = -1
            onDropped: function(drop) { dropY = -1; root.handleEditorDrop(drop) }

            // The drop-indicator line (§5.4), following the pointer.
            Rectangle {
                visible: editorDropArea.containsDrag
                width: parent.width
                height: 2
                radius: 1
                color: Theme.accent
                y: Math.max(0, editorDropArea.dropY)
            }
        }

        ScrollView {
            id: scrollView
            objectName: "editorScrollView"

            // §10.2 maximum content width: when capped, the extra space
            // becomes symmetric margins, centering the block column
            // (delegates cannot be x-offset: ListView re-asserts item
            // positions on every relayout).
            readonly property int centeringMargin: {
                var max = Typography.maxContentWidth
                // Focus mode (§16.1) centers the column even when the user has
                // left the max width uncapped: a fullscreen edge-to-edge line
                // would be the opposite of focused, so it applies a readable
                // default (honoring an explicit max width if one is set).
                if (max <= 0 && root.focusMode)
                    max = 760
                if (max <= 0)
                    return 0
                return Math.max(0, Math.floor((parent.width - 40 - max) / 2))
            }

            anchors.fill: parent
            anchors.margins: 20
            anchors.leftMargin: 20 + centeringMargin
            anchors.rightMargin: 20 + centeringMargin
            anchors.topMargin: tagStrip.visible ? tagStrip.height + 16 : 20

            contentWidth: availableWidth

            ListView {
                id: blockListView
                objectName: "blockListView"

                width: parent.width
                // Blank-line rhythm between blocks (§10.2).
                spacing: Typography.paragraphSpacing

                reuseItems: true
                // Keep a small offscreen row window warm for ordinary
                // wheel/flick movement without making startup instantiate a
                // large variable-height document through the buffer.
                cacheBuffer: 240

                // §16.2 typewriter mode: caret-line centering scrolls smoothly.
                // The animation is enabled only in typewriter mode so ordinary
                // scrolling, find-bar jumps, and drag auto-scroll are unchanged.
                Behavior on contentY {
                    // Typewriter scroll honors reduced motion (§14.3): 0
                    // duration stills it instantly.
                    enabled: root.typewriterMode && Theme.motionScale > 0
                    NumberAnimation { duration: 130 * Theme.motionScale
                                      easing.type: Easing.OutQuad }
                }

                model: BlockModel

                // One delegate per block type; paragraphs and headings
                // share the default text choice.
                // The chooser watches delegateKind, not blockType: it
                // recreates a row's delegate whenever the watched role
                // changes, and heading conversions must not drop focus.
                delegate: DelegateChooser {
                    id: blockDelegateChooser
                    role: "delegateKind"

                    // Kinds a linked module registered: each becomes a
                    // DelegateChoice of its own here. This runs
                    // before the view creates its first row, and the open
                    // build registers nothing, so the choices below are the
                    // whole story unless a premium module is linked in.
                    //
                    // The order matters: DelegateChooser takes the FIRST
                    // choice whose roleValue matches, and a choice with no
                    // roleValue matches everything — which is why the text
                    // delegate at the bottom names its kind explicitly
                    // instead of acting as a catch-all that would shadow
                    // every appended choice.
                    Component.onCompleted: {
                        var registered = BlockKindRegistry.registeredDelegates()
                        for (var i = 0; i < registered.length; ++i) {
                            var entry = registered[i]
                            var component = Qt.createComponent(entry.delegateUrl)
                            if (component.status !== Component.Ready) {
                                console.warn("block kind '" + entry.language
                                             + "' has no usable delegate: "
                                             + component.errorString())
                                continue
                            }
                            var choice = Qt.createQmlObject(
                                'import QtQml.Models; DelegateChoice { }',
                                blockDelegateChooser)
                            choice.roleValue = entry.kind
                            choice.delegate = component
                            blockDelegateChooser.choices.push(choice)
                        }
                    }

                    DelegateChoice {
                        roleValue: Block.BulletList
                        BulletListDelegate { width: blockListView.width }
                    }
                    DelegateChoice {
                        roleValue: Block.NumberedList
                        NumberedListDelegate { width: blockListView.width }
                    }
                    DelegateChoice {
                        roleValue: Block.Todo
                        TodoDelegate { width: blockListView.width }
                    }
                    DelegateChoice {
                        roleValue: Block.Quote
                        QuoteDelegate { width: blockListView.width }
                    }
                    DelegateChoice {
                        roleValue: Block.CodeBlock
                        CodeBlockDelegate { width: blockListView.width }
                    }
                    DelegateChoice {
                        roleValue: Block.Divider
                        DividerDelegate { width: blockListView.width }
                    }
                    DelegateChoice {
                        roleValue: Block.Image
                        ImageBlock { width: blockListView.width }
                    }
                    DelegateChoice {
                        roleValue: Block.Callout
                        CalloutBlock { width: blockListView.width }
                    }
                    DelegateChoice {
                        roleValue: Block.Table
                        TableBlock { width: blockListView.width }
                    }
                    DelegateChoice {
                        roleValue: Block.MathBlock
                        MathBlock { width: blockListView.width }
                    }
                    DelegateChoice {
                        roleValue: Block.Media
                        MediaBlock { width: blockListView.width }
                    }
                    DelegateChoice {
                        // A `kanban`-tagged code fence renders as a board.
                        roleValue: BlockKinds.Kanban
                        KanbanBlock { width: blockListView.width }
                    }
                    DelegateChoice {
                        // A `toc`-tagged code fence: a read-only linked
                        // heading list.
                        roleValue: BlockKinds.Toc
                        TocBlock { width: blockListView.width }
                    }
                    DelegateChoice {
                        // An ![](url) image expression whose URL is a
                        // web/video embed: a preview card.
                        roleValue: BlockKinds.Embed
                        EmbedBlock { width: blockListView.width }
                    }
                    DelegateChoice {
                        // A `mermaid`-tagged code fence, rendered natively
                        // as a diagram.
                        roleValue: BlockKinds.Mermaid
                        DiagramBlock { width: blockListView.width }
                    }
                    DelegateChoice {
                        // A `query`-tagged code fence holding a live
                        // front-matter query.
                        roleValue: BlockKinds.Query
                        QueryBlock { width: blockListView.width }
                    }
                    DelegateChoice {
                        // Paragraphs and all four heading levels share kind 0
                        // (BlockModel::delegateKindFor). Named rather than left
                        // as the catch-all so module choices appended above are
                        // reachable.
                        roleValue: 0
                        TextBlockDelegate { width: blockListView.width }
                    }
                }

                // Enable move animations
                displaced: Transition {
                    NumberAnimation {
                        properties: "y"
                        duration: 200
                        easing.type: Easing.OutQuad
                    }
                }

                move: Transition {
                    NumberAnimation {
                        properties: "y"
                        duration: 200
                        easing.type: Easing.OutQuad
                    }
                }

                // Add remove animation for visual feedback
                remove: Transition {
                    NumberAnimation {
                        property: "opacity"
                        to: 0
                        duration: 150
                    }
                }

                // Add insert animation
                add: Transition {
                    NumberAnimation {
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 150
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }
        }

        // Multi-block drop indicator: a line naming the
        // insertion gap. Parented to the ListView viewport (not its
        // contentItem), so the y computation subtracts contentY.
        Rectangle {
            id: dropIndicator
            objectName: "dropIndicator"
            parent: blockListView
            visible: blockDragState.active && blockDragState.isMulti
                     && blockDragState.indicatorGap >= 0
            x: 40
            width: blockListView.width - 48
            height: 3
            radius: 1.5
            color: Theme.accent
            y: {
                var gap = blockDragState.indicatorGap
                if (gap < 0)
                    return 0
                var yContent = 0
                if (gap < blockListView.count) {
                    var item = (blockListView.itemAtIndex(gap) as BlockDelegateBase)
                    yContent = item ? item.y - blockListView.spacing / 2 : 0
                } else {
                    var last = (blockListView.itemAtIndex(blockListView.count - 1) as BlockDelegateBase)
                    yContent = last ? last.y + last.height
                                      + blockListView.spacing / 2 : 0
                }
                return yContent - blockListView.contentY - height / 2
            }
        }

        // The floating find/replace bar (features.md §7): overlays the
        // editor's top-right corner, so opening it reflows nothing.
        // Placed after the ScrollView so
        // presses on it never reach the selection-clearing MouseArea.
        FormattingBar {
            id: formattingBar
            target: appToolbar.targetBlock
            listView: blockListView
        }

        FindBar {
            id: findBar
            appWindow: root
            listView: blockListView
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 8
            anchors.topMargin: tagStrip.visible ? tagStrip.height + 12 : 8
        }
    }

    // features.md §17.1 document outline dock: a right-side pane over the
    // editor, toggled from the view menu. Placed after the editor Rectangle so
    // it sits above it; the editor's right margin reserves its width.
    OutlinePanel {
        id: outlinePanel
        objectName: "outlinePanel"
        appWindow: root
        visible: root.outlineVisible && !root.focusMode
        width: visible ? root.outlineWidth : 0
        anchors.top: appToolbar.visible ? appToolbar.bottom : parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.bottomChromeHeight
    }

    // Backlinks pane: collection mode only, left of the outline when
    // both are open.
    BacklinksPanel {
        id: backlinksPanel
        objectName: "backlinksPanel"
        appWindow: root
        visible: root.backlinksVisible && root.collectionOpen
                 && !root.focusMode
        width: visible ? root.backlinksWidth : 0
        anchors.top: appToolbar.visible ? appToolbar.bottom : parent.top
        anchors.right: outlinePanel.visible ? outlinePanel.left
                                            : parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.bottomChromeHeight
    }

    Loader {
        id: extensionBottomBar
        objectName: "extensionBottomBar"
        source: Extensions.slotSource("bottomBar")
        active: source != ""
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.visible ? statusBar.top : parent.bottom
        height: active && item ? (item as Item).implicitHeight : 0
        // Focus mode hides the chrome (§16.1); an extension bar is chrome.
        visible: !root.focusMode
    }

    Loader {
        id: extensionSidePanel
        objectName: "extensionSidePanel"
        source: Extensions.slotSource("sidePanel")
        active: source != ""
        anchors.top: appToolbar.visible ? appToolbar.bottom : parent.top
        anchors.right: backlinksPanel.visible ? backlinksPanel.left
                     : outlinePanel.visible ? outlinePanel.left
                     : parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.bottomChromeHeight
        width: active && item && visible ? (item as Item).implicitWidth : 0
        visible: active && !root.focusMode
    }

    // The status bar (features.md §9.7). Anchored and shown from here, since
    // the window owns its layout and the view menu owns its visibility; what
    // it reports about the document is in EditorStatusBar.qml.
    EditorStatusBar {
        id: statusBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.statusBarVisible && !root.focusMode

        appWindow: root
        listView: blockListView
        targetBlock: appToolbar.targetBlock
        statisticsPanel: root.statisticsPanel

        onWritingGoalRequested: goalDialog.openFor(root.currentNoteRelPath)
        onCreateVaultRequested: createVaultDialog.open()
    }
}
