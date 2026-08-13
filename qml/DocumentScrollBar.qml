// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The editor's scrollbar, drawn from the document-height table rather than
// from the block list's own estimate of how tall the document is.
//
// A QQuickListView keeps no per-row height memory: a row that leaves the
// window is pooled and its measured height goes with it, so the view's
// contentHeight is the rows it currently has plus the average of those rows
// for every one it does not. That average moves whenever the set of built
// rows changes, which is on every notch of the wheel, and the handle's length
// is the viewport divided by it. In a note of very unequal rows the handle
// swings by a quarter to a half of its own size over a single read, in both
// directions, because reading the note through teaches the view nothing it
// keeps. tests/test_scrollmetrics.cpp measures both estimates over the same
// walk.
//
// DocumentHeights does keep it, so this bar asks the table three questions
// instead: how tall the document is, where a block sits in it, and which
// block covers a given offset. Nothing here places a row — the list goes on
// measuring and positioning rows exactly as it did, and a stale entry in the
// table costs a handle drawn at a slightly wrong length and nothing else.
//
// It is a standalone ScrollBar rather than the one a Flickable attaches for
// itself. An attached bar is driven from C++: Qt binds its size and position
// to the flickable's visibleArea — the estimate this exists to stop using —
// and writes contentY directly when the handle is dragged. Both are the
// behaviour being replaced, and there is no way to opt out of half of it, so
// the shell switches the attached bars off and positions this one itself.
ScrollBar {
    id: control
    objectName: "editorScrollBar"

    // The document view this bar scrolls.
    property ListView listView: null

    orientation: Qt.Vertical
    policy: ScrollBar.AsNeeded

    // A standalone bar is told when it is in use; an attached one is told by
    // the flickable it belongs to. Without this the Fusion style draws it at
    // zero opacity forever, since its "active" state is what fades it in.
    active: control.pressed || control.hovered
            || (control.listView ? control.listView.moving : false)

    // The scrollable extent in document coordinates: every block's height,
    // measured or estimated, plus the space the list leaves past the last
    // block so the end of a note can be pulled up into the middle of the
    // window.
    readonly property real scrollRange:
        control.listView
            ? DocumentHeights.totalHeight + control.listView.bottomMargin
            : 0

    // Whether the table can answer yet. It cannot before the first row has
    // been measured, which is the frame or two after a note opens, and the
    // view's own estimate is what there is until then.
    readonly property bool driven:
        control.listView !== null && DocumentHeights.ready
        && control.scrollRange > 0

    // Both numbers are assigned rather than bound, and always together.
    //
    // The position is the summed height of the blocks above the top row plus
    // how far the view is into that row, which is a walk over the rows the
    // list has built; a binding over that walk would subscribe to the
    // geometry of every row in the window and re-evaluate on each one. The
    // size is written here too so that the two can never be a frame out of
    // step with each other, which is what decides whether the handle can
    // reach the end of the bar.
    function refresh() {
        var view = control.listView
        if (!view)
            return
        var size = control.driven
            ? Math.min(1, view.height / control.scrollRange)
            : view.visibleArea.heightRatio
        control.size = size
        // While the handle is held, the reader owns where it is: writing a
        // position under a drag would fight the pointer. The release brings
        // it back to what the document says.
        if (control.pressed)
            return
        control.position = Math.max(0, Math.min(control.documentPosition(),
                                                Math.max(0, 1 - size)))
    }

    // Where the top of the viewport sits in the document, as a fraction of
    // the scrollable extent.
    function documentPosition() {
        var view = control.listView
        if (!view)
            return 0
        if (!control.driven)
            return view.visibleArea.yPosition
        var range = control.scrollRange
        // A document at its end says so, and refresh()'s clamp turns that
        // into the far end of the bar. The table's total and the list's
        // contentHeight are two estimates of the same document and need not
        // agree while part of it has never been built; where they do not, the
        // arithmetic below leaves the handle short of the end of a document
        // that has nothing left to scroll.
        var end = control.endContentY()
        if (end > view.originY && view.contentY >= end - 0.5)
            return 1
        var top = control.topRow()
        if (top.index < 0)
            return 0
        return (DocumentHeights.offsetOf(top.index) + top.into) / range
    }

    // How far the view can be scrolled, in its own content coordinates.
    //
    // originY is in it because contentY's zero is not the top of the
    // document: a ListView anchors its content coordinates on the rows it
    // currently has and estimates the space above the first of them, so the
    // top of the document is at originY and the foot at originY plus the
    // content and the scroll space past it. Left out, this is short by
    // however far the origin has drifted, which in the note this was measured
    // on was several hundred pixels — enough for the handle to reach the end
    // of the bar with a screenful of the note still below.
    function endContentY() {
        var view = control.listView
        if (!view)
            return 0
        return view.originY
            + Math.max(0, view.contentHeight + view.bottomMargin - view.height)
    }

    // Which block is at the top of the viewport, and how far the view is into
    // it.
    //
    // The list is asked, rather than the items in its content item walked,
    // because it is the only thing that knows which of those items are rows it
    // is currently showing. A row the list has stopped showing keeps its last
    // position until the layout that releases it, so the content item holds
    // rows lying over one another, and a walk that took the topmost of them
    // answered with a block a screenful or two from the one on screen.
    function topRow() {
        var answer = { index: -1, into: 0 }
        var view = control.listView
        if (!view || !view.contentItem)
            return answer
        var y = view.contentY
        var x = Math.max(1, view.width / 2)
        var index = view.indexAt(x, y)
        if (index < 0) {
            // Nothing covers the point when it falls in the gap the spacing
            // leaves between two rows, and the top of the viewport is in that
            // gap for a good part of any scroll. The row under the gap is the
            // one at the top of the window, and the reader is none of the way
            // into it.
            index = view.indexAt(x, y + view.spacing + 1)
        }
        if (index < 0)
            index = control.topRowByWalking()
        if (index < 0)
            return answer
        var row = view.itemAtIndex(index) as BlockDelegateBase
        if (!row)
            return answer
        answer.index = index
        // Clamped to the row, so a viewport top that has reached the gap
        // below it reports the foot of that row rather than a distance into
        // the next one.
        answer.into = Math.max(0, Math.min(y - row.y,
                                           DocumentHeights.heightOf(index)))
        return answer
    }

    // The fallback for a viewport top that is over no row at all: past the
    // last block, which a window shorter than the list's bottom margin
    // reaches, or above the first. The topmost row the list still names at its
    // own index is the answer; the comparison is what keeps the rows it has
    // stopped showing out.
    function topRowByWalking() {
        var view = control.listView
        var built = view.contentItem.children
        var found = -1
        var topY = 0
        for (var i = 0; i < built.length; ++i) {
            var row = built[i] as BlockDelegateBase
            if (!row || row.index < 0
                    || view.itemAtIndex(row.index) !== row
                    || row.y + row.height + view.spacing <= view.contentY)
                continue
            if (found < 0 || row.y < topY) {
                found = row.index
                topY = row.y
            }
        }
        return found
    }

    // Take the view to a fraction of the document.
    //
    // The fraction resolves to a BLOCK rather than to a content offset. A
    // ListView's contentY has its zero at originY, which moves as rows are
    // built and discarded — over several hundred pixels in the note this was
    // measured on, while nothing on screen moved — so a fraction written
    // straight to contentY is a fraction of something else by the time it
    // lands.
    function scrollTo(fraction) {
        var view = control.listView
        var range = control.scrollRange
        if (!view || range <= 0)
            return
        var target = fraction * range
        var index = DocumentHeights.blockAt(target)
        if (index < 0)
            return
        var remainder = target - DocumentHeights.offsetOf(index)
        // Asked more than once, because the first placement is what teaches
        // the list how tall the rows around the target are. A list whose own
        // estimate of the document is short — which it is whenever the rows it
        // has built are shorter than the note's average — cannot scroll as far
        // as it is being asked to and stops at what it believes the end to be;
        // having built the target row it revises that, and answers the next
        // attempt properly. Three is a bound rather than a number that means
        // anything: the loop stops as soon as the row is at the top.
        for (var attempt = 0; attempt < 3; ++attempt) {
            view.positionViewAtIndex(index, ListView.Beginning)
            var placed = view.itemAtIndex(index)
            if (placed && Math.abs(placed.y - view.contentY) < 1)
                break
        }
        // The remainder is applied inside the row the drag landed in, and
        // only as far as that row goes: anything past it belongs to the next
        // row, which the list places rather than this.
        var row = view.itemAtIndex(index) as BlockDelegateBase
        var rowHeight = row ? row.height : DocumentHeights.heightOf(index)
        view.contentY = Math.max(
            view.originY,
            Math.min(view.contentY + Math.max(0, Math.min(remainder, rowHeight)),
                     control.endContentY()))
    }

    // A drag, a click in the groove and a page step all arrive as a position
    // written under a press, which is the one case where the bar moves the
    // document rather than the other way round.
    onPositionChanged: {
        if (control.pressed)
            control.scrollTo(control.position)
    }
    onPressedChanged: control.refresh()
    onListViewChanged: control.refresh()
    Component.onCompleted: control.refresh()

    Connections {
        target: control.listView
        function onContentYChanged() { control.refresh() }
        function onContentHeightChanged() { control.refresh() }
        function onOriginYChanged() { control.refresh() }
        function onHeightChanged() { control.refresh() }
        function onBottomMarginChanged() { control.refresh() }
    }

    // Every measurement, and every model change that moved or dropped one.
    Connections {
        target: DocumentHeights
        function onRevisionChanged() { control.refresh() }
    }
}
