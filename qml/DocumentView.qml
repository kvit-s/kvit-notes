// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// A markdown document drawn somewhere other than the editor pane, and drawn
// by the editor (selection.md "A document drawn read-only").
//
// What this is for. Several places put a document on screen without being the
// editor: the stored versions of the open note in the backup dialog, a
// referring note's context, a search snippet, a transcript in an application
// built on this one. Each wants the markdown rendered as what it means rather
// than shown as a paragraph of asterisks, the pointer able to sweep across
// the whole of it as one piece of text, and the copy coming out as markdown.
//
// What it is made of. One BlockEditor with `readOnly` set, over a document
// this component owns. That is the whole of the design decision here, and it
// is worth stating why: the alternative is a second renderer, and a second
// renderer draws a subset. The seventeen block kinds reach the screen through
// BlockKindRegistry.delegateChoices(), so drawing a document with anything
// else means a hardcoded list of the kinds somebody remembered — a table
// drawn as its pipe characters, a Mermaid diagram as its source, and nothing
// at all for a kind a linked module registered. Using the editor, a document
// drawn here is the document as the editor draws it, including the kinds that
// did not exist when this file was written.
//
// What read-only means is at BlockEditorSurface.readOnly: the text areas hold
// their text with `readOnly` rather than being switched off, so the pointer
// still sweeps, Ctrl+C still copies and the cross-block coordinator still
// works, while every key and every gesture that would write is refused where
// it is raised.
//
// Sizing. Width comes from the container, since a rendered document is as
// wide as it is given and wraps into it. Height is one of two things, chosen
// by `growsWithDocument`. By default it follows the document, so a surface
// can sit inside a scrolling area it does not own; the block list then builds
// every row, since every row is inside the list's own height. Set to false,
// the surface is as tall as its host makes it and scrolls the document
// itself, the way the note's editor does: only the rows on screen are built,
// and a row scrolled away is recycled. A long document wants the second:
// over 1,237 blocks of this repository's own documentation, a surface growing
// with it took 3.8 s to open and 16.5 ms per wheel step, and one scrolling it
// took 0.18 s and 1.7 ms.
//
// Marking. A caller that knows something about part of what it asked to be
// drawn — which characters differ from the note as it stands, which phrase a
// search hit fell on — registers it on `decorations`, the same seam a linked
// module marks the open note through, addressed the same way: block index and
// offset in the block's display text. The registry is this surface's own, so
// two surfaces in one window mark their own documents.
Item {
    id: surface

    // ---- What the host puts in -------------------------------------------

    // The document to draw. Assigning re-parses and drops any selection.
    property string markdown: ""

    // The blank-line rhythm between blocks. The editor's own by default; a
    // pane that wants a denser preview turns it down.
    property int blockSpacing: Typography.paragraphSpacing

    // Whether the surface is as tall as its whole document (true, the
    // default) or as tall as its host makes it, scrolling the document itself
    // (false). See "Sizing" above. The first is for a surface inside a pane
    // that scrolls it along with other things, such as a short passage among
    // other content; it builds every row, which is slow for a long document.
    // The second is for a surface that is the whole of a pane, and builds
    // only what is on screen.
    property bool growsWithDocument: true

    // The space between the surface's edge and the text. Smaller than the
    // editor's reading margin, which is the twenty pixels a page of prose
    // wants and a preview pane does not have.
    property int contentMargin: 8

    // The directory a relative path inside the document is written against —
    // an image block's `![alt](charts/retention.png)`.
    //
    // A drawn document is often not the open note, and the file it came out of
    // is often not where its paths are anchored either: a stored version of a
    // note sits in the backup tree while its pictures are still written
    // against the note's own folder, and a surface may be built from a string
    // with no file behind it at all. So this is a property rather than
    // something the surface works out, defaulting to the open note's
    // directory, which is the right answer for a preview of that note's past.
    property string baseDir: {
        var path = DocumentManager.currentFilePath
        var cut = path.lastIndexOf("/")
        return cut >= 0 ? path.substring(0, cut) : ""
    }

    // The vault a picture path is also looked up in when it is not beside
    // the document: a note in a subfolder names `assets/a.png` from the
    // vault's root (BlockEditorSurface.assetRoot). The open collection's root
    // by default, which is where a stored version of one of its notes has its
    // pictures. A host drawing a file from a copy of the vault kept in
    // another folder names that folder, so the pictures drawn are the copy's.
    property string assetRoot: NoteCollection.isOpen ? NoteCollection.rootPath : ""

    // The folder a path starting with "/" is looked up in, as a website names
    // a file at its root (BlockEditorSurface.siteRoot). The vault's own site
    // folder, from its settings, taken inside assetRoot, so a host that names
    // a copy of the vault gets the copy's site folder with it.
    property string siteRoot: {
        if (surface.assetRoot === "")
            return ""
        var folder = NoteCollection.isOpen ? NoteCollection.vaultSettings.siteFolder : ""
        return folder === "" ? surface.assetRoot : surface.assetRoot + "/" + folder
    }

    // What a `[[wiki link]]` in the drawn document resolves against. The open
    // collection by default, which is what makes a link in a stored version of
    // a note style as resolved.
    property NoteCollection linkResolver: NoteCollection

    // ---- What the host reads ---------------------------------------------

    readonly property int blockCount: docBlocks.count
    readonly property bool hasSelection: docSelection.hasTextSelection

    // This surface's marked ranges. `decorations.addSpan(owner, block, start,
    // length, style, colour)` marks a run of characters in one block's display
    // text, and `decorations.spanRects(id)` says where it was drawn.
    readonly property alias decorations: docDecorations

    // The editor itself, for a host that wants one of its properties — the
    // maximum content width, a heading level filter, the outline behind the
    // drawn document.
    readonly property alias editor: blockEditor
    // This surface's own document, for a host that reads it directly.
    readonly property alias blocks: docBlocks

    // ---- What the host calls ---------------------------------------------

    // The selected range as markdown: whole blocks serialized with their
    // prefixes, fences and ordinals, and a self-contained inline fragment at
    // each partially covered end. Empty when nothing is selected.
    function selectedMarkdown() { return docSelection.rangeMarkdown() }

    // The selected range as block indexes and markdown offsets:
    // {startIndex, startPos, endIndex, endPos}.
    function selectedRange() { return docSelection.orderedTextRange() }

    // Select the whole surface. False when there is nothing to select, which
    // is what tells Ctrl+A to fall through.
    function selectAll() {
        var count = docBlocks.count
        if (count === 0)
            return false
        docSelection.beginTextSelection(0, 0, DocumentSelection.BlockGranularity)
        docSelection.updateTextSelectionHead(
            count - 1, docBlocks.getContent(count - 1).length)
        return docSelection.hasTextSelection
    }

    function clearSelection() { docSelection.clearTextSelection() }

    // Copy the selection in every clipboard flavour, as the editor's own
    // cross-block copy does (features.md §5.1). False when there was nothing
    // to copy.
    function copySelection() {
        var md = surface.selectedMarkdown()
        if (md === "")
            return false
        Clipboard.setMarkdown(md, MarkdownFormatter.toHtml(md))
        return true
    }

    // The markdown of one block, which is what a caller that set its marks
    // apart by index needs in order to check it named the right ones.
    function blockMarkdown(index) { return docBlocks.getContent(index) }

    // Place the rows now rather than at the next frame. A list positions a row
    // it has just been given during its next polish, so everything that asks
    // where a block is would otherwise answer from the arrangement before the
    // document was set.
    function forceLayout() { blockEditor.listView.forceLayout() }

    // The item drawing one block, for a caller that needs the row itself
    // rather than the space it occupies. Null for a block the list has not
    // built: while the surface grows with its document that means only an
    // index it does not hold, and while it scrolls it also means a block
    // outside the rows on screen.
    function blockItem(index) {
        return blockEditor.listView.itemAtIndex(index)
    }

    // ---- The surface -----------------------------------------------------

    // Height follows the document unless the surface scrolls; width comes
    // from the container. An implicit width taken from the rows would be
    // circular: each row is as wide as the list, and the list is as wide as
    // this. A surface that scrolls reports no height of its own, since the
    // list's is an estimate for every row it has not built.
    implicitHeight: surface.growsWithDocument
        ? blockEditor.listView.contentHeight + 2 * surface.contentMargin : 0

    onMarkdownChanged: surface.reload()
    Component.onCompleted: surface.reload()

    function reload() {
        docSelection.clearTextSelection()
        DocumentSerializer.loadIntoModel(docBlocks, surface.markdown)
    }

    BlockEditor {
        id: blockEditor
        anchors.fill: parent

        blocks: docBlocks
        selection: docSelection
        undoStack: docUndo
        search: docSearch
        outline: docOutline
        heights: docHeights
        stats: docStats
        decorations: docDecorations

        documentDirectory: surface.baseDir
        assetRoot: surface.assetRoot
        siteRoot: surface.siteRoot
        linkResolver: surface.linkResolver

        readOnly: true
        showGutter: false
        showFindBar: false
        showFormattingBar: false
        // Nothing to scroll while the surface grows with its document.
        showScrollBar: !surface.growsWithDocument
        typewriterMode: false

        contentMargin: surface.contentMargin
        contentTopMargin: surface.contentMargin
        // Nothing to scroll past the end. A surface that grows with its
        // document has no viewport for the last block to be pulled up into,
        // and one that scrolls is read rather than written in, so its last
        // line has no reason to leave the bottom edge.
        trailingScrollSpace: 0
        blockSpacing: surface.blockSpacing
    }

    // The document, and the seven objects that hold its state. Every one of
    // them defaults to the window's, so a surface that left any unset would be
    // drawing into the open note's undo history, outline, statistics, search
    // matches, height table or decoration seam.
    //
    // The undo stack is here for the same reason the others are rather than
    // because anything pushes to it: with none, the model would fall back to
    // no stack at all, and with the window's it would be the note's.
    DocumentBlocks {
        id: docBlocks
        blockKindRegistry: BlockKindRegistry
        undoStack: docUndo
    }

    DocumentUndoStack { id: docUndo }
    DocumentBlockSelection { id: docSelection; model: docBlocks }
    DocumentBlockSearch { id: docSearch; model: docBlocks }
    DocumentBlockOutline { id: docOutline; model: docBlocks }
    DocumentBlockHeights { id: docHeights; model: docBlocks }
    DocumentBlockStats { id: docStats; model: docBlocks }
    DocumentBlockDecorations { id: docDecorations }
}
