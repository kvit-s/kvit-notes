// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The decoration Repeaters below are separate component scopes. Binding them
// lets each address this row by id instead of relying on injection.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Kvit 1.0

// What every block delegate provides to the shell.
//
// This interface is not new. All twelve delegates already implement exactly
// these eight functions, and QueryBlock.qml described the set as "matches the
// other wave-2 blocks" — a convention held together by hand, with nothing
// enforcing it and nothing recording it. Writing it down does not add
// coupling; it names coupling that was already there.
//
// The shell reaches a row as `blockListView.itemAtIndex(i)`, which is typed
// QQuickItem, so before this every call had to be written as
//
//     if (item && item.focusAtStart)
//         item.focusAtStart()
//
// — a probe, because nothing could say whether the function was there. With
// the row typed as this component the members resolve, qmllint checks them,
// and a delegate that forgets one is a problem at build time rather than a
// guard that quietly does nothing at runtime.
//
// The bodies below are the neutral answers a delegate with no text gives.
// They are deliberately not abstract: DividerDelegate and MediaBlock have
// nothing to focus, and inheriting a sane default is better than each
// repeating an empty implementation to satisfy an interface.
Item {
    id: blockDelegateBase

    // Which row of BlockModel this is.
    //
    // Every delegate declared this for itself, because every delegate needs
    // it. It moved here with the decoration seam below, which has to know
    // which block a row is drawing and cannot read a property declared by the
    // type deriving this one. A view fills a required property from the model
    // role of the same name, so no delegate and no call site changed.
    required property int index

    // Variable-height rows tell their own shell whenever their geometry
    // changes. The shell coalesces all notifications in the current event turn
    // before asking ListView to process outstanding layout. This also gives
    // specialized delegates (notably asynchronous diagrams) an explicit hook
    // for completion signals that do not themselves change height.
    readonly property KvitShell geometryShell: Window.window as KvitShell
    function notifyShellGeometryChanged() {
        if (blockDelegateBase.geometryShell)
            blockDelegateBase.geometryShell.blockGeometryChanged(blockDelegateBase)
    }
    onHeightChanged: blockDelegateBase.notifyShellGeometryChanged()
    Component.onCompleted: {
        blockDelegateBase.notifyShellGeometryChanged()
        if (blockDelegateBase.geometryShell)
            blockDelegateBase.geometryShell.blockDelegateReady(blockDelegateBase)
    }
    // A row taken out of the pool is now drawing a different block, and if
    // that block is the same height as the one it drew before — which in a
    // note of repeating shapes is most of the time — nothing about its
    // geometry changes and the two handlers above say nothing. The row is a
    // new measurement all the same, because it is a measurement OF ANOTHER
    // BLOCK, and without this the last rows a reader scrolls to are the ones
    // the shell never hears about. The model properties are updated before
    // this runs, so the index it reports is the one it is now drawing.
    ListView.onReused: blockDelegateBase.notifyShellGeometryChanged()

    // Standard context-menu keys, shared by every block's primary focus
    // target. Returning true lets each delegate put this first in its own key
    // handler without duplicating the gesture or the AppActions route.
    function handleContextMenuKey(event) {
        if (event.key === Qt.Key_Menu
            || (event.key === Qt.Key_F10
                && (event.modifiers & Qt.ShiftModifier))) {
            AppActions.requestBlockHandleMenu(blockDelegateBase)
            event.accepted = true
            return true
        }
        return false
    }

    // A block row fills the list it is in, less the reserved margin column
    // (which is zero wide unless a module asked for it).
    //
    // main.qml used to say this per delegate — `width: blockListView.width`
    // repeated on all seventeen DelegateChoice blocks — and a choice the
    // shell builds at runtime from the kind registry cannot carry a binding
    // written by hand. The row knows it fills its view, so it says so once,
    // here, and every delegate inherits it.
    width: ListView.view
        ? Math.max(1, ListView.view.width - blockDelegateBase.marginColumnWidth)
        : implicitWidth

    // ---- what a linked module may draw here -----------------------------
    //
    // A module can ask for QML of its own to be drawn after this row, and for
    // glyphs in the column reserved beside it; DocumentDecorations holds the
    // registrations and the reasoning. Both are RENDERED and never inserted:
    // the document model is untouched, so this row's index is the index it
    // would have had, and nothing drawn here reaches the note's undo stack.
    //
    // Only a row the view itself created decorates. These delegate types also
    // nest inside one another — TextBlockDelegate builds an EditableBlock for
    // the same block — and without this test the container registered after
    // block 4 would be drawn once per level of nesting.
    readonly property bool isDocumentRow: ListView.view !== null

    // The reserved column at the right edge of the document, in pixels: a
    // fixed measure of the reading font, so it holds a glyph at any text
    // size. It is zero, and this row is exactly as wide as the view, until a
    // module reserves the column — which it does once, at startup, rather
    // than when its first glyph appears, so no text ever shifts sideways
    // under the reader.
    readonly property real marginColumnWidth:
        DocumentDecorations.marginColumnReserved
            ? Math.round(Typography.baseSize * DocumentDecorations.marginColumnEms)
            : 0

    // The registrations that land on this row. Both read `revision` first:
    // a bare method call subscribes to nothing, so without it a container
    // added after this row was built would never appear. Same idiom as the
    // search and selection reads in the delegates.
    readonly property var containerEntries: {
        var revision = DocumentDecorations.revision
        if (!blockDelegateBase.isDocumentRow || !DocumentDecorations.active)
            return []
        return DocumentDecorations.containersAfter(blockDelegateBase.index)
    }
    readonly property var marginEntries: {
        var revision = DocumentDecorations.revision
        if (!blockDelegateBase.isDocumentRow || !DocumentDecorations.active)
            return []
        return DocumentDecorations.marginItemsForBlock(blockDelegateBase.index)
    }

    // How tall this row's own content is. Every delegate binds this, and none
    // binds implicitHeight, because the row's implicit height is its content
    // plus whatever a module is drawing after it — which is what makes a
    // container occupy space BETWEEN two blocks rather than overlap the one
    // below.
    //
    // It has to be implicitHeight that carries the sum. A ListView repositions
    // the rows below one that grew when the row's IMPLICIT height changes;
    // measured here, a row that changed only its `height` was left overlapping
    // its neighbour, and a forceLayout() afterwards did not correct it because
    // the view had never registered anything to lay out.
    property real blockContentHeight: 0
    implicitHeight: blockDelegateBase.blockContentHeight + decorationColumn.height

    // What a decorated item is told about where it landed. A module's QML
    // declares either, both or neither of these; an item that declares
    // neither is left alone.
    function applyDecorationContext(item, entry) {
        if (!item || !entry)
            return
        if (item.decorationContext !== undefined)
            item.decorationContext = entry.context
        if (item.decorationBlock !== undefined)
            item.decorationBlock = blockDelegateBase.index
    }

    // ---- per-line geometry ----------------------------------------------
    //
    // Where the rendered text of visual line `line` (counting from zero)
    // sits inside this row. A margin glyph is addressed by block AND line, so
    // this is what puts it beside the third line of a wrapped paragraph
    // rather than beside the paragraph.
    //
    // A row whose text is one wrapped text object fills these three in and
    // gets exact positions, because within one block every visual line is the
    // same height. A row with no text of its own — a divider, a media card —
    // leaves textLineHeight at zero, and every line then resolves to the top
    // of the row, which is the honest answer for something with no lines.
    property real textLineOrigin: 0
    property real textLineHeight: 0
    property int textLineCount: 0

    function lineTop(line) {
        if (blockDelegateBase.textLineHeight <= 0)
            return blockDelegateBase.textLineOrigin
        var last = Math.max(0, blockDelegateBase.textLineCount - 1)
        var clamped = Math.max(0, Math.min(line, last))
        return blockDelegateBase.textLineOrigin
            + clamped * blockDelegateBase.textLineHeight
    }
    // How tall line `line` is. Uniform within a row, so the argument is there
    // for a delegate that one day has lines of differing heights rather than
    // because this implementation needs it.
    function lineHeightAt(line) {
        return blockDelegateBase.textLineHeight > 0
            ? blockDelegateBase.textLineHeight
            : blockDelegateBase.blockContentHeight
    }

    // Where container `id` was drawn, in this row's coordinates, or a
    // zero-width rectangle when this row is not drawing it. The shell asks
    // each instantiated row in turn; see main.qml's
    // decorationContainerGeometry.
    function decorationContainerRect(id) {
        var rows = decorationColumn.children
        for (var i = 0; i < rows.length; ++i) {
            var loader = rows[i]
            if (loader && loader.modelData && loader.modelData.id === id) {
                return Qt.rect(loader.x, decorationColumn.y + loader.y,
                               loader.width, loader.height)
            }
        }
        return Qt.rect(0, 0, 0, 0)
    }

    // Where a module's marked span is drawn inside this row, one rectangle
    // per visual line it crosses. A row with no text of its own has no
    // characters to mark and answers with nothing, which is also the answer
    // for a span belonging to some other row.
    function decorationSpanRects(id) { return [] }

    // The containers drawn after this row, stacked in registration order so
    // two modules contributing after the same block get a stable order rather
    // than one hiding the other.
    Column {
        id: decorationColumn
        objectName: "blockDecorations"
        y: blockDelegateBase.blockContentHeight
        width: blockDelegateBase.width
        // Above the row's own chrome. A delegate's press handler and its
        // hover and selection tints are anchored to the whole row, and the
        // row is now taller than its text; stacking the containers over them
        // is what sends a press inside a container to the container. The
        // tints do reach behind it, which is the one visible consequence of
        // the row owning the space rather than the list.
        z: 2

        Repeater {
            model: blockDelegateBase.containerEntries
            delegate: Loader {
                id: containerLoader
                required property var modelData
                objectName: "blockDecorationContainer"
                width: decorationColumn.width
                source: containerLoader.modelData.source
                // Sized by its own content, so a container whose QML reports
                // no implicit height occupies no vertical space at all.
                height: item ? (item as Item).implicitHeight : 0
                onLoaded: blockDelegateBase.applyDecorationContext(
                              containerLoader.item, containerLoader.modelData)
            }
        }
    }

    // The reserved column beside this row. The core owns the column — that it
    // exists, how wide it is, and that it clips — while the module owns what
    // is drawn in it and where: one item per line, several on one line, or
    // none at all is the module's policy, not the editor's.
    Item {
        id: marginColumn
        objectName: "blockMarginColumn"
        visible: blockDelegateBase.isDocumentRow
                 && DocumentDecorations.marginColumnReserved
        x: blockDelegateBase.width
        width: blockDelegateBase.marginColumnWidth
        height: blockDelegateBase.blockContentHeight
        clip: true
        z: 2

        Repeater {
            model: blockDelegateBase.marginEntries
            delegate: Loader {
                id: marginLoader
                required property var modelData
                objectName: "blockMarginItem"
                width: marginColumn.width
                y: blockDelegateBase.lineTop(marginLoader.modelData.line)
                height: blockDelegateBase.lineHeightAt(marginLoader.modelData.line)
                source: marginLoader.modelData.source
                onLoaded: blockDelegateBase.applyDecorationContext(
                              marginLoader.item, marginLoader.modelData)
            }
        }
    }

    // ---- focus entry points ----
    // Where the caret goes when the shell moves focus into this block.
    function focusAtStart() {}
    function focusAtEnd() {}
    // `markdownPos` is an offset into the block's markdown source, not into
    // the rendered text; the two differ wherever markers are hidden.
    function focusAtPosition(markdownPos) {}

    // ---- hit testing and caret geometry ----
    // The markdown offset under a point in scene coordinates.
    function markdownPositionAt(sceneX, sceneY) { return 0 }
    // Whether a scene point is over text this block would take a caret in,
    // which is how the shell decides between placing a caret and starting a
    // block selection.
    function pointInText(sceneX, sceneY) { return false }
    // The markdown offset one display line up (dir < 0) or down (dir > 0)
    // from `mdPos`, or -1 when the step leaves this block — which is the
    // shell's signal to move to the next one.
    function lineStepPosition(mdPos, dir) { return -1 }
    // The offset the caret takes when arriving from another block at
    // horizontal position `x`, entering from the top or the bottom.
    function entryPositionAtX(x, fromTop) { return 0 }
    // The inverse of entryPositionAtX: the x a caret at `mdPos` sits at, so
    // vertical movement can keep its column across blocks.
    function xAtMarkdown(mdPos) { return 0 }

    // Paint this row's share of a cross-block text range that a mouse drag
    // has just finished, when the row is the one the drag started in. The
    // shell calls it on the anchor row at the release; a row with no text
    // has no share to paint, which is what the default here says.
    function reapplySelectionPortion() {}

    // The slash / block menu, anchored at this block's caret. Only the text
    // delegates raise it — a divider or a media card has no caret to anchor
    // to — so callers guard on it and the default here does nothing, which
    // is what those callers already expected to happen.
    function openBlockMenu(mode) {}

    // Put text in at the caret as though it had been typed there. The
    // distinction from BlockModel.updateContent is the point: an edit made
    // through the editor raises the engine's edited signal, which is what the
    // slash menu and the markdown prefix conversions hang off, and a model
    // write does not. The gap cursor (§3.7) uses it to hand the character
    // that asked for a block to the block it just made. Same story as the
    // menu above: only the text delegates have anywhere to put it.
    function typeText(text) {}
}
