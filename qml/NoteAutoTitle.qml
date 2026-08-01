// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Naming a new note from what is written in it (features.md §8.3).
//
// A note is created before there is anything in it to name it after, so it
// arrives as "Untitled", "Untitled 2" and so on — and a note list full of
// those is a note list nobody can read. The first block usually says what the
// note is: a heading, or failing that the opening words of its first
// paragraph. This takes that text and renames the file to it, once.
//
// Once, because the rule is the name itself: only a note still carrying the
// automatic name is ever renamed here, so the moment it has a real name —
// this one's doing or the reader's — nothing here touches it again. Editing
// the heading afterwards does not rename the file, which is what makes a name
// the reader typed, or kept, stay put.
//
// The rename goes through the same plan-and-apply path a manual rename takes,
// with the link update authorized rather than asked about: a note this new is
// unlikely to be linked at all, and when it is, silently breaking those links
// would be the worse answer. Everything else about it is silent — a name
// already taken, or first-block text that cannot be a file name, leaves the
// note called what it was called.
Item {
    id: autoTitle

    // Wired by main.qml.
    property var appWindow
    property var renameWorkflow

    // How much of a paragraph becomes a name. A heading is normally shorter
    // than this; the cap is what keeps a note titled by its first sentence
    // from being titled by its first three.
    readonly property int titleLengthCap: 60

    // The kinds whose text describes the note. A note that opens with a
    // table, an image or a code fence is left alone: the markdown of those is
    // not a sentence, and a file named after it would be worse than
    // "Untitled".
    function titleBearing(type) {
        return type === Block.Paragraph
            || type === Block.Heading1 || type === Block.Heading2
            || type === Block.Heading3 || type === Block.Heading4
            || type === Block.BulletList || type === Block.NumberedList
            || type === Block.Todo || type === Block.Quote
            || type === Block.Callout
    }

    // The name the open note would take, or "" if it would take none.
    function candidateTitle() {
        if (BlockModel.count === 0)
            return ""
        var block = BlockModel.blockAt(0)
        if (!block || !autoTitle.titleBearing(block.blockType))
            return ""
        return NoteCollection.titleFromText(
            BlockModel.getContent(0).substring(0, autoTitle.titleLengthCap * 2))
    }

    function folderOf(relPath) {
        var slash = relPath.lastIndexOf("/")
        return slash < 0 ? "" : relPath.substring(0, slash)
    }

    // Rename the open note if it is still untitled and its first block says
    // what to call it. Returns whether it was renamed, which is what the test
    // reads; nothing in the window depends on the answer.
    function titleOpenNote() {
        if (!autoTitle.appWindow || !autoTitle.appWindow.collectionOpen
                || !autoTitle.renameWorkflow)
            return false
        var relPath = autoTitle.appWindow.currentNoteRelPath
        if (relPath === "" || !NoteCollection.isUntitledNote(relPath))
            return false
        var title = autoTitle.candidateTitle()
        if (title === "")
            return false
        if (NoteCollection.noteTitleTaken(autoTitle.folderOf(relPath), title))
            return false
        var plan = NoteCollection.planNoteRename(relPath, title)
        if (!plan || !plan.ok)
            return false
        var result = autoTitle.renameWorkflow.executeRenamePlan(plan.id, true)
        return result !== undefined && result !== null && result.ok === true
    }

    // When the note is named. Leaving the first block is the moment the text
    // in it is finished — pressing Enter for the next block, or clicking away
    // — and renaming any earlier would name the note after half a heading.
    Connections {
        target: autoTitle.appWindow
        function onCaretBlockIndexChanged() {
            if (autoTitle.appWindow.caretBlockIndex !== 0)
                autoTitle.titleOpenNote()
        }
    }
    // And whenever the note is written, which covers the reader who types a
    // heading and stays in it: the autosave, Ctrl+S and the save that closing
    // or switching notes performs all land here.
    Connections {
        target: DocumentManager
        function onLastSavedAtChanged() { autoTitle.titleOpenNote() }
    }
}
