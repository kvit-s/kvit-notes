// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// The editing surface, as a block delegate sees it.
//
// A row of the document is drawn inside a BlockEditor, and it has to ask that
// editor several questions: is a drag running, which row last held the caret,
// is one of the completion menus open for me. Until this type existed the
// answers came from the window — `Window.window as KvitShell` — which made
// every one of those reads a question about the application rather than about
// the editor the row is in. An editor embedded in some other window, or a
// second one beside the first, could not answer them at all.
//
// So the surface is named here and BlockEditor implements it. A delegate says
//
//     readonly property BlockEditorSurface editor: ...   // BlockDelegateBase
//
// once, and then reads `editor.blockDrag.active` like any other typed
// expression. BlockDelegateBase does that lookup for every row by walking out
// of the block list, so no delegate names a window.
//
// Only the interface lives here; BlockEditor.qml holds the behaviour. That is
// the arrangement BlockDragState already has with the same delegates, and it
// is what keeps this file free of any reference to a delegate: BlockEditor
// casts rows to BlockDelegateBase, BlockDelegateBase names this type, and
// neither file names the other.
//
// This is deliberately the small surface delegates actually use, not
// everything BlockEditor has. Anything added here becomes something fourteen
// files may depend on, so the cost of a new entry is higher than it looks.
Item {
    // ---- The document being edited ---------------------------------------
    //
    // The eight objects that hold this document's state, rather than the
    // application's. Every one of them was reached by its singleton name from
    // inside the rows — `BlockModel.insertBlock(...)`, `DocumentSelection.clear()`
    // — which meant a row did not edit the document it was drawn in, it edited
    // the one the window happened to have. Two editors side by side in one
    // window would have shared all eight, and an editor embedded somewhere
    // else could only ever show that process's single document.
    //
    // Each defaults to the singleton, so an editor nobody configures behaves
    // exactly as the application always has: one document per window, reached
    // through the per-vault instances AppContext builds. A host that wants a
    // second document assigns its own.
    property BlockModel blocks: BlockModel
    property DocumentSelection selection: DocumentSelection
    property UndoStack undoStack: UndoStack
    property DocumentSearch search: DocumentSearch
    property DocumentOutline outline: DocumentOutline
    property DocumentHeights heights: DocumentHeights
    property DocumentStats stats: DocumentStats
    property DocumentDecorations decorations: DocumentDecorations

    // Block drag-and-drop. See BlockDragState for what the delegates read.
    property BlockDragState blockDrag: null
    // The cross-block text drag, which is a different gesture with its own
    // state; delegates only ask whether one is running.
    property QtObject crossBlockDrag: null

    // Which row the caret is in, and which row last had it. `caretBlock` and
    // `caretBlockIndex` are live; `lastFocusedBlock` survives focus leaving
    // the editor, which is what lets focus return to the right place.
    //
    // The row as an object is what the formatting commands act on, so the
    // toolbar and the status bar read it too, and the editor is the one place
    // it is worked out.
    property var caretBlock: null
    property int caretBlockIndex: -1
    property int lastFocusedBlock: 0

    // Typewriter scrolling (features.md §10.4): the caret line is kept
    // vertically centred. Delegates check it before scrolling themselves.
    // A view mode rather than an editor mode, so the host sets it.
    property bool typewriterMode: false

    // How many open popups are acting on the caret's text selection. A block
    // that loses focus drops its selection, which is the right default; a
    // keyboard-navigable colour picker takes focus by definition, and the
    // selection it is about to recolour must survive that. Each such popup
    // raises this while it is open and lowers it on close, so the count is
    // what the block consults rather than a list of popup types it would
    // otherwise have to know about (accessibility.md Finding 2).
    property int selectionHolders: 0

    // ---- Where this document lives --------------------------------------
    //
    // Three questions a row asks about the world outside the document, and
    // the whole of what the editor needs from a notes collection. Each has a
    // default that means "there is no collection", so an editor embedded
    // somewhere without one still runs: a pasted image is refused rather than
    // written to a folder nobody chose, and a [[wiki link]] styles as an
    // ordinary link rather than as one that failed to resolve.

    // The file this document is stored in, or "" when it has none. A relative
    // image path is resolved against the folder holding it, and an image
    // pasted into the document is named after it.
    property string documentPath: ""

    // The folder that images and other pasted files are stored under, or ""
    // to store them beside the document. In the notes application this is the
    // vault root, which is what makes one note's picture visible to another.
    property string assetRoot: ""

    // What turns pasted or dropped bytes into a file on disk. Null refuses
    // the paste, which is the right answer for a host that has nowhere to put
    // it. Typed, because qmllint checks every member read off it and this
    // tree carries no suppressions for its own code; the editor is still told
    // WHICH store to use rather than reaching the process-wide one, which is
    // the whole of what it needed.
    property AssetStore assetSink: null

    // What a [[wiki link]] resolves against. Null leaves wiki links
    // unresolved and styled as plain links, which is what an editor with no
    // collection behind it should draw.
    property NoteCollection linkResolver: null

    // The folder holding `documentPath`, and a file-name slug taken from it.
    // Both are derived here rather than in each caller because a row pasting
    // an image and the drop area receiving one have to agree on them, and
    // they were the same six lines written out twice before.
    readonly property string documentDirectory: {
        var slash = documentPath.lastIndexOf("/")
        return slash >= 0 ? documentPath.substring(0, slash) : ""
    }
    readonly property string documentSlug: {
        var name = documentPath.substring(documentPath.lastIndexOf("/") + 1)
                               .replace(/\.[^.]+$/, "")
        var slug = name.toLowerCase().replace(/[^a-z0-9]+/g, "-")
                       .replace(/^-+|-+$/g, "")
        return slug === "" ? "image" : slug
    }

    // The completion menus a delegate asks the editor about rather than
    // reaching for. Each returns the menu OBJECT while it is open for the
    // given block or editor, else null; the caller then drives that object
    // (highlight, apply, dismiss) through an untyped local, which is the one
    // part of this boundary a menu type would type and is not worth a fourth
    // extracted type for these few call sites. The menus are popups, so the
    // host window owns them and overrides these to answer from them; the null
    // bodies are what an editor with no menus reports.
    //
    // This is the AppActions principle turned around: commands go out through
    // AppActions, and these queries come back through here, so a delegate
    // never holds the menu object except as the opaque thing it drives.
    function activeBlockMenu(index) { return null }
    function activeMathMenu(host) { return null }
    function activeWikiMenu(host) { return null }

    // List-delegate lifecycle hooks. BlockEditor overrides these to coalesce
    // variable-height relayouts and to finish a focus request once a
    // virtualized row exists. Keeping the hooks on the typed boundary lets
    // BlockDelegateBase report its own lifecycle without reaching into a
    // particular ListView.
    function blockGeometryChanged(item) {}
    function blockDelegateReady(item) {}

    // Focus a model row after bringing it into the virtualized list. Blocks
    // that create another row (for example Ctrl+Enter out of a diagram) use
    // the same readiness-driven path as insert dialogs and navigation.
    function focusBlockAtIndex(index, atEnd, typed) {}

    // Put a row at the top of the viewport and focus it: what an internal
    // link, an outline click and a search result all arrive at.
    function scrollToBlock(index) {}

    // Whether a context menu is open holding `target`'s selection — a bool,
    // so it is a plain typed query. The menus belong to the host window, so
    // it answers this.
    function contextMenuHoldsSelection(target) { return false }

    // Open a link, or fall back to the external opener when this editor's
    // host has none. Returns whether the host handled it, so the caller can
    // choose the fallback without reaching for the opener object.
    function openLink(url) { return false }
}
