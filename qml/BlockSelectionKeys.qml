// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// The keyboard while whole blocks are selected (features.md §3.1).
//
// Block selection is a mode: this item takes focus when it begins, which is
// what collapses the editing block's reveal and dismisses any open block
// menu. Everything the mode can do is here — move and extend the selection,
// move, duplicate, indent and delete the selected blocks, copy or cut them as
// structural markdown, paste after them, and leave the mode back into text
// editing. Printable keys are deliberately inert: typing never replaces a
// block selection.
//
// The right-click selection menu drives the same functions, so the two paths
// cannot diverge.
Item {
    id: keys
    objectName: "selectionKeyHandler"

    // Wired by main.qml.
    property var listView

    // A paste larger than the open-file cap needs the window's confirmation
    // dialog, which does the insert itself once the user agrees.
    signal oversizedPasteRequested(string text, int insertAt, bool plain)

    // Leave selection mode and edit the given block.
    function exitToBlock(idx) {
        DocumentSelection.clear()
        if (idx < 0 || idx >= BlockModel.count)
            idx = Math.max(0, Math.min(keys.listView.currentIndex,
                                       BlockModel.count - 1))
        keys.listView.currentIndex = idx
        var item = (keys.listView.itemAtIndex(idx) as BlockDelegateBase)
        if (item)
            item.focusAtEnd()
    }

    function revealSelectionEdge() {
        var idx = DocumentSelection.lastActiveIndex()
        if (idx >= 0) {
            keys.listView.currentIndex = idx
            keys.listView.positionViewAtIndex(idx, ListView.Contain)
        }
    }

    // Focus a block on the next tick (delegates may need a frame
    // after a structural change).
    function focusBlockLater(idx, atEnd) {
        Qt.callLater(function() {
            if (BlockModel.count === 0)
                return
            var i = Math.max(0, Math.min(idx, BlockModel.count - 1))
            keys.listView.currentIndex = i
            var item = (keys.listView.itemAtIndex(i) as BlockDelegateBase)
            if (item) {
                if (atEnd)
                    item.focusAtEnd()
                else
                    item.focusAtStart()
            }
        })
    }

    // The selected blocks as structural markdown, in every Clipboard
    // flavor (§5.1): plain text, rendered HTML for rich-text targets, and
    // the internal marker so pasting back into Kvit is lossless.
    function copyBlocksToClipboard() {
        var md = DocumentSerializer.serializeBlocks(
            BlockModel, DocumentSelection.selectedIndexes())
        Clipboard.setMarkdown(md, MarkdownFormatter.toHtml(md))
    }

    // Remove the selected blocks and land the cursor on the block
    // before the removed run (§3.5).
    function removeSelectedBlocks() {
        var indexes = DocumentSelection.selectedIndexes()
        if (indexes.length === 0)
            return
        var first = Number(indexes[0])
        DocumentSelection.clear()
        BlockModel.removeBlocks(indexes)
        keys.focusBlockLater(first > 0 ? first - 1 : 0, first > 0)
    }

    function selectRange(first, last) {
        DocumentSelection.selectBlock(first)
        if (last > first)
            DocumentSelection.extendBlockSelectionTo(last)
        keys.revealSelectionEdge()
    }

    Keys.onPressed: function(event) {
        if (!DocumentSelection.hasBlockSelection)
            return
        var ctrl = event.modifiers & Qt.ControlModifier
        var shift = event.modifiers & Qt.ShiftModifier

        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Return
            || event.key === Qt.Key_Enter) {
            keys.exitToBlock(DocumentSelection.lastActiveIndex())
            event.accepted = true
            return
        }

        // Delete/Backspace — and Ctrl+Shift+D, §13.3's delete-block
        // shortcut — remove the selection as one undo step (§3.5)
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace
            || (event.key === Qt.Key_D && ctrl && shift)) {
            keys.removeSelectedBlocks()
            event.accepted = true
            return
        }

        // Ctrl+D: duplicate the selection below itself; the
        // selection moves to the clones (features.md §3.6)
        if (event.key === Qt.Key_D && ctrl) {
            var clones = BlockModel.duplicateBlocks(
                DocumentSelection.selectedIndexes())
            if (clones.length > 0)
                keys.selectRange(Number(clones[0]),
                            Number(clones[clones.length - 1]))
            event.accepted = true
            return
        }

        // Alt+Up/Down: move the selection as a unit (§3.2); the
        // selection follows the moved blocks by id
        if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
            && (event.modifiers & Qt.AltModifier)) {
            BlockModel.moveBlocksBy(DocumentSelection.selectedIndexes(),
                                    event.key === Qt.Key_Down ? 1 : -1)
            keys.revealSelectionEdge()
            event.accepted = true
            return
        }

        // Tab/Shift+Tab: indent/outdent the selection's list-family
        // blocks together (§3.3)
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            BlockModel.changeIndentForBlocks(
                DocumentSelection.selectedIndexes(),
                event.key === Qt.Key_Tab ? 1 : -1)
            event.accepted = true
            return
        }

        // Ctrl+C / Ctrl+X: the selected blocks as structural
        // markdown (§5.1, §5.2)
        if (event.key === Qt.Key_C && ctrl) {
            keys.copyBlocksToClipboard()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_X && ctrl) {
            keys.copyBlocksToClipboard()
            keys.removeSelectedBlocks()
            event.accepted = true
            return
        }

        // Ctrl+V: paste the Clipboard as typed blocks after the
        // selection (§5.3); the new blocks become the selection
        if (event.key === Qt.Key_V && ctrl) {
            if (Clipboard.hasText) {
                var indexes = DocumentSelection.selectedIndexes()
                var insertAt = indexes.length > 0
                    ? Number(indexes[indexes.length - 1]) + 1
                    : BlockModel.count
                // §5.3 format matrix (internal / HTML / plain); paste-plain
                // deliberately takes the source's own plain text instead.
                var pasteText = (shift ? Clipboard.text
                                       : Clipboard.markdown())
                                    .replace(/\r\n/g, "\n")
                // Oversized-paste guard: the same threshold as file
                // open — a whale payload gets a confirm dialog instead
                // of a silent multi-second stall.
                var capBytes = DocumentManager.maxOpenFileSizeMiB > 0
                    ? DocumentManager.maxOpenFileSizeMiB * 1024 * 1024
                    : 0
                if (capBytes > 0 && pasteText.length > capBytes) {
                    keys.oversizedPasteRequested(pasteText, insertAt,
                                                 shift ? true : false)
                } else if (shift) {
                    // Ctrl+Shift+V: strip inline formatting and drop the
                    // structure too, so the payload lands as plain
                    // paragraphs (§5.3).
                    // `editorEngine` is an id inside EditableBlock.qml
                    // and has never existed in this scope, so this threw
                    // ReferenceError and the paste silently did nothing.
                    // DocumentStats.displayTextFor is the same operation
                    // as BlockEditorEngine::stripFormatting — both return
                    // displayText(markdown) for non-verbatim content —
                    // and there is no block here, so verbatim is false.
                    var plain = pasteText.split("\n").map(function(line) {
                        return DocumentStats.displayTextFor(line, false)
                    }).join("\n")
                    var plainCount = DocumentSerializer.insertPlainTextAt(
                        BlockModel, insertAt, plain)
                    if (plainCount > 0)
                        keys.selectRange(insertAt, insertAt + plainCount - 1)
                } else {
                    var count = DocumentSerializer.insertMarkdownAt(
                        BlockModel, insertAt, pasteText)
                    if (count > 0)
                        keys.selectRange(insertAt, insertAt + count - 1)
                }
            }
            event.accepted = true
            return
        }

        if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
            && ctrl && shift) {
            DocumentSelection.extendBlockSelection(
                event.key === Qt.Key_Down ? 1 : -1)
            keys.revealSelectionEdge()
            event.accepted = true
            return
        }
        if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
            && !ctrl && !shift && !(event.modifiers & Qt.AltModifier)) {
            DocumentSelection.collapseBlockSelection(
                event.key === Qt.Key_Down ? 1 : -1)
            keys.revealSelectionEdge()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_A && ctrl) {
            DocumentSelection.selectAllBlocks()
            event.accepted = true
            return
        }
    }
}
