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

    // Window geometry is persisted like the panel layout, by the component
    // below; nothing is saved until the stored geometry has been applied, or
    // the defaults would overwrite it.
    property bool geometryRestored: false
    onWidthChanged: sessionPersistence.scheduleGeometrySave()
    onHeightChanged: sessionPersistence.scheduleGeometrySave()
    onXChanged: sessionPersistence.scheduleGeometrySave()
    onYChanged: sessionPersistence.scheduleGeometrySave()
    onVisibilityChanged: {
        if (!geometryRestored)
            return
        // Full screen (focus mode) and minimized leave the flag alone.
        // Qualified: a bare `visibility` here resolves to the signal's
        // injected parameter, which Qt 6.10 warns about as undeclared.
        if (root.visibility === Window.Maximized)
            AppSettings.setValue("window.maximized", true)
        else if (root.visibility === Window.Windowed)
            AppSettings.setValue("window.maximized", false)
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

    // ---- Completion-driven block geometry -------------------------------
    // ListView batches model and delegate geometry changes. Every block row
    // reports height changes through BlockDelegateBase; diagrams additionally
    // report their asynchronous render lifecycle. One callLater coalesces all
    // notifications from the turn and forceLayout() processes the outstanding
    // list geometry before focus/reveal consumers read it.
    property bool blockRelayoutScheduled: false
    function blockGeometryChanged(item) {
        if (root.blockRelayoutScheduled)
            return
        root.blockRelayoutScheduled = true
        Qt.callLater(root.completeBlockRelayout)
    }
    function completeBlockRelayout() {
        root.blockRelayoutScheduled = false
        blockListView.forceLayout()
        root.scheduleFocusedBlockPosition()
        root.scheduleReveal()
    }

    // Scroll the editor the least it can to put `item` fully inside the
    // viewport, and not at all when it is already there. The target and its
    // containing block stay connected while it owns focus, so growth follows
    // geometry changes rather than an eight-tick settling window. A newer
    // target replaces it; focus loss or manual scrolling cancels it.
    property Item revealTarget: null
    property Item revealBlock: null
    property bool revealScheduled: false
    function containingBlock(item) {
        var candidate = item
        while (candidate && candidate !== blockListView) {
            var block = candidate as BlockDelegateBase
            if (block)
                return block
            candidate = candidate.parent
        }
        return null
    }
    function revealItem(item) {
        if (!item || !blockListView.contentItem)
            return
        root.revealTarget = item
        root.revealBlock = root.containingBlock(item)
        root.scheduleReveal()
    }
    function scheduleReveal() {
        if (!root.revealTarget || root.revealScheduled)
            return
        root.revealScheduled = true
        Qt.callLater(function() {
            root.revealScheduled = false
            root.applyReveal()
        })
    }
    function cancelRevealTracking() {
        root.revealTarget = null
        root.revealBlock = null
    }
    function applyReveal() {
        var item = root.revealTarget
        if (!item || !item.visible || !blockListView.contentItem)
            return
        var top = item.mapToItem(blockListView.contentItem, 0, 0).y
        var bottom = top + item.height
        var margin = 16
        var target = blockListView.contentY
        if (bottom + margin > target + blockListView.height)
            target = bottom + margin - blockListView.height
        if (top - margin < target)
            target = top - margin
        if (target === blockListView.contentY)
            return
        // The list's own bottom margin is scrollable space past the last
        // block, so the reveal may use it: a card at the very end of a note
        // has nothing below it to scroll into view otherwise.
        var maxY = Math.max(0, blockListView.contentHeight + blockListView.bottomMargin
                               - blockListView.height)
        blockListView.contentY = Math.max(0, Math.min(target, maxY))
    }
    Connections {
        target: root.revealTarget
        function onHeightChanged() { root.scheduleReveal() }
        function onYChanged() { root.scheduleReveal() }
        function onVisibleChanged() {
            if (root.revealTarget && root.revealTarget.visible)
                root.scheduleReveal()
            else
                root.cancelRevealTracking()
        }
    }
    Connections {
        target: root.revealBlock
        function onHeightChanged() { root.scheduleReveal() }
        function onYChanged() { root.scheduleReveal() }
    }

    function openSettingsDialog() { settingsDialog.open() }

    // ---- Focusing a block by index --------------------------------------
    // The editor list is virtualized, so a row only exists once the view has
    // positioned and created it. Requests set currentIndex and position once;
    // currentItemChanged or the delegate's Component.onCompleted then finishes
    // the focus. Once the caret lands, the row's heightChanged signal keeps it
    // contained until focus leaves, the user scrolls, or a newer request wins.
    // The one timer below is only a generous cancellation/error guard.
    property int focusRequestGeneration: 0
    property bool focusRequestPending: false
    property int focusTargetIndex: -1
    property bool focusTargetAtEnd: false
    property string focusTargetTyped: ""
    property BlockDelegateBase focusWatchItem: null
    property bool focusWatchHasFocus: false
    property bool focusPositionScheduled: false
    property bool trackedFocusValidationScheduled: false

    function focusBlockAtIndex(index, atEnd, typed) {
        if (BlockModel.count === 0)
            return
        var idx = Math.max(0, Math.min(index, BlockModel.count - 1))
        root.focusRequestGeneration++
        root.clearBlockFocusLifecycle()
        root.focusTargetIndex = idx
        root.focusTargetAtEnd = atEnd === true
        root.focusTargetTyped = typed === undefined ? "" : typed
        root.focusRequestPending = true
        blockFocusGuard.generation = root.focusRequestGeneration
        blockFocusGuard.restart()
        blockListView.currentIndex = idx
        blockListView.positionViewAtIndex(idx, ListView.Contain)
        root.applyPendingBlockFocus(blockListView.itemAtIndex(idx))
    }
    function blockDelegateReady(item) {
        if (!root.focusRequestPending || !item)
            return
        if (item === blockListView.itemAtIndex(root.focusTargetIndex))
            root.applyPendingBlockFocus(item)
    }
    function applyPendingBlockFocus(candidate) {
        if (!root.focusRequestPending)
            return false
        var item = candidate as BlockDelegateBase
        if (!item || item !== blockListView.itemAtIndex(root.focusTargetIndex))
            item = (blockListView.itemAtIndex(root.focusTargetIndex)
                    as BlockDelegateBase)
        if (!item)
            return false

        if (root.focusTargetAtEnd)
            item.focusAtEnd()
        else
            item.focusAtStart()
        if (root.focusTargetTyped !== "") {
            item.typeText(root.focusTargetTyped)
            root.focusTargetTyped = ""
        }

        root.focusRequestPending = false
        root.focusWatchItem = item
        root.focusWatchHasFocus = false
        root.updateTrackedFocus()
        return true
    }
    function activeFocusIsInside(item) {
        if (!item)
            return false
        var focusItem = root.activeFocusItem
        while (focusItem) {
            if (focusItem === item)
                return true
            focusItem = focusItem.parent
        }
        return false
    }
    function updateTrackedFocus() {
        var item = root.focusWatchItem
        if (!item)
            return
        if (root.activeFocusIsInside(item)) {
            root.focusWatchHasFocus = true
            blockFocusGuard.stop()
            root.scheduleFocusedBlockPosition()
        } else if (root.focusWatchHasFocus) {
            root.clearBlockFocusLifecycle()
        }
    }
    function scheduleTrackedFocusValidation() {
        if (root.trackedFocusValidationScheduled)
            return
        root.trackedFocusValidationScheduled = true
        Qt.callLater(function() {
            root.trackedFocusValidationScheduled = false
            root.updateTrackedFocus()
            if (root.revealTarget
                    && !root.activeFocusIsInside(root.revealTarget))
                root.cancelRevealTracking()
        })
    }
    onActiveFocusItemChanged: root.scheduleTrackedFocusValidation()

    function scheduleFocusedBlockPosition() {
        if (!root.focusWatchItem || !root.focusWatchHasFocus
                || root.focusPositionScheduled)
            return
        root.focusPositionScheduled = true
        Qt.callLater(function() {
            root.focusPositionScheduled = false
            if (!root.focusWatchItem || !root.focusWatchHasFocus)
                return
            blockListView.forceLayout()
            // Contain shows the whole row where it fits and puts its top at the
            // top of the view where it does not, which for a table is its header.
            blockListView.positionViewAtIndex(root.focusTargetIndex,
                                              ListView.Contain)
        })
    }
    function clearBlockFocusLifecycle() {
        root.focusRequestPending = false
        root.focusWatchItem = null
        root.focusWatchHasFocus = false
        blockFocusGuard.stop()
    }
    function cancelGeometryTrackingForMovement() {
        if (root.focusRequestPending || root.focusWatchItem) {
            root.focusRequestGeneration++
            root.clearBlockFocusLifecycle()
        }
        root.cancelRevealTracking()
    }
    Connections {
        target: root.focusWatchItem
        function onHeightChanged() { root.scheduleFocusedBlockPosition() }
    }
    Timer {
        id: blockFocusGuard
        objectName: "blockFocusGuard"
        property int generation: 0
        interval: 5000
        repeat: false
        onTriggered: {
            if (blockFocusGuard.generation !== root.focusRequestGeneration)
                return
            if (root.focusRequestPending
                    || (root.focusWatchItem && !root.focusWatchHasFocus)) {
                console.warn("Block delegate did not become focus-ready at index "
                             + root.focusTargetIndex)
                root.clearBlockFocusLifecycle()
            }
        }
    }

    // ---- Keyboard accessibility: focus and pane navigation (§14.1) ----
    // Skip-navigation: land on the current (or first) editor block, bypassing
    // the chrome. Bound to F6's pane cycle and the View menu.
    function focusEditor() {
        root.focusBlockAtIndex(root.lastFocusedBlock)
    }
    // Which major pane last took focus (0 sidebar, 1 note list, 2 editor,
    // 3 toolbar), so F6 can cycle to the next visible one — the standard
    // desktop region key. The toolbar is in the cycle because Insert,
    // Templates, View and the customization menu have no other shortcut, so
    // leaving it out left those actions with no keyboard route at all.
    property int focusedPane: 2
    function focusPane(p) {
        root.focusedPane = p
        if (p === 0 && !root.sidebarCollapsed && root.collectionOpen)
            sidebar.focusPane()
        else if (p === 1 && !root.noteListCollapsed && root.collectionOpen)
            noteListPane.focusPane()
        else if (p === 3 && appToolbar.visible)
            appToolbar.focusPane()
        else
            focusEditor()
    }
    function cyclePane() {
        var order = []
        if (root.collectionOpen && !root.sidebarCollapsed) order.push(0)
        if (root.collectionOpen && !root.noteListCollapsed) order.push(1)
        order.push(2)  // the editor is always present
        if (appToolbar.visible) order.push(3)
        var cur = order.indexOf(root.focusedPane)
        focusPane(order[(cur + 1) % order.length])
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
        function onRevealItemRequested(item) { root.revealItem(item) }
        function onTextContextMenuRequested(target) { root.openTextContextMenu(target) }
        function onLinkContextMenuRequested(target) { root.openLinkContextMenu(target) }
        function onBlockHandleMenuRequested(target) { root.openBlockHandleMenu(target) }
        function onInsertImageRequested(index, kind) {
            root.insertImageIntoBlock(index, kind)
        }
        function onInsertEmbedRequested(index) { root.insertEmbedIntoBlock(index) }
        function onEditEmbedRequested(index, url) { root.editEmbedInBlock(index, url) }
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

    // ---- Session state that outlives the window --------------------------
    // Reading the settings store back at startup, and the writes driven by
    // the models rather than by this window, are in SessionPersistence.qml.
    // The one-line writes above stay beside the properties they persist.
    SessionPersistence {
        id: sessionPersistence
        appWindow: root
        toolbar: appToolbar
        findBar: root.findBar
        sidebarPanel: sidebar
    }

    // The integration suite presets values and calls this to exercise the
    // read path, so the name stays on the window.
    function applyPersistedSessionState() { sessionPersistence.restore() }

    Component.onCompleted: {
        applyPersistedSessionState()
        refreshSessionBaseline()
        installNativeMenuBar()
    }

    onPanelsVisibleChanged:
        AppSettings.setValue("panels.visible", panelsVisible)

    // A table-of-contents fence's stored body is derived from the headings,
    // and TocFenceSync.qml keeps it current.
    TocFenceSync {}

    Connections {
        target: blockListView
        function onCurrentIndexChanged() {
            DocumentOutline.setCurrentBlock(blockListView.currentIndex)
        }
    }

    // relPath of the open note ("" outside collection mode).
    readonly property string currentNoteRelPath:
        collectionOpen && DocumentManager.hasFile
            ? NoteCollection.relativePath(DocumentManager.currentFilePath) : ""

    // ---- The keyboard map ----------------------------------------------
    // Every window-level shortcut is in AppShortcuts.qml, along with the
    // mouse back/forward buttons, which are the same two navigation commands
    // arriving from a different device. Shortcuts that belong to one
    // workflow — Escape during a drag, quick capture — stay with the
    // component that answers them, provided that component is built with the
    // window; anything behind a lazy Loader cannot hold a shortcut.
    AppShortcuts {
        anchors.fill: parent
        // The back/forward area inside covers the window and has to sit above
        // it, which is what its own z said while it was a child of the window.
        z: 10000
        appWindow: root
        findBar: root.findBar
        quickSwitcher: root.quickSwitcher
        sidebarPanel: sidebar
    }

    // ---- The open note -------------------------------------------------
    // Which note is open, and every transition into another one, is in
    // NoteSession.qml. The calls below are the names its callers already use:
    // the delegates reach them through AppActions, the side panels through
    // this window, and the integration suite drives several directly.
    NoteSession {
        id: noteSession
        appWindow: root
        listView: blockListView
        findBar: root.findBar
        sidebarPanel: sidebar
    }

    // Drop ingestion lives in EditorDropArea now. Both of its entry points
    // keep their window-level names, which the delegates and the integration
    // suite call.
    function blockForPath(stored) { return editorDropArea.blockForPath(stored) }
    function insertBlocksAt(afterIndex, typedBlocks) {
        editorDropArea.insertBlocksAt(afterIndex, typedBlocks)
    }

    function openNoteByPath(relPath) { return noteSession.openNoteByPath(relPath) }
    function navigateBack() { noteSession.navigateBack() }
    function navigateForward() { noteSession.navigateForward() }
    function followWikiLink(spec) { noteSession.followWikiLink(spec) }
    function openSearchResult(relPath, blockIndex, displayStart) {
        noteSession.openSearchResult(relPath, blockIndex, displayStart)
    }
    function createNoteInCurrentScope() { noteSession.createNoteInCurrentScope() }
    function createFromTemplate(templateName) {
        return noteSession.createFromTemplate(templateName)
    }
    function saveCurrentNoteAsTemplate(name) {
        return noteSession.saveCurrentNoteAsTemplate(name)
    }
    function restoreRecoveredNote(relPath) {
        return noteSession.restoreRecoveredNote(relPath)
    }
    // Asked for by restoreRecoveredNote when the recovered note is the open
    // one and its buffer holds edits the journal does not: the dialog offers
    // replace / keep / cancel and calls back into the session.
    function confirmRecoveryOverwrite(relPath) {
        root.documentDialogs().confirmRecoveryOverwrite(relPath)
    }
    function replaceEditsWithRecovery(relPath) {
        return noteSession.replaceEditsWithRecovery(relPath)
    }
    function keepEditsOverRecovery(relPath) {
        noteSession.keepEditsOverRecovery(relPath)
    }
    // A failure the session has to put in front of the user. Named here so the
    // session does not have to know which child owns the error dialog.
    function showDocumentError(message) { root.documentDialogs().showError(message) }
    // The conflict banner's two buttons (§12.1), and the one entry point that
    // raises it — both the file watcher and the collection report an external
    // change, and both arrive here.
    function keepMine() { return noteSession.keepMine() }
    function loadTheirs(absPath) { return noteSession.loadTheirs(absPath) }
    function noteChangedOnDisk(absPath) { noteSession.noteChangedOnDisk(absPath) }

    // ---- Renaming a note, and the links that point at it ----------------
    // NoteRenameWorkflow.qml owns the plan-then-apply sequence and its two
    // dialogs. The note list and folder tree ask this window to rename, so
    // the requests keep their names here.
    NoteRenameWorkflow {
        id: renameWorkflow
        // Its dialogs centre on this window, so it spans it.
        anchors.fill: parent
        appWindow: root
    }

    // A note created before it had a name takes one from its first block,
    // once, through that same rename path.
    NoteAutoTitle {
        id: noteAutoTitle
        objectName: "noteAutoTitle"
        appWindow: root
        renameWorkflow: renameWorkflow
    }

    function requestNoteRename(relPath, newTitle) {
        renameWorkflow.requestNoteRename(relPath, newTitle)
    }
    function requestNoteMove(relPath, targetFolder) {
        renameWorkflow.requestNoteMove(relPath, targetFolder)
    }
    function requestFolderRename(relPath, newName, afterApply) {
        renameWorkflow.requestFolderRename(relPath, newName, afterApply)
    }
    // Driven directly by the integration suite, which skips the dialog.
    function finishRenamePlan(updateLinks) {
        renameWorkflow.finishRenamePlan(updateLinks)
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

    // No Save As shortcut: StandardKey.SaveAs resolves to Ctrl+Shift+S,
    // which features.md §13 assigns to strikethrough (the spec's shortcut
    // table gives Save As no binding). Ctrl+S on an untitled document
    // still opens the save dialog.

    // One route for the File menu and Ctrl+S. Loading the session-dialog
    // component first also wires save failures to its error dialog; without
    // it, a first save attempt could fail before that lazy component existed.
    function saveCurrentDocument(forceSaveAs) {
        root.documentDialogs()
        if (forceSaveAs || !DocumentManager.hasFile)
            return DocumentManager.saveFileDialog()
        return DocumentManager.saveAsync()
    }

    // Opens link targets (features.md §2.4). Routed through one object so
    // tests can observe activations without launching a browser.
    property alias linkOpener: linkOpener
    QtObject {
        id: linkOpener
        property bool openExternally: true
        signal activated(string url)
        // The last target handed to the desktop, so a test can observe that
        // the browser branch really was reached rather than only that the
        // activation signal fired.
        property string lastExternalTarget: ""
        // What a click that opened nothing leaves behind. The address goes to
        // the clipboard, so the answer to "it did not open" is a paste away
        // rather than a retype.
        function reportNoBrowser(target) {
            Clipboard.text = target
            root.showTransientStatus(
                qsTr("No web browser available. Link copied to the clipboard."))
        }
        function activate(url) {
            // The target arrives as a plain string (a raw href or wiki-note
            // name); it is deliberately not a QUrl, whose string form would
            // percent-encode a space to %20 and carry that into the note name.
            // Normalize defensively so a null/undefined never reaches indexOf.
            var target = String(url === undefined || url === null ? "" : url)
            if (target.length === 0)
                return
            activated(target)
            // Wiki-link: kvit-note:target#heading resolves through the
            // collection and opens in-app — creating the note when the
            // target dangles — never a browser.
            if (target.indexOf("kvit-note:") === 0) {
                root.followWikiLink(target.substring(10))
                return
            }
            // Internal document link: #slug resolves
            // through the shared slug function to a heading and scrolls there,
            // rather than opening a browser. An unresolved slug is a
            // recoverable no-op with a status-bar note, never an error.
            if (target.charAt(0) === "#") {
                var slug = target.substring(1)
                var idx = DocumentOutline.blockIndexForSlug(slug)
                if (idx >= 0) {
                    root.scrollToBlock(idx)
                } else {
                    root.showTransientStatus(
                        qsTr("No heading “") + slug + qsTr("”"))
                }
                return
            }
            linkOpener.lastExternalTarget = target
            if (!openExternally)
                return
            // Through UrlLauncher rather than Qt.openUrlExternally, which on
            // Unix answers true whether or not anything opened (see
            // urllauncher.h). The verdict arrives as a signal, because
            // establishing it means running an opener and watching it.
            UrlLauncher.open(target)
        }
    }
    Connections {
        target: UrlLauncher
        function onFailed(url) { linkOpener.reportNoBrowser(url) }
        // A scheme this application does not hand to the desktop at all. Said
        // plainly, because from the reader's side it is the same click that
        // did nothing, and the reason is different.
        function onRefused(url) {
            root.showTransientStatus(
                qsTr("This kind of link is not opened: ") + url)
        }
    }

    // The Ctrl+K link dialog (features.md §2.4): display-text and URL
    // fields; prefilled when invoked inside an existing link; "Remove
    // link" replaces the span with its bare text. All edits go through
    // the model, like every formatting command.
    // The Ctrl+K link dialog (features.md §2.4), in LinkDialog.qml. A
    // delegate opens it through AppActions and the integration suite reaches
    // it by name, so the window keeps the alias.
    property alias linkDialog: linkDialog
    LinkDialog {
        id: linkDialog
        listView: blockListView
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
        onApplied: function(blockIndex, type, opensDialog) {
            Qt.callLater(function() {
                blockListView.currentIndex = blockIndex
                // An entry that opened an insert dialog (image, embed, table
                // size grid) leaves the keyboard to that dialog. Focusing the
                // block here would take it straight back, one tick after the
                // dialog appeared. The insert flow focuses the block itself
                // once it has what it asked for.
                if (opensDialog)
                    return
                var item = (blockListView.itemAtIndex(blockIndex) as BlockDelegateBase)
                if (item)
                    item.focusAtStart()
            })
        }
    }

    // Cross-block text selection and block drag-and-drop are two gestures
    // over the same list, sharing the edge auto-scroller that keeps the
    // pointer's end of the list in view. KvitShell declares both states
    // because the delegates read them; the behaviour is in the components.
    EdgeAutoScroller {
        id: edgeScroller
        listView: blockListView
    }

    crossBlockDrag: CrossBlockTextDrag {
        listView: blockListView
        scroller: edgeScroller
    }

    blockDrag: BlockDragController {
        id: blockDragState
        listView: blockListView
        scroller: edgeScroller
        dragLayer: blockDragLayer
        selectionKeys: selectionKeyHandler
    }

    // The multi-block floating proxy draws over the whole shell, which is
    // what the z value on the proxy used to say from here. Single-block
    // drags use only the live-moving row.
    BlockDragLayer {
        id: blockDragLayer
        anchors.fill: parent
        z: 1000
        dragState: blockDragState
        listView: blockListView
    }

    // Keys while a block selection is active (features.md §3.1).
    // Entering block selection focuses this item — the blurred
    // TextArea's reveal collapses and any open block
    // menu dismisses, both intended. Escape/Enter return to editing;
    // plain Up/Down move the collapsed selection; Ctrl+Shift+Up/Down
    // extend it; printable keys are deliberately inert (typing never
    // replaces a block selection).
    property alias findBar: findBar
    // Keys while a block selection is active (features.md §3.1), in
    // BlockSelectionKeys.qml. Entering block selection focuses this item,
    // which blurs the editing block — its reveal collapses and any open block
    // menu dismisses, both intended.
    property alias selectionKeyHandler: selectionKeyHandler
    BlockSelectionKeys {
        id: selectionKeyHandler
        listView: blockListView
        gapCursor: blockGapCursor

        // An oversized paste is confirmed by the window's dialog, which then
        // performs the insert itself.
        onOversizedPasteRequested: function(text, insertAt, plain) {
            largePasteConfirmDialog.pendingText = text
            largePasteConfirmDialog.pendingIndex = insertAt
            largePasteConfirmDialog.pendingPlain = plain
            largePasteConfirmDialog.pendingStripFormatting = plain
            largePasteConfirmDialog.pendingFocusLast = false
            largePasteConfirmDialog.open()
        }
    }

    // The caret between two blocks (features.md §3.7), in BlockGapCursor.qml.
    // A mode of the same shape as block selection above: it takes the focus
    // while it is placed, and typing into it makes the block. It draws in the
    // block list's own seams, so it is suspended while a drag is drawing its
    // drop indicator in them.
    property alias blockGapCursor: blockGapCursor
    BlockGapCursor {
        id: blockGapCursor
        listView: blockListView
        appWindow: root
        dragState: blockDragState

        onOversizedPasteRequested: function(text, insertAt, plain,
                                            stripFormatting) {
            largePasteConfirmDialog.pendingText = text
            largePasteConfirmDialog.pendingIndex = insertAt
            largePasteConfirmDialog.pendingPlain = plain
            largePasteConfirmDialog.pendingStripFormatting = stripFormatting
            largePasteConfirmDialog.pendingFocusLast = true
            largePasteConfirmDialog.open()
        }
    }

    // Right-click anywhere in a row's gutter opens that block's menu (§9.5).
    //
    // Inside the strip only the drag handle answered a right press, and it is
    // fourteen pixels wide and only there while the pointer is on the row, so
    // the menu was reachable by whoever already knew where it was. This sits
    // at the back of the list's content item — the same stacking the gap
    // cursor's hover probe uses (BlockGapCursor.qml) — because Qt offers a
    // press to the items in front first: a press on a block's own chrome or
    // text never reaches here, and one on the strip does, for all twelve
    // block kinds without each growing a handler of its own.
    //
    // The strip is 44px wide and an indented row's content starts one indent
    // step further in again, so the band widens with the indent. The indent
    // is read from the model rather than the row because it is the twelve
    // delegate types that carry it, not the interface the shell sees.
    MouseArea {
        objectName: "gutterMenuArea"
        parent: blockListView.contentItem
        z: -1
        width: blockListView.width
        height: blockListView.contentHeight
        acceptedButtons: Qt.RightButton
        onPressed: function(mouse) {
            var idx = blockListView.indexAt(Math.max(1, mouse.x), mouse.y)
            var block = idx >= 0 ? BlockModel.blockAt(idx) : null
            if (!block || mouse.x >= 44 + block.indentLevel * 24) {
                mouse.accepted = false
                return
            }
            AppActions.requestBlockHandleMenu(blockListView.itemAtIndex(idx))
        }
    }

    // ---- Opening, starting and closing a document -----------------------
    // The transitions that can lose work, and the dialogs that ask before
    // they do, are in DocumentSessionDialogs.qml. The error dialog lives
    // there too, because a failed save or open is the same conversation.
    // Seven dialogs — recovery, unsaved-close, errors, vault creation and
    // the rest — none of which a session needs unless something goes wrong
    // or the user asks. Built on first use, like the context menus above.
    // Nothing that has to exist before the user asks for it may go inside
    // this component. Ctrl+N and Ctrl+O were declared here as Shortcut items
    // and were dead on a fresh window for that reason, so they now live in
    // AppShortcuts.qml and call in through documentDialogs() only when they
    // have a question to ask.
    Loader {
        id: documentDialogsLoader
        // Its dialogs centre on this window, so it spans it.
        anchors.fill: parent
        active: false
        sourceComponent: DocumentSessionDialogs {
            anchors.fill: parent
            appWindow: root

            onImportRequested: importDialog.openDialog()
        }
    }
    function documentDialogs() {
        documentDialogsLoader.active = true
        return documentDialogsLoader.item as DocumentSessionDialogs
    }

    // Open a file chosen from the native picker, routed by the window's mode
    // (multi-vault.md §): a vault window opens the file in its own single-file
    // window (raising an existing one if that file is already open), so the
    // vault it is showing is left intact; a single-file window replaces its
    // document in place, the historical behavior. The picker is shown first so
    // the path can be routed rather than opened blindly.
    function openFileFromDialog() {
        var path = DocumentManager.chooseFileToOpen()
        if (path === "")
            return
        if (root.collectionOpen)
            AppActions.requestOpenFileInNewWindow(path)
        else
            DocumentManager.open(DocumentManager.toLocalFileUrl(path))
    }

    // Native folder picker behind "Open Folder…" and "Open Folder in New
    // Window…". inNewWindow chooses which route the chosen folder takes; both
    // raise an already-open window instead of duplicating it. The window owns
    // the picker because both the toolbar's File menu and the macOS menu bar
    // reach it, and only one of those exists at a time.
    function openFolderFromDialog(inNewWindow) {
        openFolderDialog.inNewWindow = inNewWindow === true
        openFolderDialog.open()
    }
    FolderDialog {
        id: openFolderDialog
        objectName: "toolbarOpenFolderDialog"
        // Owned by the application window, and where the platform has no
        // folder chooser of its own, shown inside it: the dialog Qt builds
        // in that case is a top-level window, and an unowned one never gives
        // the keyboard focus back on Wayland when it closes.
        parentWindow: root
        popupType: Popup.Item
        property bool inNewWindow: false
        title: qsTr("Open Folder as Vault")
        onAccepted: {
            var path = DocumentManager.toLocalPath(openFolderDialog.selectedFolder)
            if (path === "")
                return
            if (openFolderDialog.inNewWindow)
                AppActions.requestOpenVaultInNewWindow(path)
            else
                AppActions.requestOpenVault(path)
        }
    }

    SettingsDialog {
        id: settingsDialog
    }

    // features.md §13 discoverable keyboard-shortcut reference.
    function openShortcutReference() { shortcutReference.open() }
    ShortcutReference {
        id: shortcutReference
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

    // features.md §15 system integration: the tray icon, the system-wide
    // hotkey and the quick-capture window, in SystemIntegration.qml. The
    // integration suite opens capture directly, so the name stays here.
    SystemIntegration {
        id: systemIntegration
        appWindow: root
    }

    function openQuickCapture() { systemIntegration.openQuickCapture() }

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

    // ---- Context menus (features.md §9.5) ---------------------------
    // The right-click menus live in EditorContextMenus.qml; what stays here
    // is the shell-level surface they answer. contextMenuHoldsSelection is a
    // KvitShell query a delegate makes on itself, and the three open calls
    // arrive from AppActions, so both have to be reachable on the window.
    // Built on the first right-click rather than at startup. These are five
    // full menus with their items, actions and separators — measured at
    // about 800 QObjects, the largest single thing the shell was creating
    // before the user had done anything — and a session that never opens a
    // context menu never needs any of it.
    Loader {
        id: contextMenusLoader
        anchors.fill: parent
        active: false
        sourceComponent: EditorContextMenus {
            anchors.fill: parent
            appWindow: root
            toolbar: appToolbar
            selectionKeys: selectionKeyHandler
        }
    }
    function contextMenus() {
        contextMenusLoader.active = true
        return contextMenusLoader.item
    }

    function contextMenuHoldsSelection(target) {
        // Asked on every right-click before deciding which menu to open, so
        // it must not be the thing that builds them: with no menu yet there
        // is no menu holding a selection.
        var menus = contextMenusLoader.item as EditorContextMenus
        return menus ? menus.holdsSelection(target) : false
    }
    function openTextContextMenu(target) {
        root.contextMenus().openTextMenu(target)
    }
    function openLinkContextMenu(target) {
        root.contextMenus().openLinkMenu(target)
    }
    function openBlockHandleMenu(target) {
        root.contextMenus().openHandleMenu(target)
    }

    // Whether Menu / Shift+F10 belongs to the editor rather than another
    // focused pane. Walk from the window's active focus item because text,
    // captions, table cells and the block-selection key handler have
    // different immediate parents but all converge on one of these roots.
    readonly property bool blockContextShortcutEnabled: {
        var item = root.activeFocusItem
        while (item) {
            if (item === blockListView || item === selectionKeyHandler)
                return true
            item = item.parent
        }
        return false
    }
    function openFocusedBlockContextMenu() {
        var target = null
        if (DocumentSelection.hasBlockSelection) {
            var selectedIndex = DocumentSelection.lastActiveIndex()
            if (selectedIndex >= 0 && selectedIndex < BlockModel.count)
                target = (blockListView.itemAtIndex(selectedIndex)
                          as BlockDelegateBase)
        } else {
            // The ListView's currentIndex is updated by editor operations,
            // but merely focusing a row does not promise to update it. Walk
            // up from the real focus item so this route cannot target a row
            // left current by an earlier mouse or drag operation.
            var item = root.activeFocusItem
            while (item && item !== blockListView) {
                target = (item as BlockDelegateBase)
                if (target)
                    break
                item = item.parent
            }
            if (!target && blockListView.currentIndex >= 0)
                target = (blockListView.itemAtIndex(blockListView.currentIndex)
                          as BlockDelegateBase)
        }
        if (target)
            root.contextMenus().openHandleMenu(target, true)
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

    // The image lightbox (§1.2.8): an image block opens it with a resolved
    // source. Declared below over the whole window at a high z.
    function openLightbox(source, alt) {
        lightbox.openImage(source, alt)
    }

    // Putting an image, a web embed or a table into an empty block: the
    // dialogs and the conversion they perform are in BlockInsertDialogs.qml.
    // A delegate asks for these through AppActions, so the window keeps the
    // three names.
    // The image, embed and table insertion pickers, built when one is asked
    // for.
    Loader {
        id: blockInsertsLoader
        anchors.fill: parent
        active: false
        sourceComponent: BlockInsertDialogs {
            anchors.fill: parent
            onFocusBlockRequested: function(index) {
                root.focusBlockAtIndex(index)
            }
        }
    }
    function blockInserts() {
        blockInsertsLoader.active = true
        return blockInsertsLoader.item as BlockInsertDialogs
    }

    // kind is "image" or "media"; it decides what the shared dialog is called
    // and which files it offers, not what the block becomes.
    function insertImageIntoBlock(idx, kind) {
        root.blockInserts().insertImage(idx, kind)
    }
    function insertEmbedIntoBlock(idx) { root.blockInserts().insertEmbed(idx) }
    function editEmbedInBlock(idx, url) { root.blockInserts().editEmbed(idx, url) }
    function insertTableIntoBlock(idx) { root.blockInserts().insertTable(idx) }

    // The folder holding the open file. Two workflows ask for it: a drop
    // ingests its assets beside the note, and the create-a-vault offer turns
    // this folder into the collection root.
    function currentNoteDir() {
        var p = DocumentManager.currentFilePath
        var idx = p.lastIndexOf("/")
        return idx >= 0 ? p.substring(0, idx) : ""
    }

    // The image lightbox overlay (§1.2.8), over the whole window.
    Lightbox {
        id: lightbox
        objectName: "lightbox"
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

    // Restoring a note from one of its backups, in BackupRestoreDialog.qml.
    // The tag strip's button opens it and the integration suite reaches it by
    // name, so the window keeps the alias.
    property alias backupDialog: backupDialog
    BackupRestoreDialog {
        id: backupDialog
        appWindow: root
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
        property bool pendingStripFormatting: false
        // Block-selection paste selects the inserted range; a paste at the
        // between-block caret instead resumes editing at the end of it.
        property bool pendingFocusLast: false

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
            var at = pendingIndex
            var focusLast = pendingFocusLast
            var pasted = pendingStripFormatting
                ? blockGapCursor.stripPastedFormatting(pendingText)
                : pendingText
            var count = pendingPlain
                ? DocumentSerializer.insertPlainTextAt(
                    BlockModel, at, pasted)
                : DocumentSerializer.insertMarkdownAt(
                    BlockModel, at, pasted)
            if (count > 0) {
                if (focusLast) {
                    Qt.callLater(function() {
                        root.focusBlockAtIndex(at + count - 1, true)
                    })
                } else {
                    selectionKeyHandler.selectRange(at, at + count - 1)
                }
            }
            pendingText = ""
            pendingPlain = false
            pendingStripFormatting = false
            pendingFocusLast = false
        }
        onRejected: {
            pendingText = ""
            pendingPlain = false
            pendingStripFormatting = false
            pendingFocusLast = false
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
        if (DocumentManager) {
            DocumentManager.flushPendingEdits()
            if (DocumentManager.isDirty) {
                if (DocumentManager.hasFile) {
                    // A save that fails on the way out is the worst case for
                    // data loss: there is no next attempt, and the recovery
                    // journal is not meant to cover an orderly quit. Keep the
                    // window open so the error is visible and the user can act.
                    if (!DocumentManager.save()) {
                        close.accepted = false
                        return
                    }
                } else {
                    // A dirty document that has never been saved: ask rather
                    // than discard, and treat cancel as "do not close". The
                    // dialog's Save/Discard re-close the window, re-entering
                    // here once the document is clean.
                    close.accepted = false
                    root.documentDialogs().confirmCloseUnsaved()
                    return
                }
            }
        }
        // The close is going through. If close-to-tray keeps the app resident
        // the window only hides, so this vault stays open; otherwise the window
        // is really going away, so tell the registry to release its vault.
        if (!(typeof SystemTray !== "undefined" && SystemTray.available
              && SystemTray.closeToTray))
            AppActions.notifyWindowClosing()
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
            // The model normally coalesces revisions for 20 ms. This
            // transition consumes its first row immediately, so synchronize
            // the projection instead of racing its rebuild timer.
            NoteListModel.rebuildNow()
            var next = NoteListModel.relPathAt(0)
            if (next !== "")
                root.openNoteByPath(next)
        }
        function onOperationFailed(message) {
            root.documentDialogs().showError(message)
        }
        // The vault is held by another Kvit process (a separate instance, or
        // another computer sharing the folder). Within this process the window
        // registry raises the window already showing a vault instead of
        // reaching this refusal, so this only fires across processes. Only one
        // session may write a vault: both would load the same state and the
        // second to save would discard the first's work. This window keeps
        // running as a plain editor, so File > Open still works on notes.
        function onVaultInUse(path, detail) {
            root.documentDialogs().showError(
                qsTr("%1\n\nThis vault is already open in another Kvit "
                     + "process, or on another computer sharing this folder, "
                     + "and only one can write to it at a time. Close it there "
                     + "and reopen, or keep working here on individual "
                     + "files.\n\n%2")
                    .arg(detail).arg(path))
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

    // ---- The menu bar on macOS -------------------------------------
    // A Mac application's menus belong in the system menu bar at the top of
    // the screen rather than in a strip inside the window, and Qt puts a
    // MenuBar there when its menus are native popups. The two menus are the
    // same components the toolbar's File and View buttons use, so the commands
    // cannot drift apart; whichever platform this is, exactly one of the two
    // homes instantiates them.
    //
    // The bar is built only on macOS and assigned rather than declared: an
    // ApplicationWindow lays out whatever its `menuBar` holds, so on the
    // platforms that do not want one the property stays empty instead of
    // holding something switched off, and no window grows a menu strip it did
    // not have before.
    //
    // macOS moves "Settings…" and the quit command into the application menu
    // by the text of the item, so the File menu needs no macOS-only entries.
    Component {
        id: macMenuBarComponent
        MenuBar {
            objectName: "macMenuBar"
            FileMenu {
                appWindow: root
                popupType: Popup.Native
            }
            ViewMenu {
                appWindow: root
                popupType: Popup.Native
            }
        }
    }
    function installNativeMenuBar() {
        if (Qt.platform.os !== "osx" || root.menuBar)
            return
        root.menuBar = macMenuBarComponent.createObject(root)
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

        // External-drag ingestion (§5.4), in EditorDropArea.qml.
        EditorDropArea {
            id: editorDropArea
            objectName: "editorDropArea"
            anchors.fill: scrollView
            z: 40
            appWindow: root
            listView: blockListView
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

            // A Flickable does not clip unless told to, and rows scrolled just
            // past the top of the viewport stay instantiated (cacheBuffer
            // below), so they painted over the tag strip and on up over the
            // toolbar — the editor pane is declared after the toolbar, so it
            // wins the overlap. Every other scrolling surface in the app
            // already clips; the popups a block raises are window-level items,
            // so none of them are clipped by this.
            clip: true

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

                // Scrollable space past the last block, so the end of a note
                // can be pulled up into the middle of the window instead of
                // being pinned to its bottom edge. The last block was where
                // the reader had the least room to work: a task-board card
                // clicked there grows a description field below the window's
                // edge, and with the document ending exactly at that edge
                // there was nothing to scroll to. It is a scroll range, not a
                // row and not content height, so what the seam cursor and the
                // block list measure themselves against is unchanged.
                bottomMargin: Math.max(120, Math.round(height * 0.35))

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

                // Delegate readiness completes pending focus without polling.
                // Geometry-driven reveal/focus tracking yields immediately to
                // the reader as soon as they start moving the view themselves.
                onCurrentItemChanged: root.blockDelegateReady(currentItem)
                onMovementStarted: root.cancelGeometryTrackingForMovement()
                onContentHeightChanged: root.scheduleReveal()

                // One delegate per block type; paragraphs and headings
                // share the default text choice.
                // The chooser watches delegateKind, not blockType: it
                // recreates a row's delegate whenever the watched role
                // changes, and heading conversions must not drop focus.
                delegate: DelegateChooser {
                    id: blockDelegateChooser
                    role: "delegateKind"

                    // One DelegateChoice per registered kind, built when the
                    // block list is created. A kind reaches the screen by
                    // being registered — the same rule for a built-in kind
                    // and for one a linked module added.
                    //
                    // This was seventeen DelegateChoice blocks written out
                    // here, each pairing a kind number with a QML file, and
                    // nothing checked that a kind had one: a kind whose
                    // choice nobody remembered to add drew an empty row and
                    // said nothing about it.
                    //
                    // The order matters. DelegateChooser takes the FIRST
                    // choice whose roleValue matches, and a choice with no
                    // roleValue matches everything — so every choice built
                    // here names its kind, and none can shadow another.
                    Component.onCompleted: {
                        var choices = BlockKindRegistry.delegateChoices()
                        for (var i = 0; i < choices.length; ++i) {
                            var entry = choices[i]
                            // Parented to the chooser, which is what keeps it
                            // alive. A component created with no parent is
                            // owned by JavaScript, and once this loop's local
                            // goes out of scope the collector is free to take
                            // it — leaving the chooser holding a freed
                            // component and crashing on the next row it
                            // builds, which is a scroll or two later.
                            var component = Qt.createComponent(
                                entry.delegateUrl, Component.PreferSynchronous,
                                blockDelegateChooser)
                            if (component.status !== Component.Ready) {
                                console.warn("block kind '" + entry.id
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
                }

                // Do not animate displaced rows. Delegates such as Mermaid
                // diagrams acquire their final height asynchronously, after
                // insertion. A displaced y-transition keeps the following
                // rows at positions calculated from the delegate's temporary
                // height and can leave them overlapped after the animation.
                // Let ListView track changing delegate heights directly.

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
        onCreateVaultRequested: root.documentDialogs().offerVaultFromCurrentFolder()
    }
}
