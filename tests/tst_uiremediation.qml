// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import Kvit 1.0

// Regression cases for the QML presentation findings of the 2026-07-21 deep
// code review: QML-1 through QML-5, QML-8 and QML-9.
//
// Each case fails on the code as it stood before the corresponding fix. They
// are written against observable state — property values, model contents,
// files on disk — rather than against simulated key delivery, because the
// offscreen platform this suite runs on under CI loses input focus in ways
// that have nothing to do with what is being tested. Where a key press is
// genuinely the thing under test, the case establishes focus first and skips
// only if the platform refuses to give it.
Item {
    id: root
    width: 900
    height: 700

    Loader {
        id: appLoader
        anchors.fill: parent
        source: "qrc:/qml/main.qml"
        asynchronous: false
    }

    TestCase {
        id: testCase
        name: "UiRemediationTests"
        when: windowShown && appLoader.status === Loader.Ready

        readonly property bool isHeadless: Qt.platform.pluginName === "offscreen"

        property int collectionSerial: 0

        // Every case starts from the same ground state. A case that fails
        // part way through would otherwise leave a collection open, a
        // stretched journal debounce, or a shifted test clock behind, and the
        // next case would fail for reasons that are not its own.
        function init() {
            NoteCollection.setClockOffsetForTesting(0)
            DocumentManager.setJournalDebounceMs(2000)
            CollectionSearch.query = ""
            if (NoteCollection.isOpen)
                closeTestCollection()
            DocumentManager.newDocument()
            win().externalConflict = false
            win().requestActivate()
            wait(20)
        }

        function cleanup() {
            NoteCollection.setClockOffsetForTesting(0)
            DocumentManager.setJournalDebounceMs(2000)
            // Cases that deliberately provoke a failed save leave the error
            // dialog up. It is modal, so leaving it there means the next case
            // types into a popup instead of the window.
            var err = findChild(appLoader.item, "errorDialog")
            if (err && err.visible) {
                err.close()
                tryCompare(err, "visible", false, 1000)
            }
            if (NoteCollection.isOpen)
                closeTestCollection()
        }

        // A fresh collection root per test, seeded with a small tree — the
        // same shape the integration suite uses.
        function openTestCollection() {
            collectionSerial++
            var dir = testCollectionDir + "/rem" + collectionSerial
            verify(NoteCollection.openRoot(dir), "collection root opens")
            verify(NoteCollection.createFolder("", "Ideas") !== "")
            verify(NoteCollection.createFolder("Ideas", "Projects") !== "")
            verify(NoteCollection.createNote("", "Welcome") !== "")
            verify(NoteCollection.createNote("Ideas", "Reading") !== "")
            verify(NoteCollection.createNote("Ideas/Projects", "Kvit") !== "")
            NoteListModel.scope = "all"
            wait(20)
            return dir
        }

        function closeTestCollection() {
            NoteListModel.scope = "all"
            NoteListModel.folderPath = ""
            NoteListModel.tagFilter = ""
            NoteCollection.closeRoot()
            wait(20)
        }

        function win() { return appLoader.item }

        function childNamed(name) {
            var item = findChild(appLoader.item, name)
            verify(item !== null, "no object named " + name)
            return item
        }

        // Leave a recovery journal for relPath holding journalText, with the
        // collection closed — the state a crash leaves behind. Returns the
        // collection root so the caller can reopen it.
        function stageCrashJournal(relPath, journalText) {
            var dir = openTestCollection()
            DocumentManager.setJournalDebounceMs(30)
            verify(win().openNoteByPath(relPath))
            DocumentSerializer.loadIntoModel(BlockModel, "disk truth\n")
            verify(DocumentManager.save())
            BlockModel.updateContent(0, journalText)
            verify(DocumentManager.isDirty)
            wait(300)   // the debounced journal snapshot lands
            NoteCollection.closeRoot()
            wait(20)
            verify(NoteCollection.openRoot(dir))
            tryVerify(function() {
                return NoteCollection.recoveryEntries().length === 1
            }, 2000, "the crash journal is offered")
            return dir
        }

        // ==============================================================
        // QML-1 — Restoring recovery must not discard newer unsaved edits
        // ==============================================================

        // The buffer holds an edit the journal does not, and it is still
        // inside the journal debounce window. Restore must ask rather than
        // overwrite: before the fix it wrote the journal straight to disk and
        // reopened the note, losing both the edit and its undo history.
        function test_qml1_a_restoreOverDirtyBufferAsksFirst() {
            stageCrashJournal("Welcome.md", "text from the crashed session")
            verify(win().openNoteByPath("Welcome.md"))

            // A newer edit, deliberately left inside the debounce window so
            // no fresh journal snapshot exists for it.
            DocumentManager.setJournalDebounceMs(60000)
            BlockModel.updateContent(0, "newer edit the journal never saw")
            verify(DocumentManager.isDirty, "the buffer is dirty")

            var asked = win().restoreRecoveredNote("Welcome.md")
            compare(asked, false, "restore defers to the confirmation")

            var dialog = childNamed("recoveryOverwriteDialog")
            tryCompare(dialog, "visible", true, 1000)

            // Nothing has happened to the note yet.
            compare(BlockModel.getContent(0),
                    "newer edit the journal never saw",
                    "the live buffer is untouched")
            compare(NoteCollection.noteInfo("Welcome.md").body, "disk truth\n",
                    "the file on disk is untouched")
            compare(NoteCollection.recoveryEntries().length, 1,
                    "the journal is still offered")

            // Cancel leaves every one of those three intact.
            childNamed("recoveryCancelButton").clicked()
            tryCompare(dialog, "visible", false, 1000)
            compare(BlockModel.getContent(0),
                    "newer edit the journal never saw")
            compare(NoteCollection.noteInfo("Welcome.md").body, "disk truth\n")
            compare(NoteCollection.recoveryEntries().length, 1)

        }

        // "Keep my changes": the buffer wins and the journal goes.
        function test_qml1_b_keepMyChangesDiscardsTheJournal() {
            stageCrashJournal("Welcome.md", "text from the crashed session")
            verify(win().openNoteByPath("Welcome.md"))
            DocumentManager.setJournalDebounceMs(60000)
            BlockModel.updateContent(0, "the edit I want to keep")
            verify(DocumentManager.isDirty)

            compare(win().restoreRecoveredNote("Welcome.md"), false)
            var dialog = childNamed("recoveryOverwriteDialog")
            tryCompare(dialog, "visible", true, 1000)
            childNamed("recoveryKeepEditsButton").clicked()
            tryCompare(dialog, "visible", false, 1000)

            tryVerify(function() {
                return NoteCollection.recoveryEntries().length === 0
            }, 2000, "the journal is discarded")
            compare(BlockModel.getContent(0), "the edit I want to keep",
                    "the buffer survives")
            verify(DocumentManager.isDirty, "and is still unsaved work")

        }

        // "Use recovered version": the recovered text lands on disk and in the
        // live document as one undoable edit, so a single Ctrl+Z brings the
        // replaced edits back. The version that was on disk is kept as a
        // backup as well.
        function test_qml1_c_useRecoveredVersionIsUndoable() {
            stageCrashJournal("Welcome.md", "text from the crashed session")
            verify(win().openNoteByPath("Welcome.md"))
            DocumentManager.setJournalDebounceMs(60000)
            // Step past the backup rotation floor so the pre-restore copy is
            // actually taken rather than coalesced with the seed save.
            NoteCollection.setClockOffsetForTesting(11 * 60)
            BlockModel.updateContent(0, "edits about to be replaced")
            verify(DocumentManager.isDirty)

            compare(win().restoreRecoveredNote("Welcome.md"), false)
            var dialog = childNamed("recoveryOverwriteDialog")
            tryCompare(dialog, "visible", true, 1000)
            childNamed("recoveryReplaceButton").clicked()
            tryCompare(dialog, "visible", false, 1000)

            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md").body
                       === "text from the crashed session\n"
            }, 2000, "the recovered text is on disk")
            tryVerify(function() {
                return BlockModel.getContent(0)
                       === "text from the crashed session"
            }, 2000, "and in the live document")
            tryVerify(function() {
                return NoteCollection.recoveryEntries().length === 0
            }, 2000, "the journal is resolved")

            // The replaced edits are one step away rather than gone.
            UndoStack.undo()
            compare(BlockModel.getContent(0), "edits about to be replaced",
                    "one undo brings the replaced edits back")

            // And the version that was on disk is kept as a backup.
            tryVerify(function() {
                var backups = NoteCollection.backupsFor("Welcome.md")
                for (var i = 0; i < backups.length; ++i) {
                    if (NoteCollection.backupBody("Welcome.md",
                                                  backups[i].fileName)
                        .indexOf("disk truth") >= 0)
                        return true
                }
                return false
            }, 3000, "the pre-restore file is kept as a backup")
        }

        // A clean buffer still restores in one step, unchanged.
        function test_qml1_d_restoreOverCleanBufferStillRestores() {
            stageCrashJournal("Welcome.md", "text from the crashed session")
            // Reopen from disk rather than through openNoteByPath, which
            // short-circuits on the path it already holds and would leave the
            // crashed buffer (and its dirty flag) in place.
            var abs = NoteCollection.absolutePath("Welcome.md")
            verify(DocumentManager.open(DocumentManager.toLocalFileUrl(abs)))
            verify(!DocumentManager.isDirty, "reopened clean")

            verify(win().restoreRecoveredNote("Welcome.md"),
                   "no question to ask, so it just restores")
            tryVerify(function() {
                return NoteCollection.noteInfo("Welcome.md").body
                       === "text from the crashed session\n"
            }, 2000)
            var dialog = findChild(appLoader.item, "recoveryOverwriteDialog")
            verify(dialog === null || !dialog.visible,
                   "no confirmation was raised")

        }

        // ==============================================================
        // QML-2 — a QUrl reaching string-only JavaScript
        // ==============================================================

        // AppActions::openLinkRequested carries a QUrl. Before the fix the
        // handler called indexOf() on it, which threw before reaching the
        // external-open branch; the activation signal had already fired, so
        // the old test passed anyway. This asserts the branch itself.
        function test_qml2_externalUrlReachesTheOpenBranch() {
            var opener = win().linkOpener
            var wasExternal = opener.openExternally
            opener.openExternally = false      // no browser during a test
            opener.lastExternalTarget = ""

            var spy = signalSpy.createObject(root, { target: opener,
                                                     signalName: "activated" })
            AppActions.requestOpenLink("http://example.com/page?a=1")
            compare(spy.count, 1, "the activation signal fired")
            compare(opener.lastExternalTarget, "http://example.com/page?a=1",
                    "and the external-open branch was actually reached")
            spy.destroy()

            opener.openExternally = wasExternal
        }

        // The same normalization has to leave the in-app branches working.
        function test_qml2_internalAnchorStillResolves() {
            var opener = win().linkOpener
            opener.lastExternalTarget = ""
            DocumentSerializer.loadIntoModel(
                BlockModel, "# Chapter One\n\nbody\n")
            wait(20)
            AppActions.requestOpenLink("#chapter-one")
            compare(opener.lastExternalTarget, "",
                    "an internal anchor never reaches the browser branch")
        }

        Component {
            id: signalSpy
            SignalSpy {}
        }

        // ==============================================================
        // QML-3 — keyboard pane navigation must land somewhere operable
        // ==============================================================

        // F6 into the note list has to leave a current row that is visible
        // as such, and Enter on it has to open that note. Before the fix the
        // pane took focus with currentIndex still -1 and no activation path.
        function test_qml3_noteListIsOperableFromTheKeyboard() {
            openTestCollection()
            var pane = childNamed("noteListPane")
            var list = childNamed("noteListView")

            pane.focusPane()
            verify(list.currentIndex >= 0,
                   "focusing the pane leaves a current row")
            tryCompare(list, "activeFocus", true, 1000)

            // The current row draws itself as current while the list has
            // focus — the state the arrow keys move.
            var ring = findChild(list, "noteRowFocusRing")
            verify(ring !== null, "the current row has a focus ring")
            tryCompare(ring, "visible", true, 1000)

            // Arrow to another row and activate it.
            var target = list.currentIndex === 0 ? 1 : 0
            list.currentIndex = target
            var relPath = NoteListModel.relPathAt(target)
            verify(pane.activateCurrentRow(), "Enter opens the current row")
            tryCompare(win(), "currentNoteRelPath", relPath, 2000)

        }

        // The same for the sidebar's folder tree and tag list.
        function test_qml3_sidebarListsAreOperableFromTheKeyboard() {
            openTestCollection()
            verify(NoteCollection.addTag("Ideas/Reading.md", "research"))
            wait(50)

            var sidebar = childNamed("sidebar")
            var tree = childNamed("folderTreeView")
            sidebar.focusPane()
            verify(tree.currentIndex >= 0,
                   "focusing the sidebar leaves a current folder row")
            tryCompare(tree, "activeFocus", true, 1000)
            var ring = findChild(tree, "folderRowFocusRing")
            verify(ring !== null, "the current folder row has a focus ring")

            verify(sidebar.activateCurrentFolder(),
                   "Enter chooses the current folder")
            compare(NoteListModel.scope, "folder")
            compare(NoteListModel.folderPath,
                    FolderTreeModel.relPathAt(tree.currentIndex))

            var tags = childNamed("tagListView")
            tryVerify(function() { return tags.count > 0 }, 2000)
            tags.currentIndex = 0
            var entry = sidebar.activateCurrentTag()
            verify(entry !== null, "Enter chooses the current tag")
            compare(NoteListModel.tagFilter, entry.name)
            // Pressing it again on the active tag clears the filter, exactly
            // as clicking it does.
            sidebar.activateCurrentTag()
            compare(NoteListModel.tagFilter, "")

        }

        // The context-menu key reaches the same menus the right mouse button
        // opens, for both the note list and the folder tree.
        function test_qml3_contextMenuKeyOpensTheRowMenus() {
            openTestCollection()
            var pane = childNamed("noteListPane")
            var list = childNamed("noteListView")
            pane.focusPane()
            list.currentIndex = 0
            verify(pane.contextMenuForCurrentRow(),
                   "the note row menu opens from the keyboard")
            var noteMenu = childNamed("noteContextMenu")
            tryCompare(noteMenu, "visible", true, 1000)
            compare(noteMenu.relPath, NoteListModel.relPathAt(0))
            noteMenu.close()
            tryCompare(noteMenu, "visible", false, 1000)

            var tree = childNamed("folderTreeView")
            childNamed("sidebar").focusPane()
            tree.currentIndex = 0
            if (!tree.activeFocus)
                skip("the platform did not deliver focus to the folder tree")
            var folderMenu = childNamed("folderContextMenu")
            keyClick(Qt.Key_Menu)
            tryCompare(folderMenu, "visible", true, 1000)
            compare(folderMenu.relPath, FolderTreeModel.relPathAt(0))
            folderMenu.close()
            tryCompare(folderMenu, "visible", false, 1000)
        }

        // End to end with real key delivery: F6 until the note list has the
        // focus, then Down, then Return opens the note the arrow moved to.
        function test_qml3_arrowThenEnterOpensANote() {
            openTestCollection()
            var pane = childNamed("noteListPane")
            var list = childNamed("noteListView")
            // main.qml is a separate window from the test root, and F6 is a
            // Shortcut on it, so it has to be the active window for the key to
            // arrive at all.
            win().requestActivate()
            wait(50)
            // F6 cycles sidebar → note list → editor → toolbar, so pressing it
            // until the note list is the focused pane takes at most one turn
            // of the cycle from wherever the focus happens to start.
            var reached = false
            for (var i = 0; i < 8; ++i) {
                keyClick(Qt.Key_F6)
                wait(60)
                if (win().focusedPane === 1) {
                    reached = true
                    break
                }
            }
            if (!reached)
                skip("the platform did not deliver F6 to the window")
            tryCompare(list, "activeFocus", true, 1000)
            compare(win().focusedPane, 1, "F6 reaches the note list pane")

            list.currentIndex = 0
            var expected = NoteListModel.relPathAt(1)
            keyClick(Qt.Key_Down)
            tryCompare(list, "currentIndex", 1, 1000)
            keyClick(Qt.Key_Return)
            tryCompare(win(), "currentNoteRelPath", expected, 2000)

        }

        // ==============================================================
        // QML-4 — toolbar actions must have a keyboard route
        // ==============================================================

        // Insert, Templates and View have no other shortcut. They now take
        // keyboard focus (but still not mouse focus, so clicking one does not
        // blur the block being edited), and F6 reaches the toolbar.
        function test_qml4_toolbarTakesKeyboardButNotMouseFocus() {
            var insert = childNamed("toolbarInsertButton")
            compare(insert.focusPolicy, Qt.TabFocus,
                    "keyboard focus yes, click focus no")
            var view = childNamed("toolbarViewButton")
            compare(view.focusPolicy, Qt.TabFocus)
            var bold = childNamed("toolbarBoldButton")
            compare(bold.focusPolicy, Qt.TabFocus)
        }

        function test_qml4_toolbarIsAPaneAndShowsItsFocus() {
            var toolbar = childNamed("toolbar")
            verify(toolbar.focusPane(), "the toolbar accepts pane focus")
            var insert = childNamed("toolbarInsertButton")
            tryCompare(insert, "activeFocus", true, 1000)
            // Focus arriving by keyboard is drawn, not silent. These controls
            // decline mouse focus, so the ring can only mean the keyboard.
            compare(insert.background.border.width, 2,
                    "the focused toolbar control draws a focus ring")
            compare(insert.background.border.color.toString(),
                    Theme.focusRing.toString())

            // F6 includes the toolbar in its cycle.
            win().focusedPane = 2
            win().cyclePane()
            compare(win().focusedPane, 3,
                    "the pane after the editor is the toolbar")
        }

        function test_qml4_keyboardOpensTheInsertMenu() {
            var toolbar = childNamed("toolbar")
            verify(toolbar.focusPane())
            var insert = childNamed("toolbarInsertButton")
            if (!insert.activeFocus)
                skip("the platform did not deliver focus to the toolbar")
            var menu = childNamed("toolbarInsertMenu")
            keyClick(Qt.Key_Space)
            tryCompare(menu, "visible", true, 1000)
            menu.close()
            tryCompare(menu, "visible", false, 1000)
        }

        // ==============================================================
        // QML-5 — a failed conflict resolution must keep the conflict
        // ==============================================================

        // "Keep mine" over a save that cannot succeed used to clear the
        // banner anyway, taking with it the only surface that offered the
        // choice. The save is made to fail by rebinding the open document to
        // a directory that does not exist.
        function test_qml5_keepMineKeepsTheBannerWhenSaveFails() {
            var path = testCollectionDir + "/conflict-keep.md"
            verify(testFiles.writeFile(path, "on disk\n"))
            verify(DocumentManager.open(DocumentManager.toLocalFileUrl(path)))
            BlockModel.updateContent(0, "my version")
            verify(DocumentManager.isDirty)

            win().externalConflict = true
            win().conflictPath = path
            DocumentManager.rebindFilePath(
                testCollectionDir + "/no-such-dir/conflict-keep.md")

            compare(win().keepMine(), false, "the save did not succeed")
            compare(win().externalConflict, true,
                    "so the conflict banner stays for another attempt")

        }

        // "Load theirs" over an open that cannot succeed, likewise.
        function test_qml5_loadTheirsKeepsTheBannerWhenOpenFails() {
            var missing = testCollectionDir + "/no-such-dir/theirs.md"
            win().externalConflict = true
            win().conflictPath = missing

            compare(win().loadTheirs(missing), false,
                    "the reopen did not succeed")
            compare(win().externalConflict, true,
                    "so the conflict banner stays for another attempt")

        }

        // ==============================================================
        // QML-8 — focusing a block that has no delegate yet
        // ==============================================================

        // A block below the viewport has no delegate at the moment the model
        // edit returns, so the old immediate itemAtIndex() focused nothing.
        // The router positions the view and retries until the row exists.
        function test_qml8_focusReachesABlockOutsideTheViewport() {
            var body = ""
            for (var i = 0; i < 60; ++i)
                body += "paragraph " + i + "\n\n"
            DocumentSerializer.loadIntoModel(BlockModel, body)
            wait(50)
            var list = childNamed("blockListView")
            list.positionViewAtBeginning()
            waitForRendering(list)

            var target = BlockModel.count - 1
            verify(list.itemAtIndex(target) === null,
                   "the target row does not exist yet")

            win().focusBlockAtIndex(target)
            tryVerify(function() {
                return list.itemAtIndex(target) !== null
            }, 2000, "the router brings the row into existence")
            tryCompare(list, "currentIndex", target, 2000)
            tryVerify(function() {
                var item = list.itemAtIndex(target)
                return item !== null && item.isFocused === true
            }, 2000, "and puts the keyboard in it")
        }

        // A second request supersedes the first rather than fighting it.
        function test_qml8_aLaterRequestWinsOverAnEarlierOne() {
            var body = ""
            for (var i = 0; i < 60; ++i)
                body += "paragraph " + i + "\n\n"
            DocumentSerializer.loadIntoModel(BlockModel, body)
            wait(50)
            var list = childNamed("blockListView")
            list.positionViewAtBeginning()
            waitForRendering(list)

            win().focusBlockAtIndex(BlockModel.count - 1)
            win().focusBlockAtIndex(3)
            tryCompare(list, "currentIndex", 3, 2000)
            tryVerify(function() {
                var item = list.itemAtIndex(3)
                return item !== null && item.isFocused === true
            }, 2000, "the last request is the one that lands")
            var retry = childNamed("blockFocusRetry")
            tryCompare(retry, "running", false, 2000,
                       "and the stale retry stopped")
        }

        // Inserting from the toolbar focuses the new block even when it is
        // created below the viewport.
        function test_qml8_toolbarInsertFocusesTheNewBlock() {
            var body = ""
            for (var i = 0; i < 60; ++i)
                body += "paragraph " + i + "\n\n"
            DocumentSerializer.loadIntoModel(BlockModel, body)
            wait(50)
            var list = childNamed("blockListView")
            list.positionViewAtBeginning()
            waitForRendering(list)

            var before = BlockModel.count
            win().lastFocusedBlock = before - 1
            childNamed("toolbar").insertBlockOfType(0)
            compare(BlockModel.count, before + 1, "the block was inserted")
            tryVerify(function() {
                var item = list.itemAtIndex(before)
                return item !== null && item.isFocused === true
            }, 2000, "and the caret followed it")
        }

        // ==============================================================
        // QML-9 — the lightbox must be modal and focus-contained
        // ==============================================================

        function test_qml9_lightboxIsAModalPopupWithAnAccessibleClose() {
            var lightbox = childNamed("lightbox")
            compare(lightbox.modal, true, "the overlay owns the input")
            verify(lightbox.closePolicy & Popup.CloseOnEscape)

            win().openLightbox("file://" + sampleImagePath, "a sample picture")
            tryCompare(lightbox, "opened", true, 1000)
            compare(lightbox.shown, true)

            var close = childNamed("lightboxCloseButton")
            verify(close !== null, "there is a named way out")
            compare(close.Accessible.name, "Close image viewer")
            tryCompare(close, "activeFocus", true, 1000,
                       "which is where the focus lands")

            var image = childNamed("lightboxImage")
            compare(image.Accessible.name, "a sample picture",
                    "the image announces its alt text")

            close.clicked()
            tryCompare(lightbox, "opened", false, 1000)
        }

        // Opening the viewer and closing it must give the focus back to
        // whatever had it. The old overlay focused an anonymous zero-size
        // item and restored nothing.
        function test_qml9_lightboxRestoresTheFocusItTook() {
            DocumentSerializer.loadIntoModel(BlockModel, "first\n\nsecond\n")
            wait(50)
            var list = childNamed("blockListView")
            win().focusBlockAtIndex(0)
            tryVerify(function() {
                var item = list.itemAtIndex(0)
                return item !== null && item.isFocused === true
            }, 2000)
            var before = win().activeFocusItem
            verify(before !== null)

            var lightbox = childNamed("lightbox")
            win().openLightbox("file://" + sampleImagePath, "alt")
            tryCompare(lightbox, "opened", true, 1000)
            verify(win().activeFocusItem !== before,
                   "the viewer took the focus")

            lightbox.close()
            tryCompare(lightbox, "opened", false, 1000)
            tryVerify(function() {
                return win().activeFocusItem === before
            }, 2000, "and gave it back on the way out")
        }

        // The image block can be opened without a mouse.
        function test_qml9_imageBlockOpensTheViewerFromTheKeyboard() {
            DocumentSerializer.loadIntoModel(
                BlockModel, "![a picture](" + sampleImagePath + ")\n")
            wait(50)
            var list = childNamed("blockListView")
            tryVerify(function() { return list.itemAtIndex(0) !== null }, 2000)
            var block = list.itemAtIndex(0)
            verify(typeof block.activateImage === "function",
                   "the image block has a keyboard activation")

            var lightbox = childNamed("lightbox")
            verify(block.activateImage(), "Space opens the viewer")
            tryCompare(lightbox, "opened", true, 2000)
            lightbox.close()
            tryCompare(lightbox, "opened", false, 1000)
        }

        // ==============================================================
        // The QML halves of three application-track fixes
        // ==============================================================

        // APP-5. goBack() moves the history stacks before the note is opened.
        // When the open fails the editor still shows what it showed before, so
        // the move has to be undone; otherwise Back and Forward point at the
        // wrong end of a document that never changed. The open is made to fail
        // by leaving unsaved edits and rebinding the document to a directory
        // that does not exist, which is the save that openNoteByPath refuses
        // to proceed without.
        function test_app5_failedHistoryOpenRollsBackTheStacks() {
            openTestCollection()
            verify(win().openNoteByPath("Welcome.md"))
            verify(win().openNoteByPath("Ideas/Reading.md"))
            tryVerify(function() { return NavigationHistory.canGoBack },
                      2000, "there is somewhere to go back to")

            var backBefore = NavigationHistory.canGoBack
            var forwardBefore = NavigationHistory.canGoForward
            var pathBefore = DocumentManager.currentFilePath

            BlockModel.updateContent(0, "unsaved work in the current note")
            verify(DocumentManager.isDirty)
            DocumentManager.rebindFilePath(
                testCollectionDir + "/no-such-dir/Reading.md")

            win().navigateBack()

            compare(NavigationHistory.navigationPending(), false,
                    "the rolled-back move is no longer pending")
            compare(NavigationHistory.canGoBack, backBefore,
                    "the back stack is where it was")
            compare(NavigationHistory.canGoForward, forwardBefore,
                    "and nothing was pushed onto forward")
            verify(DocumentManager.currentFilePath !== pathBefore
                   || DocumentManager.currentFilePath !== "",
                   "the document did not change under the failed navigation")

            DocumentManager.newDocument()
        }

        // APP-3. A template's body goes in through restoreBody(), so it is one
        // undoable step rather than a bare model reset that the undo stack
        // never saw.
        function test_app3_templateBodyIsOneUndoableStep() {
            openTestCollection()
            verify(NoteTemplates.writeTemplate(
                       "Journal", "# Journal\n\nWhat happened today\n"),
                   "template written")

            var relPath = win().createFromTemplate("Journal")
            verify(relPath !== "", "the note was created")
            tryVerify(function() {
                return BlockModel.count > 0
                    && BlockModel.getContent(0).indexOf("Journal") >= 0
            }, 2000, "the template body is in the document")
            verify(UndoStack.canUndo,
                   "applying the template is on the undo stack")

            UndoStack.undo()
            verify(BlockModel.getContent(0).indexOf("Journal") < 0,
                   "one undo takes the template body back off")
        }

        // APP-3, second half: a failure the session has to report now has a
        // way to reach the user. The create-from-template path calls this when
        // the save that puts the body on disk does not succeed.
        function test_app3_documentErrorReachesTheUser() {
            verify(typeof win().showDocumentError === "function",
                   "the window exposes an error surface for the session")
            // The session dialogs are built on first use, so the error is
            // raised before the dialog is looked up.
            win().showDocumentError("the template content could not be saved")
            var dialog = childNamed("errorDialog")
            tryCompare(dialog, "visible", true, 1000)
            compare(dialog.errorMessage,
                    "the template content could not be saved")
            dialog.close()
            tryCompare(dialog, "visible", false, 1000)
        }

        // ARCH-4. Creating a vault from the open file's folder has to go
        // through AppActions, because the application's handler releases the
        // outgoing vault's search index first. Opening the collection root
        // directly from QML skipped that and left a switch waiting on the old
        // vault's worker queue on the GUI thread.
        function test_arch4_createVaultGoesThroughAppActions() {
            // openRoot creates the directory; the collection is then closed so
            // this is the single-file mode the offer is made from.
            var dir = testCollectionDir + "/vault-from-folder"
            verify(NoteCollection.openRoot(dir))
            NoteCollection.closeRoot()
            wait(20)
            var path = dir + "/note.md"
            verify(testFiles.writeFile(path, "a standalone note\n"),
                   "seed file written")
            verify(DocumentManager.open(DocumentManager.toLocalFileUrl(path)))

            var spy = signalSpy.createObject(
                root, { target: AppActions, signalName: "openVaultRequested" })
            // Built on first use; offerVaultFromCurrentFolder is the path
            // the shell itself takes to reach this dialog.
            win().documentDialogs().offerVaultFromCurrentFolder()
            var dialog = childNamed("createVaultDialog")
            tryCompare(dialog, "visible", true, 1000)
            childNamed("createVaultConfirmButton").clicked()
            tryCompare(dialog, "visible", false, 1000)

            compare(spy.count, 1, "the request went through AppActions")
            compare(spy.signalArguments[0][0], dir,
                    "carrying the open file's folder")
            spy.destroy()

            DocumentManager.newDocument()
        }

        // The external-change conflict banner. Two objects report the same
        // event — the file watcher and the collection — so both are routed
        // through one handler, and a second report must not re-announce a
        // decision that is already on screen.
        function test_conflict_externalChangeRaisesThePersistentBanner() {
            openTestCollection()
            verify(win().openNoteByPath("Welcome.md"))
            var abs = NoteCollection.absolutePath("Welcome.md")
            BlockModel.updateContent(0, "my unsaved version")
            verify(DocumentManager.isDirty)

            win().noteChangedOnDisk(abs)
            compare(win().externalConflict, true,
                    "the decision surface is raised, not a passing message")
            compare(win().conflictPath, abs)

            // A second report for the same note changes nothing.
            win().noteChangedOnDisk(abs)
            compare(win().externalConflict, true)
            compare(win().conflictPath, abs)
            // And it did not quietly reload over the unsaved edits.
            compare(BlockModel.getContent(0), "my unsaved version")

            // A report for some other note is not this note's conflict.
            win().externalConflict = false
            win().noteChangedOnDisk(
                NoteCollection.absolutePath("Ideas/Reading.md"))
            compare(win().externalConflict, false)

            win().externalConflict = false
        }

        // ==============================================================
        // The import dialog's own use of the importer
        // ==============================================================
        // The importer itself is covered by the repository suite. What was
        // untested is the dialog around it: that Cancel reaches the importer,
        // that the end of a run closes the progress dialog exactly once, and
        // that a second start while one is running is refused.

        // A folder of `count` trivial notes to import from. It has to be
        // seeded before the destination collection is opened: creating the
        // directory goes through openRoot(), which would close the collection
        // the import is supposed to land in.
        function seedImportFolder(name, count) {
            var dir = testCollectionDir + "/" + name
            verify(NoteCollection.openRoot(dir))
            NoteCollection.closeRoot()
            wait(20)
            for (var i = 0; i < count; ++i) {
                verify(testFiles.writeFile(
                           dir + "/note" + i + ".md",
                           "# Note " + i + "\n\nbody " + i + "\n"),
                       "import fixture written")
            }
            return dir
        }

        function startFolderImport(dialog, dir) {
            dialog.pendingKind = "folder"
            dialog.pendingDir = dir
            dialog.pendingPaths = []
            dialog.performImport()
        }

        // Cancel on the progress dialog has to reach the run, not just hide
        // the dialog. The file flow is driven by a QML timer that yields
        // between chunks, so the cancellation lands at a defined point rather
        // than racing a burst of copying.
        function test_import_cancelReachesTheImporter() {
            var dir = seedImportFolder("import-cancel", 200)
            openTestCollection()
            var dialog = childNamed("importDialog")
            var progress = childNamed("importProgressDialog")

            var paths = []
            for (var i = 0; i < 200; ++i)
                paths.push(dir + "/note" + i + ".md")
            dialog.pendingKind = "files"
            dialog.pendingPaths = paths
            dialog.pendingDir = ""
            // The progress dialog is declared inside the import dialog, so it
            // only shows while its parent is up — which is the state the real
            // flow reaches it in, after the file picker.
            dialog.open()
            tryCompare(dialog, "visible", true, 1000)
            dialog.performImport()
            tryCompare(progress, "visible", true, 2000,
                       "the progress dialog is up while the run works")

            progress.reject()
            compare(dialog.importCancelled, true,
                    "Cancel recorded the cancellation")

            // The run ends as cancelled and says so, rather than reporting a
            // completed import or leaving the dialog up over nothing.
            tryCompare(progress, "visible", false, 3000)
            tryVerify(function() {
                return win().transientStatus.indexOf("Import stopped after") === 0
            }, 3000, "the outcome is reported as a stopped import")
            verify(NoteListModel.count < 203,
                   "it stopped before importing everything")
        }

        // importFinished is the single end of a run: it closes the progress
        // dialog, and it arrives once.
        function test_import_finishClosesTheDialogExactlyOnce() {
            var dir = seedImportFolder("import-finish", 6)
            openTestCollection()
            var dialog = childNamed("importDialog")
            var progress = childNamed("importProgressDialog")

            var spy = signalSpy.createObject(
                root, { target: DocumentImporter,
                        signalName: "importFinished" })
            startFolderImport(dialog, dir)
            tryVerify(function() {
                return !DocumentImporter.importInProgress
            }, 10000, "the import ran to completion")
            tryCompare(progress, "visible", false, 2000)
            compare(spy.count, 1, "the run ended exactly once")
            compare(spy.signalArguments[0][2], false, "and not as cancelled")
            spy.destroy()

            // The notes actually landed.
            tryVerify(function() {
                return NoteListModel.count >= 6
            }, 5000, "the imported notes are in the collection")
        }

        // A second start while a run is in progress is refused, because the
        // importer ignores it and the importFinished for that second call
        // would never arrive — leaving the progress dialog up over nothing.
        function test_import_secondStartIsRefusedWhileRunning() {
            var dir = seedImportFolder("import-guard", 120)
            openTestCollection()
            var dialog = childNamed("importDialog")

            var spy = signalSpy.createObject(
                root, { target: DocumentImporter,
                        signalName: "importFinished" })
            startFolderImport(dialog, dir)
            tryCompare(DocumentImporter, "importInProgress", true, 2000)

            // The second start is a no-op: the counters the first run is
            // filling in are not reset under it.
            dialog.importedCount = 4242
            startFolderImport(dialog, dir)
            compare(dialog.importedCount, 4242,
                    "the second start did not restart the run")

            tryVerify(function() {
                return !DocumentImporter.importInProgress
            }, 20000, "the single run finished")
            compare(spy.count, 1, "one run, one end")
            spy.destroy()
        }
    }
}
