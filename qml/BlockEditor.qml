// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The DelegateChooser's choices and the Connections below are separate
// component scopes. Binding them lets each address the ids and model roles
// it uses instead of relying on injection.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Qt.labs.qmlmodels
import Kvit 1.0

// The editing surface for one block document: the scrolling list of rows,
// the gestures over it, and the three floating bars that belong to it.
//
// This is the part of the application that is an editor rather than a notes
// app. It was declared inline in main.qml — the ListView in the middle of the
// window and, spread through the eight hundred lines above it, the focus
// lifecycle, the relayout coalescing, the reveal scrolling and the drag and
// selection controllers that make the list an editor. Nothing could
// instantiate it, so nothing outside this application could use it, and two
// documents could not be shown side by side within it.
//
// What it needs from its host is at the top of BlockEditorSurface: where the
// document is stored, where pasted images go, what resolves a wiki link, and
// four hooks for the popup menus the host window owns. Everything else it
// does itself. A host that supplies none of them gets a working editor over
// whatever BlockModel holds, without images-on-paste and without wiki links.
//
// Rows reach back through BlockDelegateBase's `editor` property, which finds
// this object by walking out of the block list, so no delegate names a
// window.
BlockEditorSurface {
    id: editor

    // ---- What the host places around the editor -------------------------

    // Space above the block column, for host chrome drawn across the top of
    // the editor — the tag strip and the document-header extension slot in
    // the notes application. The column starts below it.
    property int contentTopMargin: 20
    // The same for the floating bars, which sit in the top-right corner and
    // clear only the chrome that reaches that corner.
    property int overlayTopMargin: 8
    // Focus mode (§16.1) centers the block column even where the reader has
    // left the maximum content width uncapped. A presentation choice of the
    // host's, so it arrives as a plain flag rather than as the whole view mode.
    property bool focusColumn: false

    // ---- What a compact host switches off --------------------------------
    //
    // A message composer or a capture box is an editor without a pane around
    // it: it is a few lines tall, it grows with what is typed, and it has no
    // room for chrome that belongs to a document being read. Each of these is
    // separate rather than one "compact" flag, because a host may want the
    // formatting bar in a box that has no find bar, and because one flag
    // covering five decisions could not say which. qml/CompactEditor.qml is
    // the composition the notes application and its hosts use; these are what
    // it is composed from.

    // The floating find/replace bar (features.md §7). Nothing opens it in an
    // embedded editor, and an editor with no bar has nothing to open.
    property bool showFindBar: true
    // The floating formatting bar over a completed selection (§9.3).
    property bool showFormattingBar: true
    // The document scrollbar at the right edge. A box that grows with its
    // content and scrolls only past its cap has no use for one.
    property bool showScrollBar: true
    // Whether the wheel is handled by qml/WheelScroller.qml rather than by
    // the flickable itself. That file has what the flickable's own handling
    // did and why this exists; it is a property so the two can be measured
    // against each other, and so a host that wants the old behaviour can ask.
    property bool smoothWheelScrolling: true

    // Space around the block column, inside the editor's own bounds. The
    // reading default is the twenty pixels a page of prose wants on either
    // side of it; a composer gives its text the box instead.
    property int contentMargin: 20
    // Scrollable space past the last block, so the end of a long note can be
    // pulled up into the middle of the window rather than being pinned to its
    // bottom edge. Negative means that reading default, which is a third of
    // the viewport; a compact editor sets 0, since a box whose last line can
    // be scrolled out of sight is a box that looks empty.
    property real trailingScrollSpace: -1
    // The blank-line rhythm between rows. The reading default is the
    // paragraph spacing the reader chose; a preview of a stored version is
    // read at a glance rather than at reading length and turns it down.
    // Negative means that default.
    property real blockSpacing: -1

    // The floating proxy a multi-block drag draws under the pointer. A host
    // that wants it over its own chrome supplies one, because an editor's own
    // layer can only draw within the editor's stacking order; with none the
    // editor makes its own, which is what an embedded editor wants.
    property Item dragLayer: null
    readonly property Item effectiveDragLayer:
        dragLayer ? dragLayer : (ownDragLayer.item as Item)

    // ---- What the host reads --------------------------------------------

    readonly property alias listView: blockListView
    readonly property alias findBar: findBar
    readonly property alias formattingBar: formattingBar
    readonly property alias selectionKeys: selectionKeyHandler
    readonly property alias gapCursor: blockGapCursor
    readonly property alias dropArea: editorDropArea

    // The row the caret is in. The toolbar and the status bar used to work
    // this out for themselves from the window's focus item; it is the
    // editor's own question and this is the one answer.
    caretBlock: {
        // Three reads that exist to make this re-evaluate: the window's focus
        // item, the row that last held it, and the count of popups holding
        // the caret's selection. A popup that is acting on the selection —
        // the colour picker — takes the keyboard from the block by design, so
        // that it can be used without a pointer (accessibility.md Finding 2).
        // That does not mean the block stopped being the one the commands act
        // on: it is the block whose selection the popup is about to change.
        var focusDep = editor.Window.activeFocusItem
        var indexDep = editor.lastFocusedBlock
        var holdDep = editor.selectionHolders
        var item = blockListView.itemAtIndex(indexDep) as BlockDelegateBase
        if (!item)
            return null
        return (item.isFocused || holdDep > 0) ? item : null
    }
    caretBlockIndex: caretBlock ? caretBlock.index : -1

    // ---- What the host is asked ------------------------------------------

    // A paste larger than the open-size cap. It is allowed, but only
    // deliberately, and the question is a modal dialog, which belongs to the
    // window rather than to the editor. `focusLast` says where the caret goes
    // once the text is in: at the end of the inserted run for a paste at the
    // between-block caret, selecting the whole run for a paste over a block
    // selection. A host that leaves this unhandled drops the paste, which is
    // the same outcome as declining the question.
    signal oversizedPasteRequested(string text, int insertAt, bool plain,
                                   bool stripFormatting, bool focusLast)

    // ---- Typewriter scrolling (features.md §16.2) ------------------------

    // Center the caret's line in the viewport. Generalizes the find bar's
    // scroll-into-view to "put the caret line at mid-viewport"; the scroll
    // animates only while typewriter mode is on.
    function centerCaretLine(item) {
        if (!item || !item.rectForMarkdownPosition || !item.markdownCursor)
            return
        var rect = item.rectForMarkdownPosition(item.markdownCursor())
        var yInContent = item.y + rect.y
        var target = yInContent - blockListView.height / 2 + rect.height / 2
        var maxY = Math.max(0, blockListView.contentHeight - blockListView.height)
        blockListView.contentY = Math.max(0, Math.min(target, maxY))
    }
    // When the caret moves to a new block (not just within one), recenter it.
    // onCursorRectangleChanged in the delegate catches within-block moves, but
    // a focus change can settle after that signal, so this covers the handoff.
    onCaretBlockIndexChanged: {
        if (editor.typewriterMode && editor.caretBlockIndex >= 0)
            caretCenterTimer.restart()
    }
    Timer {
        id: caretCenterTimer
        interval: 0
        onTriggered: {
            if (editor.caretBlock)
                editor.centerCaretLine(editor.caretBlock)
        }
    }

    function editorContentY() { return blockListView.contentY }
    function setEditorContentY(value) {
        var maxY = Math.max(0, blockListView.contentHeight
                               + blockListView.bottomMargin
                               - blockListView.height)
        blockListView.contentY = Math.max(0, Math.min(Number(value), maxY))
    }

    // ---- Why these are Timers and not queued calls ----------------------
    //
    // Six things here are deferred to the end of the turn, and each of them
    // used `Qt.callLater`, which is what it is for. A queued call cannot be
    // cancelled, though, and it holds either this object or one of its
    // functions: destroy the editor before the turn ends and the callback
    // runs against something that is half gone —
    //
    //   Property 'recordBuiltBlockHeights' of object BlockEditor is not a
    //   function (exception occurred during delayed function evaluation)
    //
    // which is not a guard anybody can write inside the closure. It never
    // showed while the only editor was the window's, since that one lives as
    // long as the window. A drawn document is created and destroyed as a
    // dialog opens and closes (qml/DocumentView.qml), which is where it
    // started appearing.
    //
    // A Timer belongs to the item, so destroying the item stops it. A
    // zero-interval one fires on the next pass of the event loop, and
    // restarting one that is already running leaves a single pending firing,
    // which is the same compression the queued call gave.

    // ---- Completion-driven block geometry -------------------------------
    // ListView batches model and delegate geometry changes. Every block row
    // reports height changes through BlockDelegateBase; diagrams additionally
    // report their asynchronous render lifecycle. One callLater coalesces all
    // notifications from the turn and forceLayout() processes the outstanding
    // list geometry before focus/reveal consumers read it.
    property bool blockRelayoutScheduled: false
    function blockGeometryChanged(item) {
        // Before the coalescing, not after it: this is where a row's height
        // reaches the document-height table, and all but the first
        // notification of a turn returns on the next line.
        editor.recordBlockHeight(item)
        if (editor.blockRelayoutScheduled)
            return
        editor.blockRelayoutScheduled = true
        blockRelayoutTimer.restart()
    }
    Timer {
        id: blockRelayoutTimer
        interval: 0
        onTriggered: editor.completeBlockRelayout()
    }
    function completeBlockRelayout() {
        editor.blockRelayoutScheduled = false
        blockListView.forceLayout()
        editor.recordBuiltBlockHeights()
        editor.scheduleFocusedBlockPosition()
        editor.scheduleReveal()
    }

    // ---- the document-height table ---------------------------------------
    // What the rows measured, kept per open document so the scrollbar is
    // drawn from a total that settles instead of from the list's own estimate
    // over whichever rows happen to be built. DocumentHeights says why; this
    // is the whole of the collection, since every row already reports its
    // geometry above.
    function recordBlockHeight(item) {
        // Only a row of this list. A row drawn anywhere else — a delegate
        // hosted outside the document view — is a row of some other document.
        var row = item as BlockDelegateBase
        if (!row || row.parent !== blockListView.contentItem)
            return
        // And only at the index it is currently drawing. The delegates are
        // pooled, so a row part-way through being reused could report a
        // height against an index that has stopped being its own, and the
        // list naming some OTHER row at that index is how that shows. A row
        // the list is still building has no index registered yet and the
        // answer is null, which is not a refusal: that is where a row's first
        // measurement comes from, and without it a row built by the last
        // scroll of a read stays estimated.
        var atIndex = blockListView.itemAtIndex(row.index)
        if (atIndex && atIndex !== row)
            return
        editor.heights.recordHeight(row.index, row.height, row.width)
    }
    // Re-measure every row the list currently has built. An edit drops the
    // measurement of the block it changed, and a change that left the row the
    // same height reports no geometry, so without this that block would stay
    // estimated until something else moved it.
    function recordBuiltBlockHeights() {
        if (!blockListView.contentItem)
            return
        var built = blockListView.contentItem.children
        for (var i = 0; i < built.length; ++i)
            editor.recordBlockHeight(built[i])
    }
    // A row's height is only meaningful at the size its text is set at, so a
    // typography change empties the table. A column-width change — the
    // window, the panels, the maximum content width, focus mode — needs no
    // handler: a measurement carries the width it was taken at, and one
    // arriving at a new width empties the table for itself.
    Connections {
        target: Typography
        function onTypographyChanged() { editor.heights.clear() }
    }
    // The gap between two rows is part of the document's height and of every
    // offset in it, and it is not part of any row's own height.
    Binding {
        target: editor.heights
        property: "spacing"
        value: blockListView.spacing
    }

    // ---- Scrolling something into view -----------------------------------
    // Scroll the editor the least it can to put `item` fully inside the
    // viewport, and not at all when it is already there. The target and its
    // containing block stay connected while it owns focus, so growth follows
    // geometry changes rather than an eight-tick settling window. A newer
    // target replaces it; focus loss or manual scrolling cancels it.
    property Item revealTarget: null
    property Item revealBlock: null
    property bool revealScheduled: false
    function containingBlock(item) {
        var candidate = item
        while (candidate && candidate !== blockListView) {
            var block = candidate as BlockDelegateBase
            if (block)
                return block
            candidate = candidate.parent
        }
        return null
    }
    function revealItem(item) {
        if (!item || !blockListView.contentItem)
            return
        editor.revealTarget = item
        editor.revealBlock = editor.containingBlock(item)
        editor.scheduleReveal()
    }
    function scheduleReveal() {
        if (!editor.revealTarget || editor.revealScheduled)
            return
        editor.revealScheduled = true
        revealTimer.restart()
    }
    Timer {
        id: revealTimer
        interval: 0
        onTriggered: {
            editor.revealScheduled = false
            editor.applyReveal()
        }
    }
    function cancelRevealTracking() {
        editor.revealTarget = null
        editor.revealBlock = null
    }
    function applyReveal() {
        var item = editor.revealTarget
        if (!item || !item.visible || !blockListView.contentItem)
            return
        var top = item.mapToItem(blockListView.contentItem, 0, 0).y
        var bottom = top + item.height
        var margin = 16
        var target = blockListView.contentY
        if (bottom + margin > target + blockListView.height)
            target = bottom + margin - blockListView.height
        if (top - margin < target)
            target = top - margin
        if (target === blockListView.contentY)
            return
        // The list's own bottom margin is scrollable space past the last
        // block, so the reveal may use it: a card at the very end of a note
        // has nothing below it to scroll into view otherwise.
        var maxY = Math.max(0, blockListView.contentHeight + blockListView.bottomMargin
                               - blockListView.height)
        blockListView.contentY = Math.max(0, Math.min(target, maxY))
    }
    Connections {
        target: editor.revealTarget
        function onHeightChanged() { editor.scheduleReveal() }
        function onYChanged() { editor.scheduleReveal() }
        function onVisibleChanged() {
            if (editor.revealTarget && editor.revealTarget.visible)
                editor.scheduleReveal()
            else
                editor.cancelRevealTracking()
        }
    }
    Connections {
        target: editor.revealBlock
        function onHeightChanged() { editor.scheduleReveal() }
        function onYChanged() { editor.scheduleReveal() }
    }

    // ---- Focusing a block by index --------------------------------------
    // The editor list is virtualized, so a row only exists once the view has
    // positioned and created it. Requests set currentIndex and position once;
    // currentItemChanged or the delegate's Component.onCompleted then finishes
    // the focus. Once the caret lands, the row's heightChanged signal keeps it
    // contained until focus leaves, the user scrolls, or a newer request wins.
    // The one timer below is only a generous cancellation/error guard.
    property int focusRequestGeneration: 0
    property bool focusRequestPending: false
    property int focusTargetIndex: -1
    property bool focusTargetAtEnd: false
    property string focusTargetTyped: ""
    property BlockDelegateBase focusWatchItem: null
    property bool focusWatchHasFocus: false
    property bool focusPositionScheduled: false
    property bool trackedFocusValidationScheduled: false

    function focusBlockAtIndex(index, atEnd, typed) {
        if (editor.blocks.count === 0)
            return
        var idx = Math.max(0, Math.min(index, editor.blocks.count - 1))
        editor.focusRequestGeneration++
        editor.clearBlockFocusLifecycle()
        editor.focusTargetIndex = idx
        editor.focusTargetAtEnd = atEnd === true
        editor.focusTargetTyped = typed === undefined ? "" : typed
        editor.focusRequestPending = true
        blockFocusGuard.generation = editor.focusRequestGeneration
        blockFocusGuard.restart()
        blockListView.currentIndex = idx
        blockListView.positionViewAtIndex(idx, ListView.Contain)
        editor.applyPendingBlockFocus(blockListView.itemAtIndex(idx))
    }
    function blockDelegateReady(item) {
        if (!editor.focusRequestPending || !item)
            return
        if (item === blockListView.itemAtIndex(editor.focusTargetIndex))
            editor.applyPendingBlockFocus(item)
    }
    function applyPendingBlockFocus(candidate) {
        if (!editor.focusRequestPending)
            return false
        var item = candidate as BlockDelegateBase
        if (!item || item !== blockListView.itemAtIndex(editor.focusTargetIndex))
            item = (blockListView.itemAtIndex(editor.focusTargetIndex)
                    as BlockDelegateBase)
        if (!item)
            return false

        if (editor.focusTargetAtEnd)
            item.focusAtEnd()
        else
            item.focusAtStart()
        if (editor.focusTargetTyped !== "") {
            item.typeText(editor.focusTargetTyped)
            editor.focusTargetTyped = ""
        }

        editor.focusRequestPending = false
        editor.focusWatchItem = item
        editor.focusWatchHasFocus = false
        editor.updateTrackedFocus()
        return true
    }
    function activeFocusIsInside(item) {
        if (!item)
            return false
        var focusItem = editor.Window.activeFocusItem
        while (focusItem) {
            if (focusItem === item)
                return true
            focusItem = focusItem.parent
        }
        return false
    }
    function updateTrackedFocus() {
        var item = editor.focusWatchItem
        if (!item)
            return
        if (editor.activeFocusIsInside(item)) {
            editor.focusWatchHasFocus = true
            blockFocusGuard.stop()
            editor.scheduleFocusedBlockPosition()
        } else if (editor.focusWatchHasFocus) {
            editor.clearBlockFocusLifecycle()
        }
    }
    function scheduleTrackedFocusValidation() {
        if (editor.trackedFocusValidationScheduled)
            return
        editor.trackedFocusValidationScheduled = true
        trackedFocusTimer.restart()
    }
    Timer {
        id: trackedFocusTimer
        interval: 0
        onTriggered: {
            editor.trackedFocusValidationScheduled = false
            editor.updateTrackedFocus()
            if (editor.revealTarget
                    && !editor.activeFocusIsInside(editor.revealTarget))
                editor.cancelRevealTracking()
        }
    }
    // The window's focus item, watched as a property of this editor so the
    // validation below runs without the editor knowing which window it is in.
    readonly property Item windowFocusItem: editor.Window.activeFocusItem
    onWindowFocusItemChanged: editor.scheduleTrackedFocusValidation()

    function scheduleFocusedBlockPosition() {
        if (!editor.focusWatchItem || !editor.focusWatchHasFocus
                || editor.focusPositionScheduled)
            return
        editor.focusPositionScheduled = true
        focusPositionTimer.restart()
    }
    Timer {
        id: focusPositionTimer
        interval: 0
        onTriggered: {
            editor.focusPositionScheduled = false
            if (!editor.focusWatchItem || !editor.focusWatchHasFocus)
                return
            blockListView.forceLayout()
            // Contain shows the whole row where it fits and puts its top at the
            // top of the view where it does not, which for a table is its header.
            blockListView.positionViewAtIndex(editor.focusTargetIndex,
                                              ListView.Contain)
        }
    }
    function clearBlockFocusLifecycle() {
        editor.focusRequestPending = false
        editor.focusWatchItem = null
        editor.focusWatchHasFocus = false
        blockFocusGuard.stop()
    }
    function cancelGeometryTrackingForMovement() {
        if (editor.focusRequestPending || editor.focusWatchItem) {
            editor.focusRequestGeneration++
            editor.clearBlockFocusLifecycle()
        }
        editor.cancelRevealTracking()
    }
    Connections {
        target: editor.focusWatchItem
        function onHeightChanged() { editor.scheduleFocusedBlockPosition() }
    }
    Timer {
        id: blockFocusGuard
        objectName: "blockFocusGuard"
        property int generation: 0
        interval: 5000
        repeat: false
        onTriggered: {
            if (blockFocusGuard.generation !== editor.focusRequestGeneration)
                return
            if (editor.focusRequestPending
                    || (editor.focusWatchItem && !editor.focusWatchHasFocus)) {
                console.warn("Block delegate did not become focus-ready at index "
                             + editor.focusTargetIndex)
                editor.clearBlockFocusLifecycle()
            }
        }
    }

    // Skip-navigation (§14.1): land on the current (or first) block, bypassing
    // whatever chrome the host draws around the editor.
    function focusEditor() {
        editor.focusBlockAtIndex(editor.lastFocusedBlock)
    }

    // Scroll a block to the top of the viewport and focus it — the find bar's
    // scroll-into-view generalized, reused by internal-link navigation and the
    // outline/TOC click-to-scroll.
    function scrollToBlock(idx) {
        if (idx < 0 || !editor.blocks || idx >= editor.blocks.count)
            return
        blockListView.currentIndex = idx
        blockListView.positionViewAtIndex(idx, ListView.Beginning)
        scrollFocusTimer.index = idx
        scrollFocusTimer.restart()
    }
    Timer {
        id: scrollFocusTimer
        property int index: -1
        interval: 0
        onTriggered: {
            var item = (blockListView.itemAtIndex(scrollFocusTimer.index)
                        as BlockDelegateBase)
            if (item && item.focusAtStart)
                item.focusAtStart()
        }
    }

    Connections {
        target: blockListView
        function onCurrentIndexChanged() {
            editor.outline.setCurrentBlock(blockListView.currentIndex)
        }
    }

    // ---- The two gestures over the list ---------------------------------
    // Cross-block text selection and block drag-and-drop are two gestures
    // over the same list, sharing the edge auto-scroller that keeps the
    // pointer's end of the list in view. BlockEditorSurface declares both
    // states because the delegates read them; the behaviour is in the
    // components.
    EdgeAutoScroller {
        id: edgeScroller
        listView: blockListView
    }

    crossBlockDrag: CrossBlockTextDrag {
        editor: editor
        listView: blockListView
        scroller: edgeScroller
    }

    // Reordering blocks by their handle. The delegates and the gap cursor read
    // `blockDrag` to decide whether a press on the strip is a drag at all, so
    // a read-only surface reports none rather than an object that would
    // refuse every gesture. The controller is a plain QtObject and costs
    // nothing to leave built.
    blockDrag: editor.readOnly ? null : blockDragState

    BlockDragController {
        id: blockDragState
        editor: editor
        listView: blockListView
        scroller: edgeScroller
        dragLayer: editor.effectiveDragLayer
        selectionKeys: selectionKeyHandler
    }

    // The editor's own drag proxy, built only when the host supplied none.
    Loader {
        id: ownDragLayer
        active: !editor.dragLayer && !editor.readOnly
        anchors.fill: parent
        z: 1000
        sourceComponent: BlockDragLayer {
            dragState: blockDragState
            listView: blockListView
        }
    }

    // Keys while a block selection is active (features.md §3.1), in
    // BlockSelectionKeys.qml. Entering block selection focuses this item,
    // which blurs the editing block — its reveal collapses and any open block
    // menu dismisses, both intended.
    BlockSelectionKeys {
        id: selectionKeyHandler
        editor: editor
        listView: blockListView
        gapCursor: blockGapCursor
        // Block selection is a mode whose keys are commands — delete,
        // duplicate, indent, paste — so a read-only surface never enters it
        // and the handler never takes the keyboard.
        enabled: !editor.readOnly

        onOversizedPasteRequested: function(text, insertAt, plain) {
            editor.oversizedPasteRequested(text, insertAt, plain, plain, false)
        }
    }

    // The caret between two blocks (features.md §3.7), in BlockGapCursor.qml.
    // A mode of the same shape as block selection above: it takes the focus
    // while it is placed, and typing into it makes the block. It draws in the
    // block list's own seams, so it is suspended while a drag is drawing its
    // drop indicator in them.
    BlockGapCursor {
        id: blockGapCursor
        listView: blockListView
        editor: editor
        dragState: editor.blockDrag
        // The caret between two blocks exists to make one by typing into it.
        enabled: !editor.readOnly
        visible: !editor.readOnly

        onOversizedPasteRequested: function(text, insertAt, plain,
                                            stripFormatting) {
            editor.oversizedPasteRequested(text, insertAt, plain,
                                           stripFormatting, true)
        }
    }

    // Right-click anywhere in a row's gutter opens that block's menu (§9.5).
    //
    // Inside the strip only the drag handle answered a right press, and it is
    // fourteen pixels wide and only there while the pointer is on the row, so
    // the menu was reachable by whoever already knew where it was. This sits
    // at the back of the list's content item — the same stacking the gap
    // cursor's hover probe uses (BlockGapCursor.qml) — because Qt offers a
    // press to the items in front first: a press on a block's own chrome or
    // text never reaches here, and one on the strip does, for all twelve
    // block kinds without each growing a handler of its own.
    //
    // The strip is 44px wide and an indented row's content starts one indent
    // step further in again, so the band widens with the indent. The indent
    // is read from the model rather than the row because it is the twelve
    // delegate types that carry it, not the interface the editor sees.
    MouseArea {
        objectName: "gutterMenuArea"
        parent: blockListView.contentItem
        z: -1
        width: blockListView.width
        height: blockListView.contentHeight
        acceptedButtons: Qt.RightButton
        // Nothing to open in an editor that draws no strip, and no band it
        // would be in: the rows start at the left edge there, so the same
        // press is a press on a block's own text.
        enabled: editor.showGutter && !editor.readOnly
        onPressed: function(mouse) {
            var idx = blockListView.indexAt(Math.max(1, mouse.x), mouse.y)
            var block = idx >= 0 ? editor.blocks.blockAt(idx) : null
            if (!block || mouse.x >= 44 + block.indentLevel * 24) {
                mouse.accepted = false
                return
            }
            AppActions.requestBlockHandleMenu(blockListView.itemAtIndex(idx))
        }
    }

    // ---- The document itself ---------------------------------------------

    // A press that no block claimed (margins, the gap between blocks, below
    // the last block) ends any document-level selection (§3.1
    // clicking-elsewhere behavior).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: function(mouse) {
            if (editor.selection.hasBlockSelection
                || editor.selection.hasTextSelection)
                editor.selection.clear()
            mouse.accepted = false
        }
    }

    // External-drag ingestion (§5.4), in EditorDropArea.qml.
    // Files and text dropped onto the editor become blocks, which is a change
    // to the document.
    EditorDropArea {
        id: editorDropArea
        objectName: "editorDropArea"
        anchors.fill: scrollView
        z: 40
        enabled: !editor.readOnly
        editor: editor
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
            if (max <= 0 && editor.focusColumn)
                max = 760
            if (max <= 0)
                return 0
            return Math.max(0, Math.floor(
                (parent.width - 2 * editor.contentMargin - max) / 2))
        }

        anchors.fill: parent
        anchors.margins: editor.contentMargin
        anchors.leftMargin: editor.contentMargin + centeringMargin
        anchors.rightMargin: editor.contentMargin + centeringMargin
        anchors.topMargin: editor.contentTopMargin

        // A Flickable does not clip unless told to, and rows scrolled just
        // past the top of the viewport stay instantiated (cacheBuffer
        // below), so they painted over the tag strip and on up over the
        // toolbar — the editor pane is declared after the toolbar, so it
        // wins the overlap. Every other scrolling surface in the app
        // already clips; the popups a block raises are window-level items,
        // so none of them are clipped by this.
        clip: true

        contentWidth: availableWidth

        // The two bars a ScrollView draws for itself are off, and the
        // list below attaches none of its own: the editor's vertical bar
        // is the DocumentScrollBar declared after this view, which is
        // drawn from the document-height table rather than from the
        // list's estimate of how tall the document is. The horizontal one
        // has never had anything to scroll — the content is exactly as
        // wide as the viewport — and it is switched off here so that
        // nothing draws over the vertical one's foot.
        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ListView {
            id: blockListView
            objectName: "blockListView"

            width: parent.width
            // Blank-line rhythm between blocks (§10.2).
            spacing: editor.blockSpacing >= 0 ? editor.blockSpacing
                                              : Typography.paragraphSpacing

            reuseItems: true
            // Keep a small offscreen row window warm for ordinary
            // wheel/flick movement without making startup instantiate a
            // large variable-height document through the buffer.
            cacheBuffer: 240

            // Scrollable space past the last block, so the end of a note
            // can be pulled up into the middle of the window instead of
            // being pinned to its bottom edge. The last block was where
            // the reader had the least room to work: a task-board card
            // clicked there grows a description field below the window's
            // edge, and with the document ending exactly at that edge
            // there was nothing to scroll to. It is a scroll range, not a
            // row and not content height, so what the seam cursor and the
            // block list measure themselves against is unchanged.
            bottomMargin: editor.trailingScrollSpace >= 0
                          ? editor.trailingScrollSpace
                          : Math.max(120, Math.round(height * 0.35))

            // §16.2 typewriter mode: caret-line centering scrolls smoothly.
            // The animation is enabled only in typewriter mode so ordinary
            // scrolling, find-bar jumps, and drag auto-scroll are unchanged.
            Behavior on contentY {
                // Typewriter scroll honors reduced motion (§14.3): 0
                // duration stills it instantly.
                //
                // And not while the wheel is being turned. WheelScroller
                // writes contentY once a frame and reads back what it wrote
                // to tell its own movement from anybody else's; an animation
                // between the write and the value means it never reads back
                // what it wrote, so it concludes something else has taken the
                // view and gives up. Typewriter mode would otherwise be the
                // one mode in which the wheel does nothing.
                enabled: editor.typewriterMode && Theme.motionScale > 0
                         && !wheelScroller.chasing
                NumberAnimation { duration: 130 * Theme.motionScale
                                  easing.type: Easing.OutQuad }
            }

            model: editor.blocks

            // ---- geometry answers for a linked module's decorations ----
            //
            // A module that draws between or beside the blocks needs
            // positions to implement a scroll policy of its own, and a
            // position only exists once Qt Quick has laid the row out.
            // DocumentDecorations forwards its three geometry questions
            // here, to the object that has the laid-out rows; the
            // rectangles are in this list's content coordinates, which is
            // the space contentY moves through. A row outside the window
            // of rows the list keeps alive answers with a null rectangle,
            // which is the same answer as "no such block".
            Component.onCompleted: editor.decorations.setDocumentView(blockListView)

            function decorationBlockGeometry(blockIndex) {
                var row = (blockListView.itemAtIndex(blockIndex)
                           as BlockDelegateBase)
                if (!row)
                    return Qt.rect(0, 0, 0, 0)
                return Qt.rect(row.x, row.y, row.width, row.height)
            }
            function decorationLineGeometry(blockIndex, line) {
                var row = (blockListView.itemAtIndex(blockIndex)
                           as BlockDelegateBase)
                if (!row)
                    return Qt.rect(0, 0, 0, 0)
                return Qt.rect(row.x, row.y + row.lineTop(line),
                               row.width, row.lineHeightAt(line))
            }
            // Where a marked run of characters is drawn: one rectangle
            // per visual line it crosses, since a marked phrase that
            // wraps is in two places. The row that holds the span works
            // them out in its own coordinates and this lifts them into
            // the list's content coordinates, as with a container.
            function decorationSpanRects(id) {
                for (var i = 0; i < editor.blocks.count; ++i) {
                    var spanRow = (blockListView.itemAtIndex(i)
                                   as BlockDelegateBase)
                    if (!spanRow)
                        continue
                    var rects = spanRow.decorationSpanRects(id)
                    if (rects.length === 0)
                        continue
                    var out = []
                    for (var j = 0; j < rects.length; ++j) {
                        out.push(Qt.rect(spanRow.x + rects[j].x,
                                         spanRow.y + rects[j].y,
                                         rects[j].width, rects[j].height))
                    }
                    return out
                }
                return []
            }
            function decorationContainerGeometry(id) {
                for (var i = 0; i < editor.blocks.count; ++i) {
                    var row = (blockListView.itemAtIndex(i)
                               as BlockDelegateBase)
                    if (!row)
                        continue
                    var box = row.decorationContainerRect(id)
                    // A container is as wide as the row, so a zero width
                    // is how the row says it is not drawing this one.
                    if (box.width > 0) {
                        return Qt.rect(row.x + box.x, row.y + box.y,
                                       box.width, box.height)
                    }
                }
                return Qt.rect(0, 0, 0, 0)
            }

            // Delegate readiness completes pending focus without polling.
            // Geometry-driven reveal/focus tracking yields immediately to
            // the reader as soon as they start moving the view themselves.
            onCurrentItemChanged: editor.blockDelegateReady(currentItem)
            onMovementStarted: editor.cancelGeometryTrackingForMovement()
            onContentHeightChanged: editor.scheduleReveal()

            // One delegate per block type; paragraphs and headings
            // share the default text choice.
            // The chooser watches delegateKind, not blockType: it
            // recreates a row's delegate whenever the watched role
            // changes, and heading conversions must not drop focus.
            delegate: DelegateChooser {
                id: blockDelegateChooser
                role: "delegateKind"

                // One DelegateChoice per registered kind, built when the
                // block list is created. A kind reaches the screen by
                // being registered — the same rule for a built-in kind
                // and for one a linked module added.
                //
                // This was seventeen DelegateChoice blocks written out
                // here, each pairing a kind number with a QML file, and
                // nothing checked that a kind had one: a kind whose
                // choice nobody remembered to add drew an empty row and
                // said nothing about it.
                //
                // The order matters. DelegateChooser takes the FIRST
                // choice whose roleValue matches, and a choice with no
                // roleValue matches everything — so every choice built
                // here names its kind, and none can shadow another.
                Component.onCompleted: {
                    var choices = BlockKindRegistry.delegateChoices()
                    for (var i = 0; i < choices.length; ++i) {
                        var entry = choices[i]
                        // Parented to the chooser, which is what keeps it
                        // alive. A component created with no parent is
                        // owned by JavaScript, and once this loop's local
                        // goes out of scope the collector is free to take
                        // it — leaving the chooser holding a freed
                        // component and crashing on the next row it
                        // builds, which is a scroll or two later.
                        var component = Qt.createComponent(
                            entry.delegateUrl, Component.PreferSynchronous,
                            blockDelegateChooser)
                        if (component.status !== Component.Ready) {
                            console.warn("block kind '" + entry.id
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
            }

            // Do not animate displaced rows. Delegates such as Mermaid
            // diagrams acquire their final height asynchronously, after
            // insertion. A displaced y-transition keeps the following
            // rows at positions calculated from the delegate's temporary
            // height and can leave them overlapped after the animation.
            // Let ListView track changing delegate heights directly.

            // The one positional animation in the editor, and the
            // category reduced-motion settings exist for: every block
            // insert, delete and reorder slides the rows below it. It is
            // the first thing that has to go still when the setting is on
            // (accessibility.md Finding 5).
            move: Transition {
                NumberAnimation {
                    properties: "y"
                    duration: 200 * Theme.motionScale
                    easing.type: Easing.OutQuad
                }
            }

            // Add remove animation for visual feedback
            remove: Transition {
                NumberAnimation {
                    property: "opacity"
                    to: 0
                    duration: 150 * Theme.motionScale
                }
            }

            // No fade-in for an inserted row, for the same reason there is
            // no displaced transition above, and it is worth stating
            // exactly because the animation itself was harmless.
            //
            // While a ListView is running one of its own add transitions
            // it drops any size change a delegate reports, and it never
            // revisits it: the rows below stay where the shorter delegate
            // put them, and forceLayout() afterwards does nothing, because
            // the view has nothing recorded to lay out. The window is only
            // the 150 ms of the fade, but that is precisely when a row
            // that acquires its height asynchronously — a Mermaid diagram,
            // an image, a decoration a module draws as a note opens — is
            // most likely to grow, and the result is a row drawn over the
            // one below it until the reader happens to edit something.
            //
            // Measured directly (tests/test_decorationshell.cpp): with the
            // transition present a row that grew during it stayed
            // overlapped through any number of frames and any number of
            // forceLayout() calls; without it the view re-placed the rows
            // below within the frame.
        }
    }

    // How the wheel moves the list. Declared outside the ScrollView so that
    // it is not reparented into the flickable's content item; the handler
    // inside it attaches itself to the list. WheelScroller says what the
    // flickable's own handling did and why this replaces it.
    WheelScroller {
        id: wheelScroller
        objectName: "editorWheelScroller"
        listView: blockListView
        enabled: editor.smoothWheelScrolling
    }

    // The document's scrollbar, at the right edge of the scrolling area
    // and over it, which is where the bar the ScrollView draws for itself
    // sits. Outside the ScrollView rather than attached to it, because an
    // attached bar is driven from C++ against the flickable's own
    // estimate; DocumentScrollBar says what that costs the reader.
    DocumentScrollBar {
        editor: editor
        listView: blockListView
        wheelScroller: wheelScroller
        visible: editor.showScrollBar
        anchors.top: scrollView.top
        anchors.bottom: scrollView.bottom
        anchors.right: scrollView.right
    }

    // The floating formatting bar (features.md §9.3): overlays the caret's
    // selection. Placed after the ScrollView so presses on it never reach the
    // selection-clearing MouseArea.
    FormattingBar {
        id: formattingBar
        editor: editor
        available: editor.showFormattingBar
        target: editor.caretBlock
        listView: blockListView
    }

    // The floating find/replace bar (features.md §7): overlays the editor's
    // top-right corner, so opening it reflows nothing.
    FindBar {
        id: findBar
        editor: editor
        available: editor.showFindBar
        listView: blockListView
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        anchors.topMargin: editor.overlayTopMargin
    }
}
