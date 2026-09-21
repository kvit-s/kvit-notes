// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// The block editor sized like a text field: a message box, a comment field, a
// capture box.
//
// What this is for. The editor is built for a pane — a document filling a
// window, with a gutter down its left, floating bars over it and a scrollbar
// at its edge. Several places want the same editing instead of a plain
// TextArea, in a box a few lines tall: a message to a coding agent, a comment
// on a passage, a note jotted without opening the main window. Those places
// take markdown, because whatever reads them afterwards takes markdown, and
// what they draw today is the markdown itself — a heading is a hash and a
// word, bold is a word between asterisks. This draws what the markdown means,
// with the same keys, lists, tables and pictures the note has, and hands back
// clean markdown.
//
// What it is made of. One BlockEditor with its chrome switched off, over a
// document this component owns: a block model, a selection, an undo stack,
// and the five projections a document needs. Owning all eight is the point.
// Every one of them defaults to the window's — BlockEditorSurface says so at
// the top — so a box that left any of them unset would be typing into the
// open note's undo history, outline, statistics, search matches, height table
// or decoration seam. It would not fail; it would corrupt the note's state
// quietly, which is why tests/test_embeddededitor.cpp checks each of the six
// by name.
//
// Sizing. The box is as tall as what has been typed into it, from one line up
// to `maximumLines`, and scrolls inside itself past that. The host gives it a
// width and reads `implicitHeight`.
//
// What it does not draw. No frame and no background: the host draws the box,
// because the box belongs to whatever the composer sits in. No gutter, no
// drag handles, no formatting bar, no find bar, no scrollbar, and no
// typewriter mode. A host that wants one of those back sets the matching flag
// on `editor`.
Item {
    id: root

    // ---- What the host puts in ------------------------------------------

    // The tallest the box grows before it scrolls, counted in lines of body
    // text. Twelve is about a third of a screen, which is as much of a
    // message as is worth showing at once.
    property int maximumLines: 12

    // The space between the box's edge and the text. Smaller than the
    // editor's own reading margin, which is the twenty pixels a page of prose
    // wants and a two-line box does not have.
    property int contentMargin: 6

    // Whether Enter sends. True is the chat convention — Enter sends,
    // Shift+Enter starts a new line — and it is what `submitted` is emitted
    // from. False gives Enter back to the editor, where it makes the next
    // block, which is what a capture box wants.
    property bool returnSubmits: true

    // What an empty box says it is for.
    property string placeholderText: qsTr("Type something...")

    // Where this document lives, forwarded to the editor. Each has a default
    // meaning "there is no collection": with no `assetSink` a pasted image is
    // refused rather than written to a folder nobody chose, and with no
    // `linkResolver` a [[wiki link]] styles as an ordinary link. See the same
    // four properties on BlockEditorSurface.
    property string documentPath: ""
    property string assetRoot: ""
    property AssetStore assetSink: null
    property NoteCollection linkResolver: null

    // ---- What the host reads --------------------------------------------

    // The editor itself, for a host that wants one of the switched-off pieces
    // of chrome back, or that has its own drag layer to lend it.
    readonly property alias editor: blockEditor
    // This box's own document, for a host that watches it directly.
    readonly property alias blocks: docBlocks
    readonly property alias undoStack: docUndo

    // True while the box holds nothing: no blocks, or one block with no text.
    // What a send button is disabled on.
    readonly property bool empty: {
        var countDep = docBlocks.count
        var contentDep = docBlocks.documentCharCount
        return countDep === 0 || (countDep === 1 && contentDep === 0)
    }

    // Enter, with `returnSubmits` on. The markdown is passed rather than left
    // for the host to fetch, because the common case is sending it and then
    // clearing the box, and fetching it afterwards would fetch the empty one.
    signal submitted(string markdown)

    // Something in the box changed. For a host saving a draft; debounce it,
    // since this fires per edit and `markdown()` walks the whole document.
    signal edited()

    // ---- What the host calls --------------------------------------------

    // The document as markdown. A walk over every block, so a host that wants
    // it on each keystroke should debounce rather than bind to it.
    function markdown() {
        return DocumentSerializer.serialize(docBlocks)
    }

    // Replace the document with `text`, dropping the selection and the undo
    // history — this is a new document, and an undo across the boundary would
    // restore the previous one.
    function setMarkdown(text) {
        docSelection.clear()
        DocumentSerializer.loadIntoModel(docBlocks, text)
        docUndo.clear()
    }

    // Empty the box: one empty paragraph, which is what an empty document is
    // here and what puts the caret somewhere to type.
    function clear() {
        root.setMarkdown("")
        if (docBlocks.count === 0)
            docBlocks.insertBlock(0, Block.Paragraph, "")
        docUndo.clear()
    }

    // Put the caret in the box.
    function focusEditor() {
        blockEditor.focusEditor()
    }

    // ---- Height ----------------------------------------------------------

    // One line of body text, which is what `maximumLines` counts. The rows
    // themselves are taller than this — a paragraph carries a few pixels of
    // padding — so twelve lines of cap is a little over twelve lines of text,
    // which is the direction to be wrong in for a box that grows.
    readonly property real lineUnit:
        Math.max(1, Math.round(Typography.bodySize * Typography.lineHeight))

    implicitHeight: Math.min(blockEditor.listView.contentHeight,
                             root.maximumLines * root.lineUnit)
                    + 2 * root.contentMargin
    implicitWidth: 240

    // ---- The document ----------------------------------------------------
    //
    // Eight objects, joined here rather than by AppContext, which builds the
    // window's set and knows nothing about this one. `blockKindRegistry` is
    // the shared one, so a fence kind a linked module registered is drawn in
    // a message exactly as it is in a note.

    DocumentBlocks {
        id: docBlocks
        blockKindRegistry: BlockKindRegistry
        undoStack: docUndo
        onCountChanged: root.edited()
        onDocumentCountsChanged: root.edited()
    }

    DocumentUndoStack {
        id: docUndo
    }

    DocumentBlockSelection {
        id: docSelection
        model: docBlocks
    }

    DocumentBlockSearch {
        id: docSearch
        model: docBlocks
    }

    DocumentBlockOutline {
        id: docOutline
        model: docBlocks
    }

    DocumentBlockHeights {
        id: docHeights
        model: docBlocks
    }

    DocumentBlockStats {
        id: docStats
        model: docBlocks
    }

    // No model: its entries are addressed by block index and by offset within
    // a block, so a second instance is the whole of what a second document
    // needs. It is here all the same, because BlockEditor installs its block
    // list as the view these answer geometry from — sharing the window's
    // would take those answers away from the note's own editor.
    DocumentBlockDecorations {
        id: docDecorations
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

        documentPath: root.documentPath
        assetRoot: root.assetRoot
        assetSink: root.assetSink
        linkResolver: root.linkResolver

        showGutter: false
        showFindBar: false
        showFormattingBar: false
        showScrollBar: false
        typewriterMode: false
        returnCreatesBlock: !root.returnSubmits
        paragraphPlaceholder: root.placeholderText

        contentMargin: root.contentMargin
        contentTopMargin: root.contentMargin
        // No scrollable space past the last line. The reading default is a
        // third of the viewport, which in a box this size is room to scroll
        // the only line of a message out of sight.
        trailingScrollSpace: 0

        // Enter, reported by the row that declined to make a block for it.
        // Everything else Enter does inside a block is unchanged: it still
        // takes the highlighted entry of an open completion menu, still
        // writes a newline inside a code fence, and Shift+Enter still breaks
        // a line.
        onReturnPressed: root.submitted(root.markdown())
    }

    // A box opens with somewhere to type. An empty BlockModel has no rows at
    // all, and a document with no rows has no caret and no placeholder.
    Component.onCompleted: {
        if (docBlocks.count === 0)
            docBlocks.insertBlock(0, Block.Paragraph, "")
        docUndo.clear()
    }
}
