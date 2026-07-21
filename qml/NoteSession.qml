// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// The open note: which one it is, and every way the window moves to another.
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
// functions on the window.
//
// main.qml forwards its public names here; nothing outside calls this object
// directly.
QtObject {
    id: session

    // Wired by main.qml. The window supplies the state that is genuinely its
    // own — which note is open, the last focused block, the transient status
    // line — and the three objects a switch has to touch.
    property var appWindow
    property var listView
    property var findBar
    // The sidebar owns the recent-search history that opening a search result
    // adds to. Named apart from its id so the wiring in main.qml cannot
    // resolve to this property instead.
    property var sidebarPanel

    // Switch notes: save-on-blur, load, undo clears (the existing open()
    // contract), search and selections reset.
    function openNoteByPath(relPath) {
        if (!session.appWindow.collectionOpen || relPath === "")
            return false
        var abs = NoteCollection.absolutePath(relPath)
        if (DocumentManager.currentFilePath === abs)
            return true
        DocumentManager.flushPendingEdits()
        // The departing note's scroll position, captured before the
        // switch so back/forward return the reader to it (§3.3). A
        // history-driven reopen is a no-op inside visit().
        var departingY = session.listView.contentY
        // Opening the next note replaces the model, which is the only copy of
        // the current one's unsaved content and undo history. If the save did
        // not succeed - unwritable file, full disk - going ahead destroys work
        // the user never agreed to lose, so stay put and let the error stand.
        if (DocumentManager.isDirty && DocumentManager.hasFile) {
            if (!DocumentManager.save())
                return false
        }
        if (session.findBar.visible)
            session.findBar.close()
        if (DocumentSelection.hasBlockSelection
            || DocumentSelection.hasTextSelection)
            DocumentSelection.clear()
        if (!DocumentManager.open(DocumentManager.toLocalFileUrl(abs)))
            return false
        NavigationHistory.visit(relPath, departingY)
        NoteCollection.setLastOpenNote(relPath)
        session.appWindow.lastFocusedBlock = 0
        session.listView.currentIndex = 0
        // Reset the session word tracker to the just-loaded document (the model
        // has finished loading synchronously here).
        Qt.callLater(session.appWindow.refreshSessionBaseline)
        return true
    }

    // Back/forward over the note history; scroll positions restore after
    // the (synchronous) model load settles.
    function navigateBack() {
        var entry = NavigationHistory.goBack(session.listView.contentY)
        if (entry.ok)
            session.openHistoryEntry(entry)
    }

    function navigateForward() {
        var entry = NavigationHistory.goForward(session.listView.contentY)
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
        if (!session.appWindow.collectionOpen) {
            session.appWindow.showTransientStatus(
                qsTr("Wiki-links need an open collection"))
            return
        }
        var resolution = NoteCollection.wikiTargetResolution(target)
        if (resolution.status === "ambiguous") {
            session.appWindow.showTransientStatus(
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
            session.appWindow.showTransientStatus(
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
        if (idx >= 0)
            session.appWindow.scrollToBlock(idx)
        else
            session.appWindow.showTransientStatus(
                qsTr("No heading “%1”").arg(heading))
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
            folder = session.appWindow.currentNoteRelPath.indexOf("/") >= 0
                ? session.appWindow.currentNoteRelPath.substring(
                      0, session.appWindow.currentNoteRelPath.lastIndexOf("/"))
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
        session.sidebarPanel.commitRecentSearch(CollectionSearch.query)
        session.findBar.openAt(CollectionSearch.query, blockIndex, mdPos)
    }

    // Ctrl+N in collection mode: a new note in the current folder scope
    // (§13.4 New Note), opened immediately.
    function createNoteInCurrentScope() {
        if (!session.appWindow.collectionOpen)
            return
        var folder = NoteListModel.scope === "folder"
            ? NoteListModel.folderPath : ""
        var relPath = NoteCollection.createNote(folder, "")
        if (relPath !== "") {
            session.openNoteByPath(relPath)
            var item = (session.listView.itemAtIndex(0) as BlockDelegateBase)
            if (item && item.focusAtStart)
                item.focusAtStart()
        }
    }

    // features.md §18 create a note from a template: a new note in
    // the current scope, titled by the template, with the template's expanded
    // body loaded and its front-matter (tags, favorite) carried through.
    function createFromTemplate(templateName) {
        if (!session.appWindow.collectionOpen)
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
            session.appWindow.showDocumentError(
                qsTr("The note was created but its template content could not "
                     + "be saved."))
            return relPath
        }
        Qt.callLater(function() {
            var item = (session.listView.itemAtIndex(0) as BlockDelegateBase)
            if (item && item.focusAtStart)
                item.focusAtStart()
        })
        return relPath
    }

    // "Save current note as template": copy the open note (front-matter and
    // body) into .kvit/templates under the given name.
    function saveCurrentNoteAsTemplate(name) {
        if (!session.appWindow.collectionOpen
                || session.appWindow.currentNoteRelPath === "")
            return false
        DocumentManager.flushPendingEdits()
        // The on-disk note text (front-matter + serialized body) is the
        // template; save first so the file reflects the current buffer.
        if (DocumentManager.isDirty)
            DocumentManager.save()
        var fm = NoteCollection.frontMatterFor(
            session.appWindow.currentNoteRelPath)
        var full = (fm ? fm : "") + DocumentSerializer.serialize(BlockModel)
        return NoteTemplates.writeTemplate(name, full)
    }

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
        if (session.appWindow.currentNoteRelPath === relPath
            && DocumentManager.isDirty) {
            session.appWindow.confirmRecoveryOverwrite(relPath)
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
        if (session.appWindow.currentNoteRelPath === relPath) {
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

    function keepMine() {
        // Re-write the editor's content, overwriting the external change.
        // The banner is the only surface that offers this decision, and the
        // watcher will not necessarily raise the same version again, so it
        // stays up until the write actually succeeded.
        if (!DocumentManager.save())
            return false
        session.appWindow.externalConflict = false
        return true
    }

    function loadTheirs(absPath) {
        var target = absPath !== undefined
            ? absPath : session.appWindow.conflictPath
        // Force a reload past openNoteByPath's same-path short-circuit. A
        // failed open leaves the editor on the version it already had, which
        // is still in conflict, so the banner stays for a second attempt.
        if (!DocumentManager.open(DocumentManager.toLocalFileUrl(target)))
            return false
        session.appWindow.lastFocusedBlock = 0
        session.listView.currentIndex = 0
        Qt.callLater(session.appWindow.refreshSessionBaseline)
        session.appWindow.externalConflict = false
        return true
    }

    // features.md §12.1: the open note changed on disk. While it is dirty
    // here both versions are real work, so the window raises its conflict
    // banner and the two functions above are its answers; when it is not
    // dirty, taking the disk version loses nothing and happens silently.
    //
    // Two objects report the same event — the filesystem watcher, which sees
    // the write, and the collection, which reports it after re-indexing — so
    // both are routed through this one function. It is idempotent: a second
    // report for a note whose banner is already up changes nothing, rather
    // than announcing the conflict twice.
    function noteChangedOnDisk(absPath) {
        if (absPath === "" || absPath !== DocumentManager.currentFilePath)
            return   // not the open note — the tree re-scan handles the rest
        if (session.appWindow.externalConflict
            && session.appWindow.conflictPath === absPath)
            return   // already asked, and the answer is still outstanding
        DocumentManager.flushPendingEdits()
        if (DocumentManager.isDirty) {
            session.appWindow.conflictPath = absPath
            session.appWindow.externalConflict = true
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
}
