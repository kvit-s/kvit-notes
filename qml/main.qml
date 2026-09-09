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
import Kvit 1.0

ApplicationWindow {
    id: root

    // First-run default; every later launch restores the persisted
    // geometry below. 800x600 clipped the toolbar's right end.
    width: Math.min(1100, Screen.desktopAvailableWidth > 0
                    ? Screen.desktopAvailableWidth - 40 : 1100)
    height: Math.min(720, Screen.desktopAvailableHeight > 0
                     ? Screen.desktopAvailableHeight - 60 : 720)
    visible: true
    title: {
        var name = root.contentView === "text"
            ? TextFileViewModel.path.split("/").pop()
            : root.contentView === "media"
              ? standaloneFilePane.requestedPath.split("/").pop()
              : DocumentManager ? DocumentManager.currentFileName : "Kvit Notes"
        // Collection mode: the note title is the file name without ".md".
        if (currentNoteRelPath !== "" && name.toLowerCase().endsWith(".md"))
            name = name.substring(0, name.length - 3)
        return (DocumentManager && DocumentManager.isDirty ? "* " : "")
            + name + " - Kvit Notes"
    }

    // ---- The editor, under the names the rest of the window uses --------
    // Everything below forwards to the BlockEditor declared in the document
    // pane. The panes, menus, dialogs and the session all reached these on the
    // window before the editor was a component of its own, and they are the
    // window's own vocabulary rather than the editor's, so they stay here.
    //
    // The block that most recently held editing focus (§3.1's "current
    // block"): the Shift+Click block-range anchor. Maintained by the
    // delegates on focus gain — not listView.currentIndex, whose
    // assignment moves focus into the delegate root.
    property alias lastFocusedBlock: blockEditor.lastFocusedBlock
    readonly property alias caretBlockIndex: blockEditor.caretBlockIndex
    readonly property alias blockDrag: blockEditor.blockDrag
    readonly property alias findBar: blockEditor.findBar
    readonly property alias selectionKeyHandler: blockEditor.selectionKeys
    readonly property alias blockGapCursor: blockEditor.gapCursor
    // Popups that hold the caret's text selection while they are open. The
    // toolbar's colour picker is outside the editor and raises the same count.
    property alias selectionHolders: blockEditor.selectionHolders

    function focusBlockAtIndex(index, atEnd, typed) {
        blockEditor.focusBlockAtIndex(index, atEnd, typed)
    }
    function focusEditor() { blockEditor.focusEditor() }
    function scrollToBlock(idx) { blockEditor.scrollToBlock(idx) }
    function centerCaretLine(item) { blockEditor.centerCaretLine(item) }
    function revealItem(item) { blockEditor.revealItem(item) }
    function editorContentY() { return blockEditor.editorContentY() }
    function setEditorContentY(value) { blockEditor.setEditorContentY(value) }

    // ---- The notes collection -------------------------------------------
    // Collection mode shows the sidebar and note list; single-file mode
    // (file argument, or the test harness's unopened collection) keeps
    // the pre-Phase-8 editor-only geometry.
    readonly property bool collectionOpen: NoteCollection && NoteCollection.isOpen
    property bool panelsVisible: true
    property bool navigationRailsVisible: false
    // The note navigator remains the default. Files is an independent, lazy
    // projection over the same root; neither view mutates the other.
    property string sidebarView: "notes"
    // Notes, Folders, Tags and Search are four ways into the same collection:
    // each narrows the note list rather than replacing it. The list column
    // therefore belongs to all four. Showing it for "notes" alone left the
    // other three choosing a folder, a tag or a query whose result had
    // nowhere to appear — the search view in particular typed into a field
    // whose matches were drawn in a hidden pane.
    readonly property bool notesFamilyView:
        ["notes", "folders", "tags", "search"].indexOf(sidebarView) >= 0

    // A stored view id can name a sidebar this build does not have: a profile
    // written by a build with a module installed, or one left by a version
    // that had a view since removed. Restoring it verbatim leaves nothing
    // drawn at all — the notes family hides for any id outside it, the files
    // pane loads only for "files", and the module Loader resolves an unknown
    // id to an empty source — and the controls that would switch back are
    // inside the pane that just hid. Only restored values pass through here;
    // an assignment from code that knows the id is left alone.
    function knownSidebarView(id) {
        if (["notes", "folders", "tags", "search", "files"].indexOf(id) >= 0)
            return id
        if (id && Extensions.sidebarViewSource(id) !== "")
            return id
        return "notes"
    }
    // Exactly one document surface is active: the editable note, a read-only
    // source file, or the shared image/media viewer.
    property string contentView: "document"
    // Set only by WindowRegistry for an explicit root-rail Close. That action
    // means release the root even when ordinary title-bar closes are configured
    // to hide into the tray. It remains true across an unsaved-new-document
    // question and is cleared by either the accepted close or Cancel.
    property bool forceActualClose: false

    onSidebarViewChanged: {
        var rootPath = NoteCollection && NoteCollection.isOpen
            ? NoteCollection.rootPath : ""
        if (rootPath === "")
            return
        var views = AppSettings.value("sidebar.viewByRoot", {})
        views[rootPath] = sidebarView
        AppSettings.setValue("sidebar.viewByRoot", views)
    }
    onNavigationRailsVisibleChanged:
        AppSettings.setValue("view.navigationRails", navigationRailsVisible)

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
    property int bottomDockHeight: 220
    property bool bottomDockCollapsed: false
    // What every bottom-anchored region has to clear: the status bar plus an
    // extension bottom bar when a module fills that slot (zero otherwise).
    readonly property int bottomChromeHeight:
        (statusBar.visible ? statusBar.height : 0)
        + (extensionBottomBar.visible ? extensionBottomBar.height : 0)
        + (bottomDock.visible ? bottomDock.height : 0)
    onStatusBarVisibleChanged:
        AppSettings.setValue("view.statusBar", statusBarVisible)
    onBottomDockHeightChanged:
        AppSettings.setValue("dock.height", bottomDockHeight)
    onBottomDockCollapsedChanged:
        AppSettings.setValue("dock.collapsed", bottomDockCollapsed)

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
                if (blockEditor.caretBlock)
                    blockEditor.centerCaretLine(blockEditor.caretBlock)
            })
    }

    function openSettingsDialog() { settingsDialog.open() }

    // ---- Keyboard accessibility: focus and pane navigation (§14.1) ----
    // Skip-navigation lands on the current (or first) editor block, bypassing
    // the chrome; focusEditor() above forwards to the editor for it.
    //
    // Which major pane last took focus (0 sidebar, 1 note list, 2 editor,
    // 3 toolbar, 4 bottom dock, 5 navigation rails), so F6 can cycle to the next visible one — the standard
    // desktop region key. The toolbar is in the cycle because Insert,
    // Templates, View and the customization menu have no other shortcut, so
    // leaving it out left those actions with no keyboard route at all.
    property int focusedPane: 2
    function focusPane(p) {
        root.focusedPane = p
        if (p === 0 && !root.sidebarCollapsed && root.collectionOpen)
            sidebar.focusPane()
        else if (p === 1 && root.notesFamilyView
                 && !root.noteListCollapsed && root.collectionOpen)
            noteListPane.focusPane()
        else if (p === 3 && appToolbar.visible)
            appToolbar.focusPane()
        else if (p === 4 && bottomDock.visible)
            bottomDock.focusPane()
        else if (p === 5 && navigationRails.visible)
            navigationRails.focusPane()
        else
            focusEditor()
    }
    function cyclePane() {
        var order = []
        if (navigationRails.visible) order.push(5)
        if (root.collectionOpen && !root.sidebarCollapsed) order.push(0)
        if (root.collectionOpen && root.notesFamilyView
                && !root.noteListCollapsed) order.push(1)
        order.push(2)  // the editor is always present
        if (bottomDock.visible) order.push(4)
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
            if (DocumentManager.currentFilePath !== "")
                root.contentView = "document"
            Qt.callLater(root.refreshSessionBaseline)
        }

        function onIsDirtyChanged() {
            if (!DocumentManager.isDirty)
                A11y.announceSaveState(false)
        }
    }
    Connections {
        target: FileTreeModel
        function onFileActivated(absolutePath, kind, relativePath) {
            root.openFileTreeEntry(absolutePath, kind, relativePath)
        }
    }
    Connections {
        target: NoteCollection
        function onRootChanged() {
            if (!NoteCollection.isOpen)
                return
            var views = AppSettings.value("sidebar.viewByRoot", {})
            root.sidebarView =
                root.knownSidebarView(views[NoteCollection.rootPath])
            root.contentView = "document"
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
        function onVaultSwitchConfirmationRequested(path) {
            root.documentDialogs().confirmVaultSwitch(path)
        }
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
        function onSelectionFocusRequested() {
            blockEditor.selectionKeys.forceActiveFocus()
        }
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
        editor: blockEditor
        listView: blockEditor.listView
        findBar: root.findBar
        sidebarPanel: sidebar
    }

    // Drop ingestion lives in EditorDropArea now. Both of its entry points
    // keep their window-level names, which the delegates and the integration
    // suite call.
    function blockForPath(stored) {
        return blockEditor.dropArea.blockForPath(stored)
    }
    function insertBlocksAt(afterIndex, typedBlocks) {
        blockEditor.dropArea.insertBlocksAt(afterIndex, typedBlocks)
    }

    function openNoteByPath(relPath) {
        root.contentView = "document"
        return noteSession.openNoteByPath(relPath)
    }
    function openFileTreeEntry(absolutePath, kind, relativePath) {
        if (kind === "markdown") {
            root.contentView = "document"
            var info = NoteCollection.noteInfo(relativePath)
            if (info && info.relPath !== undefined)
                return noteSession.openNoteByPath(relativePath)
            return DocumentManager.open(
                DocumentManager.toLocalFileUrl(absolutePath))
        }
        if (kind === "text") {
            TextFileViewModel.open(absolutePath, 1)
            root.contentView = "text"
            return true
        }
        if (kind === "image" || kind === "media") {
            standaloneFilePane.openFile(absolutePath, kind)
            root.contentView = "media"
            return true
        }
        return UrlLauncher.open(
            DocumentManager.toLocalFileUrl(absolutePath).toString())
    }
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
        listView: blockEditor.listView
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
        editor: blockEditor
        onApplied: function(blockIndex, type, opensDialog) {
            Qt.callLater(function() {
                blockEditor.listView.currentIndex = blockIndex
                // An entry that opened an insert dialog (image, embed, table
                // size grid) leaves the keyboard to that dialog. Focusing the
                // block here would take it straight back, one tick after the
                // dialog appeared. The insert flow focuses the block itself
                // once it has what it asked for.
                if (opensDialog)
                    return
                var item = (blockEditor.listView.itemAtIndex(blockIndex)
                            as BlockDelegateBase)
                if (item)
                    item.focusAtStart()
            })
        }
    }

    // The multi-block floating proxy draws over the whole shell — the toolbar
    // and the side panes included — which is what the z value on it says. An
    // editor draws its own only when its host supplies none, and it could
    // then draw no further than the editor's own bounds. Single-block drags
    // use only the live-moving row.
    BlockDragLayer {
        id: blockDragLayer
        anchors.fill: parent
        z: 1000
        dragState: blockEditor.blockDrag
        listView: blockEditor.listView
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
        targetBlock: blockEditor.caretBlock
    }

    // §19.2 writing-goal dialog: set or clear the open note's word target.
    KvitDialog {
        id: goalDialog
        // Opens on the field it is about, so a screen reader
        // announces something to type into and a keyboard user
        // does not have to guess how many tabs reach it.
        initialFocusItem: goalField
        objectName: "goalDialog"
        modal: true
        title: qsTr("Writing goal")
        anchors.centerIn: parent
        width: Interface.px(300)
        standardButtons: Dialog.Ok | Dialog.Cancel
        property string relPath: ""
        function openFor(rel) {
            relPath = rel
            goalField.value = NoteCollection.goalFor(rel)
            open()
        }
        onAccepted: NoteCollection.setGoal(relPath, goalField.value)
        contentItem: ColumnLayout {
            spacing: Interface.px(8)
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
    // BlockEditorSurface query a delegate makes on itself, and the three open
    // calls arrive from AppActions, so both have to be reachable here.
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
            selectionKeys: blockEditor.selectionKeys
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
            if (item === blockEditor.listView
                    || item === blockEditor.selectionKeys)
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
                target = (blockEditor.listView.itemAtIndex(selectedIndex)
                          as BlockDelegateBase)
        } else {
            // The ListView's currentIndex is updated by editor operations,
            // but merely focusing a row does not promise to update it. Walk
            // up from the real focus item so this route cannot target a row
            // left current by an earlier mouse or drag operation.
            var item = root.activeFocusItem
            while (item && item !== blockEditor.listView) {
                target = (item as BlockDelegateBase)
                if (target)
                    break
                item = item.parent
            }
            if (!target && blockEditor.listView.currentIndex >= 0)
                target = (blockEditor.listView.itemAtIndex(
                              blockEditor.listView.currentIndex)
                          as BlockDelegateBase)
        }
        if (target)
            root.contextMenus().openHandleMenu(target, true)
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
    KvitDialog {
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
            implicitWidth: Interface.px(380)
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
                ? blockEditor.gapCursor.stripPastedFormatting(pendingText)
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
                    blockEditor.selectionKeys.selectRange(at, at + count - 1)
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
        if (root.forceActualClose
            || !(typeof SystemTray !== "undefined" && SystemTray.available
                 && SystemTray.closeToTray)) {
            AppActions.notifyWindowClosing()
            root.forceActualClose = false
        }
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
        editor: blockEditor
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
            spacing: Interface.px(10)
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("This note changed on disk. Keep your version or load the disk version?")
                color: Theme.bannerText
                font.pixelSize: Interface.strong
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: Interface.px(8)
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
            font.pixelSize: Interface.strong
        }
        Row {
            id: oversizedActions
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: Interface.px(8)
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

    NavigationRails {
        id: navigationRails
        appWindow: root
        visible: root.navigationRailsVisible && root.collectionOpen
                 && !root.focusMode
        width: visible ? railWidth * 2 : 0
        anchors.top: appToolbar.visible ? appToolbar.bottom : parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.bottomChromeHeight
    }

    Row {
        id: sidePanels
        objectName: "sidePanels"
        anchors.top: appToolbar.bottom
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.bottomChromeHeight
        visible: root.collectionOpen && root.panelsVisible && !root.focusMode
        x: navigationRails.width

        // Explicit sum (not implicitWidth): the editor pane's
        // leftMargin binds here, and a hidden Row must reserve nothing.
        readonly property int seamWidth: 6
        readonly property int stripWidth: 22
        width: visible
            ? (root.sidebarCollapsed
                   ? stripWidth : root.sidebarWidth + seamWidth)
              + (root.notesFamilyView
                 ? (root.noteListCollapsed
                        ? stripWidth : root.noteListWidth + seamWidth)
                 : 0)
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
                width: Interface.px(1)
                color: Theme.border
            }
            ToolButton {
                objectName: "sidebarExpandButton"
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                text: "»"
                Accessible.name: qsTr("Expand sidebar")
                font.pixelSize: Interface.body
                ToolTip.visible: hovered || visualFocus
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
            visible: root.notesFamilyView && root.noteListCollapsed
            width: visible ? sidePanels.stripWidth : 0
            height: parent.height
            color: Theme.listBackground
            Rectangle {
                anchors.right: parent.right
                height: parent.height
                width: Interface.px(1)
                color: Theme.border
            }
            ToolButton {
                objectName: "noteListExpandButton"
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                text: "»"
                Accessible.name: qsTr("Expand note list")
                font.pixelSize: Interface.body
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: qsTr("Expand note list")
                onClicked: root.noteListCollapsed = false
            }
        }
        NoteListPane {
            id: noteListPane
            visible: root.notesFamilyView && !root.noteListCollapsed
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
            visible: root.notesFamilyView && !root.noteListCollapsed
            width: visible ? sidePanels.seamWidth : 0
            height: parent.height
            minWidth: 180
            maxWidth: 520
            panelWidth: root.noteListWidth
            onResized: function(newWidth) { root.noteListWidth = newWidth }
        }
    }

    Rectangle {
        id: documentPane
        objectName: "documentPane"
        visible: root.contentView === "document"
        anchors.fill: parent
        anchors.leftMargin: sidePanels.width + navigationRails.width
        anchors.topMargin: appToolbar.visible ? appToolbar.height : 0
        anchors.bottomMargin: root.bottomChromeHeight
        anchors.rightMargin: (outlinePanel.visible ? root.outlineWidth : 0)
            + (backlinksPanel.visible ? root.backlinksWidth : 0)
            + extensionSidePanel.width
        color: root.backgroundColor

        // The editing surface (BlockEditor.qml): the block list, the gestures
        // over it, the floating bars, and the focus and scroll machinery that
        // make the list an editor. Declared first so the tag strip and the
        // document-header slot, which carry a z of their own, draw over it.
        BlockEditor {
            id: blockEditor
            objectName: "blockEditor"
            anchors.fill: parent

            // The chrome this window draws across the top of the editor: the
            // tag strip, and the document header a linked module fills. The
            // block column starts below both; the floating bars clear only
            // the strip, which is all that reaches the corner they sit in.
            contentTopMargin: (tagStrip.visible ? tagStrip.height + 16 : 20)
                              + extensionDocumentHeader.height
            overlayTopMargin: tagStrip.visible ? tagStrip.height + 12 : 8

            focusColumn: root.focusMode
            typewriterMode: root.typewriterMode
            dragLayer: blockDragLayer

            // Where this document lives, and the three things the editor asks
            // of the collection holding it. With no collection open every one
            // of them is empty or inert, which is the single-file mode this
            // application has always had: images are stored beside the file
            // and a [[wiki link]] styles as an ordinary link.
            documentPath: DocumentManager.currentFilePath
            assetRoot: NoteCollection.isOpen ? NoteCollection.rootPath : ""
            assetSink: AssetStore
            linkResolver: NoteCollection.isOpen ? NoteCollection : null

            // The completion menus are this window's own objects; a delegate
            // asks whether one is open for it and gets it back to drive.
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
            function contextMenuHoldsSelection(target) {
                return root.contextMenuHoldsSelection(target)
            }
            function openLink(url) { return root.openLink(url) }

            // An oversized paste is confirmed by this window's dialog, which
            // then performs the insert itself.
            onOversizedPasteRequested: function(text, insertAt, plain,
                                                stripFormatting, focusLast) {
                largePasteConfirmDialog.pendingText = text
                largePasteConfirmDialog.pendingIndex = insertAt
                largePasteConfirmDialog.pendingPlain = plain
                largePasteConfirmDialog.pendingStripFormatting = stripFormatting
                largePasteConfirmDialog.pendingFocusLast = focusLast
                largePasteConfirmDialog.open()
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
            Accessible.name: qsTr("Restore from backup")
            font.pixelSize: Interface.strong
            implicitWidth: Interface.px(26)
            implicitHeight: Interface.px(26)
            ToolTip.visible: hovered || visualFocus
            ToolTip.text: qsTr("Restore from backup…")
            onClicked: backupDialog.openForCurrentNote()
        }

        // The document-header slot: a strip across the top of the editor
        // column that a linked module fills, staying put while the document
        // scrolls under it. The three older slots sit around the editor; this
        // one is inside it, which is why it lives here rather than beside
        // extensionBanner. Empty and zero-height in the open build, and the
        // scroll area's top margin below reserves exactly its height.
        Loader {
            id: extensionDocumentHeader
            objectName: "extensionDocumentHeader"
            source: Extensions.slotSource("documentHeader")
            active: source != ""
            z: 6
            anchors.top: parent.top
            anchors.topMargin: tagStrip.visible ? tagStrip.height + 8 : 0
            anchors.left: parent.left
            anchors.right: parent.right
            height: active && item ? (item as Item).implicitHeight : 0
        }
    }

    Loader {
        id: textFilePane
        anchors.fill: parent
        anchors.leftMargin: sidePanels.width + navigationRails.width
        anchors.topMargin: appToolbar.visible ? appToolbar.height : 0
        anchors.bottomMargin: root.bottomChromeHeight
        anchors.rightMargin: extensionSidePanel.width
        active: root.contentView === "text"
        visible: active
        sourceComponent: ReadOnlyTextFile { }
    }

    Loader {
        id: standaloneFilePane
        property string requestedPath: ""
        property string requestedKind: ""
        function openFile(path, kind) {
            requestedPath = path
            requestedKind = kind
            if (item)
                (item as StandaloneFileView).openFile(path, kind)
        }
        onLoaded: (item as StandaloneFileView).openFile(
                      requestedPath, requestedKind)
        anchors.fill: parent
        anchors.leftMargin: sidePanels.width + navigationRails.width
        anchors.topMargin: appToolbar.visible ? appToolbar.height : 0
        anchors.bottomMargin: root.bottomChromeHeight
        anchors.rightMargin: extensionSidePanel.width
        active: root.contentView === "media"
        visible: active
        sourceComponent: StandaloneFileView { }
    }

    // features.md §17.1 document outline dock: a right-side pane over the
    // editor, toggled from the view menu. Placed after the editor Rectangle so
    // it sits above it; the editor's right margin reserves its width.
    OutlinePanel {
        id: outlinePanel
        objectName: "outlinePanel"
        appWindow: root
        visible: root.contentView === "document"
                 && root.outlineVisible && !root.focusMode
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
        visible: root.contentView === "document"
                 && root.backlinksVisible && root.collectionOpen
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

    BottomDock {
        id: bottomDock
        appWindow: root
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: extensionBottomBar.visible
            ? extensionBottomBar.top
            : statusBar.visible ? statusBar.top : parent.bottom
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
        listView: blockEditor.listView
        targetBlock: blockEditor.caretBlock
        statisticsPanel: root.statisticsPanel

        onWritingGoalRequested: goalDialog.openFor(root.currentNoteRelPath)
        onCreateVaultRequested: root.documentDialogs().offerVaultFromCurrentFolder()
    }
}
