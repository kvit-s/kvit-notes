// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// The open note: which one it is, and every way something moves to another.
//
// Switching notes is a transaction, not an assignment. Opening the next note
// replaces the block model, which is the only copy of the current one's
// unsaved content and undo history, so the departing note is flushed and
// saved first and the switch is abandoned if that save does not succeed. The
// find bar closes, selections clear, the scroll position is recorded for the
// back button, and the session word baseline resets. Creating a note, opening
// one from a template, following a wiki-link, restoring a recovered or
// externally-changed note and clicking a search result all end in that same
// transaction, which is why they are one component rather than a dozen
// functions on a window.
//
// Everything a part of the application needs in order to open, save or
// navigate a note is on this object, and a part takes one of these rather
// than taking the window. That is the whole point of the type: before it, all
// of this was declared on qml/main.qml, so the note list, the backlinks pane,
// the search results, the tray and the menus each held an ApplicationWindow
// in order to reach it, and nothing that was not main.qml could open a note
// at all. qml/QuickCaptureWindow.qml is the standing evidence — it writes
// through NoteCollection.captureNote() directly, because none of this was
// reachable from a second window.
//
// **Nothing here may read the window's screen.** Not focusMode, not
// sidebarView, not panelsVisible, not a pane width or a collapse flag. A
// session that needed any of those would be a session only the full editor
// screen could have, which is the arrangement this type exists to end. The
// four functions under "What the host supplies" are the exception that proves
// it: each is a dialog or a picker, a dialog is a popup, and a popup belongs
// to a window — so the session declares what it wants to ask and the host
// answers, exactly as BlockEditorSurface does for the completion menus.
//
// main.qml declares one of these and forwards its own public names to it, so
// the window's vocabulary is unchanged for the panes, menus, dialogs and the
// test suites that already use it.
QtObject {
    id: session

    // ---- What this session works with -----------------------------------
    //
    // Each defaults to "there is nothing here", so a session nobody wires up
    // still opens, saves and navigates notes; what it loses is the scroll
    // position that back/forward restore, the find bar that a search result
    // hands off to, and the recent-search history that result is added to.

    // The editor the session opens documents into.
    property BlockEditorSurface editor: null
    // The block list inside that editor. A note switch reads its scroll
    // offset for the history entry and puts the new note back at the top.
    property var listView: null
    property var findBar: null
    // The sidebar owns the recent-search history that opening a search result
    // adds to. Named apart from its id so the wiring in main.qml cannot
    // resolve to this property instead.
    property var sidebarPanel: null
    // The plan-then-apply rename sequence (NoteRenameWorkflow.qml), which the
    // note list and the folder tree ask this session for. Untyped, because
    // that file names this type for the open note's path and two QML
    // documents naming each other is a cycle.
    property var renameWorkflow: null

    // ---- What the host supplies -----------------------------------------
    //
    // Four things this session cannot do for itself because each of them puts
    // a window on screen. The default bodies are what a host with no dialogs
    // reports: nothing is asked, and the session carries on with the answer
    // it would have had if the reader had dismissed the question.

    // A failure to put in front of the reader — a save that could not be
    // written, a collection operation that was refused.
    function showDocumentError(message) {}

    // Asked before starting something that can fail. DocumentManager reports
    // a failed save through a signal the host's error dialog listens for, and
    // a dialog the host builds lazily hears nothing until it exists, so the
    // first save of a session could fail silently without this.
    function prepareErrorReporting() {}

    // The recovered note is the open one and the buffer holds edits the
    // journal does not, so the choice is the reader's: replace, keep, cancel.
    // The host's dialog calls back into replaceEditsWithRecovery() or
    // keepEditsOverRecovery() below.
    function confirmRecoveryOverwrite(relPath) {}

    // The native folder picker behind "Open Folder…" and "Open Folder in New
    // Window…". A native picker is parented to a window, so the host owns it.
    function openFolderFromDialog(inNewWindow) {}

    // ---- Which note is open ----------------------------------------------

    // Collection mode: a vault is open, so there are notes to move between.
    // Without one this is a single-file editor, which is what the application
    // has always been when given a file argument.
    readonly property bool collectionOpen: NoteCollection && NoteCollection.isOpen

    // relPath of the open note ("" outside collection mode).
    readonly property string currentNoteRelPath:
        collectionOpen && DocumentManager.hasFile
            ? NoteCollection.relativePath(DocumentManager.currentFilePath) : ""

    // The folder holding the open file. Two workflows ask for it: a drop
    // ingests its assets beside the note, and the create-a-vault offer turns
    // this folder into the collection root.
    function currentNoteDir() {
        var p = DocumentManager.currentFilePath
        var idx = p.lastIndexOf("/")
        return idx >= 0 ? p.substring(0, idx) : ""
    }

    // Switch notes: save-on-blur, load, undo clears (the existing open()
    // contract), search and selections reset.
    function openNoteByPath(relPath) {
        if (!session.collectionOpen || relPath === "")
            return false
        var abs = NoteCollection.absolutePath(relPath)
        if (DocumentManager.currentFilePath === abs)
            return true
        DocumentManager.flushPendingEdits()
        // The departing note's scroll position, captured before the
        // switch so back/forward return the reader to it (§3.3). A
        // history-driven reopen is a no-op inside visit().
        var departingY = session.listView ? session.listView.contentY : 0
        // Opening the next note replaces the model, which is the only copy of
        // the current one's unsaved content and undo history. If the save did
        // not succeed - unwritable file, full disk - going ahead destroys work
        // the user never agreed to lose, so stay put and let the error stand.
        if (DocumentManager.isDirty && DocumentManager.hasFile) {
            if (!DocumentManager.save())
                return false
        }
        if (session.findBar && session.findBar.visible)
            session.findBar.close()
        if (DocumentSelection.hasBlockSelection
            || DocumentSelection.hasTextSelection)
            DocumentSelection.clear()
        if (!DocumentManager.open(DocumentManager.toLocalFileUrl(abs)))
            return false
        NavigationHistory.visit(relPath, departingY)
        NoteCollection.setLastOpenNote(relPath)
        if (session.editor)
            session.editor.lastFocusedBlock = 0
        if (session.listView)
            session.listView.currentIndex = 0
        // Reset the session word tracker to the just-loaded document (the model
        // has finished loading synchronously here).
        Qt.callLater(session.refreshSessionBaseline)
        return true
    }

    // Back/forward over the note history; scroll positions restore after
    // the (synchronous) model load settles.
    function navigateBack() {
        var entry = NavigationHistory.goBack(
            session.listView ? session.listView.contentY : 0)
        if (entry.ok)
            session.openHistoryEntry(entry)
    }

    function navigateForward() {
        var entry = NavigationHistory.goForward(
            session.listView ? session.listView.contentY : 0)
        if (entry.ok)
            session.openHistoryEntry(entry)
    }

    function openHistoryEntry(entry) {
        // goBack()/goForward() have already moved the stacks. If the open then
        // fails the editor is still showing the note the history says we left,
        // so the move has to be undone or Back and Forward keep pointing at
        // the wrong end of a document that never changed. A successful open
        // commits it through visit().
        if (!session.openNoteByPath(entry.relPath)) {
            NavigationHistory.rollbackNavigation()
            return
        }
        Qt.callLater(function() {
            if (!session.listView)
                return
            var maxY = Math.max(0, session.listView.contentHeight
                                   - session.listView.height)
            session.listView.contentY = Math.min(entry.position, maxY)
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
                session.scrollToHeadingText(heading)
            return
        }
        if (!session.collectionOpen) {
            session.showTransientStatus(
                qsTr("Wiki-links need an open collection"))
            return
        }
        var resolution = NoteCollection.wikiTargetResolution(target)
        if (resolution.status === "ambiguous") {
            session.showTransientStatus(
                qsTr("Ambiguous link “%1”: %2")
                    .arg(target)
                    .arg(resolution.candidates.join(", ")))
            return
        }
        var relPath = resolution.relPath
        if (resolution.status === "missing") {
            relPath = session.createWikiTarget(target)
            if (relPath === "")
                return
            session.showTransientStatus(
                qsTr("Created “%1”").arg(relPath))
        }
        if (!session.openNoteByPath(relPath))
            return
        if (heading !== "") {
            Qt.callLater(function() {
                DocumentOutline.rebuildNow()
                session.scrollToHeadingText(heading)
            })
        }
    }

    // A wiki-link's #heading part is raw heading text; slug it through
    // the shared slug function before outline lookup.
    function scrollToHeadingText(heading) {
        var idx = DocumentOutline.blockIndexForSlug(
            DocumentOutline.slugForText(heading))
        if (idx >= 0) {
            if (session.editor)
                session.editor.scrollToBlock(idx)
        } else {
            session.showTransientStatus(
                qsTr("No heading “%1”").arg(heading))
        }
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
            folder = session.currentNoteRelPath.indexOf("/") >= 0
                ? session.currentNoteRelPath.substring(
                      0, session.currentNoteRelPath.lastIndexOf("/"))
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
        if (!session.openNoteByPath(relPath))
            return
        if (session.sidebarPanel)
            session.sidebarPanel.commitRecentSearch(CollectionSearch.query)
        if (session.findBar)
            session.findBar.openAt(CollectionSearch.query, blockIndex, mdPos)
    }

    // Ctrl+N in collection mode: a new note in the current folder scope
    // (§13.4 New Note), opened immediately.
    function createNoteInCurrentScope() {
        if (!session.collectionOpen)
            return
        var folder = NoteListModel.scope === "folder"
            ? NoteListModel.folderPath : ""
        var relPath = NoteCollection.createNote(folder, "")
        if (relPath !== "") {
            session.openNoteByPath(relPath)
            session.focusFirstBlock()
        }
    }

    // features.md §18 create a note from a template: a new note in
    // the current scope, titled by the template, with the template's expanded
    // body loaded and its front-matter (tags, favorite) carried through.
    function createFromTemplate(templateName) {
        if (!session.collectionOpen)
            return ""
        var folder = NoteListModel.scope === "folder"
            ? NoteListModel.folderPath : ""
        var relPath = NoteCollection.createNote(folder, templateName)
        if (relPath === "")
            return ""
        var title = NoteCollection.noteInfo(relPath).title
        var inst = NoteTemplates.instantiate(templateName, title)
        if (!session.openNoteByPath(relPath))
            return relPath
        // The note is open and empty; put the expanded body in as one undoable
        // edit rather than a bare model reset, so a single Ctrl+Z takes the
        // template back off. Then apply the metadata and save through the
        // normal path.
        DocumentManager.restoreBody(inst.body || "")
        var tags = inst.tags || []
        for (var i = 0; i < tags.length; i++)
            NoteCollection.addTag(relPath, tags[i])
        if (inst.favorite === true)
            NoteCollection.setFavorite(relPath, true)
        // The body exists only in the model until this succeeds. Swallowing a
        // failed write left the note empty on disk and the next switch away
        // discarded it, so say so and let the caller decide.
        if (!DocumentManager.save()) {
            session.showDocumentError(
                qsTr("The note was created but its template content could not "
                     + "be saved."))
            return relPath
        }
        Qt.callLater(session.focusFirstBlock)
        return relPath
    }

    // Put the caret at the start of the new note's first block. Creating a
    // note calls this straight away, because opening the empty note builds
    // that row synchronously; the template path reaches it through
    // Qt.callLater, because it has just replaced the body and the row it wants
    // is the one that replacement is still building.
    function focusFirstBlock() {
        if (!session.listView)
            return
        var item = (session.listView.itemAtIndex(0) as BlockDelegateBase)
        if (item && item.focusAtStart)
            item.focusAtStart()
    }

    // "Save current note as template": copy the open note (front-matter and
    // body) into .kvit/templates under the given name.
    function saveCurrentNoteAsTemplate(name) {
        if (!session.collectionOpen || session.currentNoteRelPath === "")
            return false
        DocumentManager.flushPendingEdits()
        // The on-disk note text (front-matter + serialized body) is the
        // template; save first so the file reflects the current buffer.
        if (DocumentManager.isDirty)
            DocumentManager.save()
        var fm = NoteCollection.frontMatterFor(session.currentNoteRelPath)
        var full = (fm ? fm : "") + DocumentSerializer.serialize(BlockModel)
        return NoteTemplates.writeTemplate(name, full)
    }

    // ---- Saving, and opening a file that is not in the collection --------

    // One route for the File menu and Ctrl+S. Asking for the error path first
    // also wires save failures to the host's error dialog; without it, a first
    // save attempt could fail before that lazy component existed.
    function saveCurrentDocument(forceSaveAs) {
        session.prepareErrorReporting()
        if (forceSaveAs || !DocumentManager.hasFile)
            return DocumentManager.saveFileDialog()
        return DocumentManager.saveAsync()
    }

    // Open a file chosen from the native picker, routed by this session's mode
    // (multi-vault.md §): a vault session opens the file in its own single-file
    // window (raising an existing one if that file is already open), so the
    // vault it is showing is left intact; a single-file session replaces its
    // document in place, the historical behavior. The picker is shown first so
    // the path can be routed rather than opened blindly.
    function openFileFromDialog() {
        var path = DocumentManager.chooseFileToOpen()
        if (path === "")
            return
        if (session.collectionOpen)
            AppActions.requestOpenFileInNewWindow(path)
        else
            DocumentManager.open(DocumentManager.toLocalFileUrl(path))
    }

    // ---- Renaming a note, and the links that point at it ----------------
    // NoteRenameWorkflow.qml owns the plan-then-apply sequence and its two
    // dialogs. The note list and the folder tree ask this session to rename,
    // which is why the three names are here rather than on that component.

    function requestNoteRename(relPath, newTitle) {
        if (session.renameWorkflow)
            session.renameWorkflow.requestNoteRename(relPath, newTitle)
    }
    function requestNoteMove(relPath, targetFolder) {
        if (session.renameWorkflow)
            session.renameWorkflow.requestNoteMove(relPath, targetFolder)
    }
    function requestFolderRename(relPath, newName, afterApply) {
        if (session.renameWorkflow)
            session.renameWorkflow.requestFolderRename(relPath, newName,
                                                       afterApply)
    }

    // ---- Crash recovery --------------------------------------------------

    // Restore a crash-recovered note (the banner's Restore button): the
    // journal content lands on disk; a currently-open note reloads.
    //
    // The banner can be on screen for the note the user is editing right now,
    // and the journal snapshot behind it is up to one debounce interval old.
    // So restoring is not always a one-sided choice: the buffer may hold
    // newer work than the journal, and going ahead would overwrite the file
    // and replace the model — losing both the edits and the undo history
    // that could get them back. Flushing first makes isDirty describe the
    // buffer as it is now rather than as it was when the last keystroke was
    // debounced; a dirty buffer then turns the button into a question.
    function restoreRecoveredNote(relPath) {
        DocumentManager.flushPendingEdits()
        if (session.currentNoteRelPath === relPath && DocumentManager.isDirty) {
            session.confirmRecoveryOverwrite(relPath)
            return false
        }
        return session.applyRecoveredNote(relPath)
    }

    // Write the journal over the file and reload. The pre-overwrite copy goes
    // through the collection's own backup rotation, so the version being
    // replaced stays reachable from the backup dialog if the recovered text
    // turns out to be the wrong one.
    function applyRecoveredNote(relPath) {
        var abs = NoteCollection.absolutePath(relPath)
        NoteCollection.backupBeforeOverwrite(abs)
        if (!NoteCollection.restoreRecovery(relPath))
            return false
        if (session.currentNoteRelPath === relPath) {
            DocumentManager.open(DocumentManager.toLocalFileUrl(abs))
        } else {
            session.openNoteByPath(relPath)
        }
        return true
    }

    // "Keep my edits": the buffer is the version worth having, so the journal
    // is what goes. The banner entry disappears with it.
    function keepEditsOverRecovery(relPath) {
        NoteCollection.discardRecovery(relPath)
    }

    // "Use the recovered version" answered over unsaved edits. Reopening the
    // note would throw the model away, and with it the undo history that is
    // the only remaining copy of those edits, so the recovered text is
    // applied to the live document as one undoable edit instead: the file
    // holds the recovered version, and one Ctrl+Z brings the replaced work
    // back. Saving the buffer first is not an option — a clean save resolves
    // the journal, so there would be nothing left to restore.
    function replaceEditsWithRecovery(relPath) {
        var abs = NoteCollection.absolutePath(relPath)
        NoteCollection.backupBeforeOverwrite(abs)
        if (!NoteCollection.restoreRecovery(relPath))
            return false
        return DocumentManager.restoreBody(
            NoteCollection.noteInfo(relPath).body)
    }

    // ---- The open note changed on disk (features.md §12.1) ---------------
    // While the buffer is dirty both versions are real work, so the host
    // raises its conflict banner over these two flags and the two functions
    // below are its answers; when the buffer is clean, taking the disk
    // version loses nothing and happens silently.
    property bool externalConflict: false
    property string conflictPath: ""

    function keepMine() {
        // Re-write the editor's content, overwriting the external change.
        // The banner is the only surface that offers this decision, and the
        // watcher will not necessarily raise the same version again, so it
        // stays up until the write actually succeeded.
        if (!DocumentManager.save())
            return false
        session.externalConflict = false
        return true
    }

    function loadTheirs(absPath) {
        var target = absPath !== undefined ? absPath : session.conflictPath
        // Force a reload past openNoteByPath's same-path short-circuit. A
        // failed open leaves the editor on the version it already had, which
        // is still in conflict, so the banner stays for a second attempt.
        if (!DocumentManager.open(DocumentManager.toLocalFileUrl(target)))
            return false
        if (session.editor)
            session.editor.lastFocusedBlock = 0
        if (session.listView)
            session.listView.currentIndex = 0
        Qt.callLater(session.refreshSessionBaseline)
        session.externalConflict = false
        return true
    }

    // Two objects report the same event — the filesystem watcher, which sees
    // the write, and the collection, which reports it after re-indexing — so
    // both are routed through this one function. It is idempotent: a second
    // report for a note whose banner is already up changes nothing, rather
    // than announcing the conflict twice.
    function noteChangedOnDisk(absPath) {
        if (absPath === "" || absPath !== DocumentManager.currentFilePath)
            return   // not the open note — the tree re-scan handles the rest
        if (session.externalConflict && session.conflictPath === absPath)
            return   // already asked, and the answer is still outstanding
        DocumentManager.flushPendingEdits()
        if (DocumentManager.isDirty) {
            session.conflictPath = absPath
            session.externalConflict = true
            A11y.announce(qsTr("This note changed on disk"))
        } else {
            // Not dirty here: loading theirs is lossless, so do it silently.
            session.loadTheirs(absPath)
        }
    }

    // Held in properties because this object has no children of its own.
    property Connections externalChangeWatch: Connections {
        target: FileWatcher
        function onNoteChangedExternally(absPath) {
            session.noteChangedOnDisk(absPath)
        }
    }

    // The collection's own report of the same thing. Without this it reaches
    // the user only as a transient status line, which scrolls away — and a
    // decision about which version of their work to keep is not something to
    // put somewhere that disappears on its own.
    property Connections collectionChangeWatch: Connections {
        target: NoteCollection
        function onNoteChangedExternally(relPath) {
            session.noteChangedOnDisk(NoteCollection.absolutePath(relPath))
        }
    }

    // ---- A file too large to open ----------------------------------------
    // The guard refuses a file over the size cap before any read; the host's
    // placeholder names the file, its size and the cap, and offers the
    // informed-consent "Open anyway".
    property string oversizedFilePath: ""
    property real oversizedFileBytes: 0
    property real oversizedFileCap: 0

    // ---- What the session says while it works ----------------------------

    // A transient status note: shown briefly, e.g. when an internal link
    // resolves or dangles. Cleared by its timer.
    property string transientStatus: ""
    property Timer transientStatusTimer: Timer {
        interval: 3500
        onTriggered: session.transientStatus = ""
    }
    function showTransientStatus(msg) {
        session.transientStatus = msg
        session.transientStatusTimer.restart()
    }

    // features.md §19.2 session word-count tracker: the document's word count
    // when the note opened; the statistics popover shows the delta.
    // Ephemeral, reset per note.
    property int sessionStartWords: 0
    function refreshSessionBaseline() {
        session.sessionStartWords = BlockModel ? BlockModel.documentWordCount : 0
    }

    // ---- Following a link out of the note --------------------------------
    // Opens link targets (features.md §2.4). Routed through one object so
    // tests can observe activations without launching a browser. It is part
    // of the session rather than of the window because two of its three
    // branches are about this note: a [[wiki link]] resolves against the
    // collection and opens another note, and a #slug resolves against this
    // document's headings.
    property QtObject linkOpener: QtObject {
        id: opener
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
            session.showTransientStatus(
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
            opener.activated(target)
            // Wiki-link: kvit-note:target#heading resolves through the
            // collection and opens in-app — creating the note when the
            // target dangles — never a browser.
            if (target.indexOf("kvit-note:") === 0) {
                session.followWikiLink(target.substring(10))
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
                    if (session.editor)
                        session.editor.scrollToBlock(idx)
                } else {
                    session.showTransientStatus(
                        qsTr("No heading “") + slug + qsTr("”"))
                }
                return
            }
            opener.lastExternalTarget = target
            if (!opener.openExternally)
                return
            // Through UrlLauncher rather than Qt.openUrlExternally, which on
            // Unix answers true whether or not anything opened (see
            // urllauncher.h). The verdict arrives as a signal, because
            // establishing it means running an opener and watching it.
            UrlLauncher.open(target)
        }
    }

    function openLink(url) {
        opener.activate(url)
        return true
    }

    property Connections launcherWatch: Connections {
        target: UrlLauncher
        function onFailed(url) { opener.reportNoBrowser(url) }
        // A scheme this application does not hand to the desktop at all. Said
        // plainly, because from the reader's side it is the same click that
        // did nothing, and the reason is different.
        function onRefused(url) {
            session.showTransientStatus(
                qsTr("This kind of link is not opened: ") + url)
        }
    }
}
