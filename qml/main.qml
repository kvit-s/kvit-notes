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
        if (visibility === Window.Maximized)
            AppSettings.setValue("window.maximized", true)
        else if (visibility === Window.Windowed)
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
    // workflow — Ctrl+O and Ctrl+N, Escape during a drag, quick capture —
    // stay with the component that answers them.
    AppShortcuts {
        anchors.fill: parent
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
        noteSession.restoreRecoveredNote(relPath)
    }
    // The conflict banner's two buttons (§12.1).
    function keepMine() { noteSession.keepMine() }
    function loadTheirs(absPath) { noteSession.loadTheirs(absPath) }

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
        onApplied: function(blockIndex, type) {
            Qt.callLater(function() {
                blockListView.currentIndex = blockIndex
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

    crossBlockDrag: CrossBlockTextSelection {
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

    // The floating proxy draws over the whole shell, which is what the z
    // value on the proxy used to say from here.
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

        // An oversized paste is confirmed by the window's dialog, which then
        // performs the insert itself.
        onOversizedPasteRequested: function(text, insertAt, plain) {
            largePasteConfirmDialog.pendingText = text
            largePasteConfirmDialog.pendingIndex = insertAt
            largePasteConfirmDialog.pendingPlain = plain
            largePasteConfirmDialog.open()
        }
    }

    // ---- Opening, starting and closing a document -----------------------
    // The transitions that can lose work, and the dialogs that ask before
    // they do, are in DocumentSessionDialogs.qml. The error dialog lives
    // there too, because a failed save or open is the same conversation.
    DocumentSessionDialogs {
        id: documentDialogs
        // Its dialogs centre on this window, so it spans it.
        anchors.fill: parent
        appWindow: root

        onImportRequested: importDialog.openDialog()
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
    EditorContextMenus {
        id: contextMenus
        anchors.fill: parent
        toolbar: appToolbar
        selectionKeys: selectionKeyHandler
    }

    function contextMenuHoldsSelection(target) {
        return contextMenus.holdsSelection(target)
    }
    function openTextContextMenu(target) {
        contextMenus.openTextMenu(target)
    }
    function openLinkContextMenu(target) {
        contextMenus.openLinkMenu(target)
    }
    function openBlockHandleMenu(target) {
        contextMenus.openHandleMenu(target)
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
        lightbox.open(source, alt)
    }

    // Putting an image, a web embed or a table into an empty block: the
    // dialogs and the conversion they perform are in BlockInsertDialogs.qml.
    // A delegate asks for these through AppActions, so the window keeps the
    // three names.
    BlockInsertDialogs {
        id: blockInserts
        anchors.fill: parent
        listView: blockListView
    }

    function insertImageIntoBlock(idx) { blockInserts.insertImage(idx) }
    function insertEmbedIntoBlock(idx) { blockInserts.insertEmbed(idx) }
    function insertTableIntoBlock(idx) { blockInserts.insertTable(idx) }

    // ---- External drop ingestion (features.md §5.3, §5.4) ----
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
        documentDialogs.confirmCloseUnsaved()
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
            documentDialogs.showError(message)
        }
        // The vault is open in another Kvit process. Only one session may
        // write a vault: both would load the same state and the second to
        // save would discard the first's work. This window keeps running as
        // a plain editor, so File > Open still works on individual notes.
        function onVaultInUse(path, detail) {
            documentDialogs.showError(
                qsTr("%1\n\nOnly one Kvit window can have a vault open, "
                     + "because two would overwrite each other's changes. "
                     + "Close the other window and reopen this folder, or "
                     + "keep working here on single files.\n\n%2")
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
        onCreateVaultRequested: documentDialogs.offerVaultFromCurrentFolder()
    }
}
