// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// One block's share of a text selection that spans several blocks
// (features.md §2.5, §21.3).
//
// A selection that crosses blocks cannot live in any one editor, because each
// block has a QTextDocument of its own and Qt has no notion of a range across
// two of them. DocumentSelection holds the range instead, in markdown
// coordinates, and every block renders the part of it that falls inside its
// own text through an ordinary TextArea selection. That is what this object
// does: it converts between scene points, markdown offsets and document
// offsets for one block, keeps that block's rendered portion matching the
// range, and answers the keys that operate on the range while this block is
// the one the selection was started from.
//
// The block it belongs to arrives as three objects rather than as values —
// the editor, the engine that maps its coordinates, and the list the blocks
// live in. Positions are the whole subject here, and there is no way to ask
// where a caret is without the item that laid the text out. Everything else
// is a value, and every outcome that changes the document leaves as a signal,
// because a range edit is one undo step and only the block can place the
// caret afterwards.
QtObject {
    id: root

    // This block's row in the document.
    property int blockIndex: -1
    // The editor this block renders its portion in.
    property TextArea editor: null
    // The engine that maps this block between markdown and document offsets.
    // The two differ wherever the reveal state hides a span's markers.
    property BlockEditorEngine engine: null
    // The list the blocks are rows of, which is how a range reaches the
    // blocks either side of this one.
    property ListView blockList: null
    // Whether this row is in the list's reuse pool. A pooled row shows text
    // belonging to no block, so applying a portion to it would paint a
    // selection over the wrong content.
    property bool pooled: false

    // Put the caret at a markdown offset in another block. The block that
    // does this has usually just changed the document, so the row may not
    // exist yet and the caller defers.
    signal refocusRequested(int index, int markdownPos)
    // Insert clipboard text at an offset, after a range edit collapsed the
    // selection to that point. Splitting text into blocks is the editing
    // side's job, not this one's.
    signal pasteRequested(int index, int markdownPos, string text, bool stripFormatting)

    // ---- Position mapping ----

    // Markdown position under a scene point (clamped into this block).
    function markdownPositionAt(sceneX, sceneY) {
        var p = root.editor.mapFromItem(null, sceneX, sceneY)
        var cx = Math.max(0, Math.min(p.x, root.editor.width - 1))
        var cy = Math.max(0, Math.min(p.y, root.editor.height - 1))
        return root.engine.toMarkdownPosition(root.editor.positionAt(cx, cy))
    }

    // Whether a scene point is over this block's text (a press in the
    // gutter must not seed a text-selection drag).
    function pointInText(sceneX, sceneY) {
        var p = root.editor.mapFromItem(null, sceneX, sceneY)
        return p.x >= 0 && p.x <= root.editor.width
            && p.y >= 0 && p.y <= root.editor.height
    }

    // Markdown position one visual line up/down from mdPos within this
    // block, or -1 when that would leave the block.
    function lineStepPosition(mdPos, dir) {
        var doc = Math.min(root.engine.toDocumentPosition(mdPos),
                           root.editor.text.length)
        var rect = root.editor.positionToRectangle(doc)
        var newY = rect.y + dir * rect.height + rect.height / 2
        if (newY < 0)
            return -1
        var newDoc = root.editor.positionAt(rect.x, newY)
        var newRect = root.editor.positionToRectangle(newDoc)
        if (Math.abs(newRect.y - rect.y) < 1)
            return -1 // same visual line: the step leaves the block
        return root.engine.toMarkdownPosition(newDoc)
    }

    // Entry position for a vertical crossing into this block at a given
    // x (the first or last visual line).
    function entryPositionAtX(x, fromTop) {
        var y = fromTop ? 2 : root.editor.height - 2
        var cx = Math.max(0, Math.min(x, root.editor.width - 1))
        return root.engine.toMarkdownPosition(root.editor.positionAt(cx, y))
    }

    function xAtMarkdown(mdPos) {
        var doc = Math.min(root.engine.toDocumentPosition(mdPos),
                           root.editor.text.length)
        return root.editor.positionToRectangle(doc).x
    }

    // ---- Rendering this block's portion ----

    // The Qt.callLater re-apply sites below can outlive this object: a
    // selection clear immediately followed by a document reload
    // (find-and-replace flows do this) tears the row down before the queued
    // call fires, and calling into its invalidated context is a TypeError.
    function applyTextPortionLater() {
        Qt.callLater(function() {
            if (root && typeof root.applyTextPortion === "function")
                root.applyTextPortion()
        })
    }

    // Apply this block's portion of the cross-block range to the
    // TextArea (persistentSelection keeps it visible unfocused). The
    // focused anchor block needs no help — its native selection IS its
    // portion while the mouse drags, and the keyboard paths manage it.
    function applyTextPortion() {
        if (root.pooled || root.editor.activeFocus)
            return
        var p = DocumentSelection.portionForBlock(root.blockIndex)
        if (p.selected === true && p.end > p.start) {
            var docStart = root.engine.toDocumentPosition(p.start)
            var docEnd = root.engine.toDocumentPosition(p.end)
            // Fixed-point guard: re-select only when the TextArea does
            // not already show the desired range, so the re-apply paths
            // below cannot feed back through the engine indefinitely.
            if (root.editor.selectionStart !== docStart
                || root.editor.selectionEnd !== docEnd)
                root.editor.select(docStart, docEnd)
        } else if (root.editor.selectionEnd > root.editor.selectionStart) {
            root.editor.deselect()
        }
    }

    // The document selection changed. Apply now, and once more on a clean
    // stack: the engine's deferred reveal transitions (the blurred anchor
    // block collapsing its markers) edit the document AFTER this and destroy
    // a just-applied selection. A cleared selection needs no delayed pass —
    // the synchronous call already deselected, and nothing re-selects
    // afterwards.
    function onSelectionRevisionChanged() {
        root.applyTextPortion()
        if (DocumentSelection.hasTextSelection)
            root.applyTextPortionLater()
    }

    // ---- Editing the range ----

    // Copy markdown in every clipboard flavor (§5.1). Shared by the
    // cross-block and in-block copy/cut paths.
    function copyMarkdownToClipboard(md) {
        Clipboard.setMarkdown(md, MarkdownFormatter.toHtml(md))
    }

    // Remove the coordinator's range from the model (one undo step) and
    // return the {index, cursor} landing spot.
    function crossBlockDeleteRange() {
        var range = DocumentSelection.orderedTextRange()
        DocumentSelection.clearTextSelection()
        root.editor.deselect()
        return BlockModel.removeTextRange(range.startIndex, range.startPos,
                                          range.endIndex, range.endPos)
    }

    // Move the cross-block head one step (Shift+Arrows, §21.3 keyboard
    // extension). Vertical steps stay within the head block's visual
    // lines until they must cross into the neighbor at the same x.
    function moveCrossBlockHead(key) {
        var headIdx = DocumentSelection.textHeadIndex()
        var headMd = DocumentSelection.textHeadPosition()
        if (headIdx < 0 || !root.blockList)
            return
        var content = BlockModel.getContent(headIdx)
        var headItem = (root.blockList.itemAtIndex(headIdx) as BlockDelegateBase)
        var newIdx = headIdx
        var newMd = headMd

        if (key === Qt.Key_Right) {
            if (headMd < content.length) {
                newMd = headMd + 1
            } else if (headIdx < BlockModel.count - 1) {
                newIdx = headIdx + 1
                newMd = 0
            }
        } else if (key === Qt.Key_Left) {
            if (headMd > 0) {
                newMd = headMd - 1
            } else if (headIdx > 0) {
                newIdx = headIdx - 1
                newMd = BlockModel.getContent(newIdx).length
            }
        } else if (key === Qt.Key_Down || key === Qt.Key_Up) {
            var dir = key === Qt.Key_Down ? 1 : -1
            var stepped = headItem && headItem.lineStepPosition
                ? headItem.lineStepPosition(headMd, dir) : -1
            if (stepped >= 0) {
                newMd = stepped
            } else {
                var x = headItem && headItem.xAtMarkdown
                    ? headItem.xAtMarkdown(headMd) : 0
                if (dir > 0 && headIdx < BlockModel.count - 1) {
                    newIdx = headIdx + 1
                    var below = (root.blockList.itemAtIndex(newIdx) as BlockDelegateBase)
                    newMd = below && below.entryPositionAtX
                        ? below.entryPositionAtX(x, true) : 0
                } else if (dir < 0 && headIdx > 0) {
                    newIdx = headIdx - 1
                    var above = (root.blockList.itemAtIndex(newIdx) as BlockDelegateBase)
                    newMd = above && above.entryPositionAtX
                        ? above.entryPositionAtX(x, false)
                        : BlockModel.getContent(newIdx).length
                }
            }
        }

        if (newIdx === DocumentSelection.textAnchorIndex()
            && newIdx === root.blockIndex) {
            // The head returned into the anchor block: collapse back to
            // a native in-block selection
            var anchorMd = DocumentSelection.textAnchorPosition()
            DocumentSelection.clearTextSelection()
            root.editor.select(root.engine.toDocumentPosition(anchorMd),
                               root.engine.toDocumentPosition(newMd))
            return
        }
        DocumentSelection.updateTextSelectionHead(newIdx, newMd)
    }

    // Keys while this block anchors an active cross-block selection.
    // Returns true when the key was consumed.
    function handleCrossBlockKey(event) {
        var ctrl = event.modifiers & Qt.ControlModifier
        var shift = event.modifiers & Qt.ShiftModifier
        var isArrow = event.key === Qt.Key_Left || event.key === Qt.Key_Right
                   || event.key === Qt.Key_Up || event.key === Qt.Key_Down

        if (event.key === Qt.Key_Escape) {
            DocumentSelection.clearTextSelection()
            root.editor.deselect()
            event.accepted = true
            return true
        }
        if (shift && !ctrl && isArrow) {
            root.moveCrossBlockHead(event.key)
            event.accepted = true
            return true
        }
        if (!ctrl && !shift && isArrow) {
            // Plain arrows collapse the selection to its edge
            var range = DocumentSelection.orderedTextRange()
            DocumentSelection.clearTextSelection()
            root.editor.deselect()
            var goStart = event.key === Qt.Key_Left || event.key === Qt.Key_Up
            root.refocusRequested(goStart ? range.startIndex : range.endIndex,
                                  goStart ? range.startPos : range.endPos)
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_C && ctrl) {
            root.copyMarkdownToClipboard(DocumentSelection.rangeMarkdown())
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_X && ctrl) {
            root.copyMarkdownToClipboard(DocumentSelection.rangeMarkdown())
            var cutResult = root.crossBlockDeleteRange()
            if (cutResult.index !== undefined)
                root.refocusRequested(cutResult.index, cutResult.cursor)
            event.accepted = true
            return true
        }
        // Ctrl+V / Ctrl+Shift+V over a cross-block selection: the range goes
        // first, exactly as every sibling operation here does, and the
        // clipboard lands at the collapsed caret. Without this branch the
        // per-block handler would run instead and see only the head block's
        // own selection, leaving the rest of the range in the document.
        if (event.key === Qt.Key_V && ctrl) {
            if (Clipboard && Clipboard.hasText) {
                var stripPaste = (event.modifiers & Qt.ShiftModifier) ? true : false
                var pasteRes = root.crossBlockDeleteRange()
                if (pasteRes.index !== undefined)
                    root.pasteRequested(pasteRes.index, pasteRes.cursor,
                                        Clipboard.text, stripPaste)
            }
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            var delResult = root.crossBlockDeleteRange()
            if (delResult.index !== undefined)
                root.refocusRequested(delResult.index, delResult.cursor)
            event.accepted = true
            return true
        }
        // Printable text replaces the range; the deletion and the typed
        // character are layered undo steps
        if (!ctrl && event.text.length > 0 && event.text.charCodeAt(0) >= 32) {
            var repResult = root.crossBlockDeleteRange()
            if (repResult.index !== undefined) {
                var md = BlockModel.getContent(repResult.index)
                BlockModel.updateContent(repResult.index,
                    md.substring(0, repResult.cursor) + event.text
                    + md.substring(repResult.cursor))
                root.refocusRequested(repResult.index,
                                      repResult.cursor + event.text.length)
            }
            event.accepted = true
            return true
        }
        return false
    }

    // Shift+Arrow at a block edge: begin a cross-block selection reaching
    // into the neighbour. Returns true when the key started one; within the
    // block the caller leaves the arrow to the editor's native extension.
    function beginSelectionAtEdge(event) {
        var crossIdx = -1
        var crossMd = 0
        var ed = root.editor
        if (event.key === Qt.Key_Right
            && ed.cursorPosition >= ed.text.length
            && root.blockIndex < BlockModel.count - 1) {
            crossIdx = root.blockIndex + 1
            crossMd = 0
        } else if (event.key === Qt.Key_Left
                   && ed.cursorPosition === 0 && root.blockIndex > 0) {
            crossIdx = root.blockIndex - 1
            crossMd = BlockModel.getContent(crossIdx).length
        } else if (event.key === Qt.Key_Down
                   && root.cursorOnLastLine()
                   && root.blockIndex < BlockModel.count - 1) {
            crossIdx = root.blockIndex + 1
            var below = root.blockList
                ? (root.blockList.itemAtIndex(crossIdx) as BlockDelegateBase) : null
            crossMd = below && below.entryPositionAtX
                ? below.entryPositionAtX(
                      ed.positionToRectangle(ed.cursorPosition).x, true)
                : 0
        } else if (event.key === Qt.Key_Up
                   && root.cursorOnFirstLine() && root.blockIndex > 0) {
            crossIdx = root.blockIndex - 1
            var above = root.blockList
                ? (root.blockList.itemAtIndex(crossIdx) as BlockDelegateBase) : null
            crossMd = above && above.entryPositionAtX
                ? above.entryPositionAtX(
                      ed.positionToRectangle(ed.cursorPosition).x, false)
                : BlockModel.getContent(crossIdx).length
        }
        if (crossIdx < 0)
            return false
        // The anchor is the far end of any native selection, else the cursor
        var anchorDoc = ed.selectionEnd > ed.selectionStart
            ? (ed.cursorPosition === ed.selectionEnd
                   ? ed.selectionStart : ed.selectionEnd)
            : ed.cursorPosition
        DocumentSelection.beginTextSelection(root.blockIndex,
            root.engine.toMarkdownPosition(anchorDoc), 0)
        DocumentSelection.updateTextSelectionHead(crossIdx, crossMd)
        return true
    }

    // Whether the caret is on the first or last visual line of this block,
    // which is what decides between moving within it and leaving it.
    function cursorOnFirstLine() {
        if (root.editor.text.indexOf('\n') === -1) return true
        var rect = root.editor.positionToRectangle(root.editor.cursorPosition)
        var firstLineRect = root.editor.positionToRectangle(0)
        return Math.abs(rect.y - firstLineRect.y) < 1
    }

    function cursorOnLastLine() {
        if (root.editor.text.indexOf('\n') === -1) return true
        var rect = root.editor.positionToRectangle(root.editor.cursorPosition)
        var lastLineRect = root.editor.positionToRectangle(root.editor.text.length)
        return Math.abs(rect.y - lastLineRect.y) < 1
    }
}
