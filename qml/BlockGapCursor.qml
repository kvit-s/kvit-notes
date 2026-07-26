// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// A caret placed between two blocks, which typing turns into a block
// (features.md §3.7).
//
// The two ways to add a block both start from a block that already exists:
// Enter at the end of one, and the gutter's plus-button on one. Neither reads
// as "put something here" when the place you want is the seam between a table
// and the paragraph under it — Enter belongs to the table's own editing, and
// the plus-button asks you to find the row above the space you were looking
// at. This names the space itself: hover the seam and a line appears in it,
// click and the line becomes a caret, then type and a paragraph holding what
// you typed takes the caret's place.
//
// It is a mode, like block selection, and is built the same way: this item
// takes the focus, which is what blurs whichever editor had it, and holds
// every key the mode answers. Losing focus — a click into any block — ends it.
//
// The character that starts a block goes in through the editor rather than
// through the model, so the engine reports it as a user edit. That is the
// difference between a paragraph that happens to contain "/" and the block
// menu opening: the slash trigger and the markdown prefix conversions all
// hang off the edited signal, and a programmatic content update does not
// raise it.
Item {
    id: cursor
    objectName: "blockGapCursor"

    // Wired by main.qml: the list of blocks, the window that owns focusing a
    // row by index, and the block drag, which draws its own indicator in
    // these same seams and must not have a second one under it.
    property var listView: null
    property var appWindow: null
    property var dragState: null

    // The armed gap, counted like an insertion index: gap g is the seam
    // above block g, and gap BlockModel.count is the space below the last
    // block. -1 when the mode is off.
    property int gap: -1
    // The seam under the pointer, which is what a click would arm. Also -1
    // for "none", and independent of `gap` so a hover elsewhere while the
    // caret is placed shows both.
    property int hoverGap: -1

    readonly property bool armed: cursor.gap >= 0
    readonly property bool suspended:
        cursor.dragState !== null && cursor.dragState.active

    // The gutter column, which the seam deliberately stops short of: the
    // plus-button and the drag handle sit at the top of a row, and a strip
    // spanning the full width would take the clicks meant for them. The
    // numbers are the drop indicator's, so the two land in the same place.
    readonly property int leftInset: 40
    readonly property int rightInset: 8
    // How far from a seam's line the pointer still counts as being in it.
    // Between two rows this is never the binding limit — the blank space is
    // narrower than it and a row claims the pointer the moment it leaves that
    // space. It is the limit at the two ends, where nothing else claims the
    // pointer and the offer would otherwise fill the rest of the page.
    readonly property int reach: 10

    // ---- Geometry -------------------------------------------------------

    // Where a seam's line sits in the list's content coordinates. Same
    // arithmetic as the multi-block drop indicator: a seam is half the row
    // spacing above the block it precedes, and the last one is that far below
    // the final row.
    function gapContentY(g) {
        var lv = cursor.listView
        if (!lv || BlockModel.count === 0)
            return 0
        if (g >= BlockModel.count) {
            var last = (lv.itemAtIndex(BlockModel.count - 1) as BlockDelegateBase)
            return last ? last.y + last.height + lv.spacing / 2 : 0
        }
        var item = (lv.itemAtIndex(Math.max(0, g)) as BlockDelegateBase)
        return item ? item.y - lv.spacing / 2 : 0
    }

    // The same line in viewport coordinates, which is what the strip and the
    // two indicators are positioned in — they are children of the list rather
    // than of its content item, so they do not scroll with it.
    function gapViewportY(g) {
        var lv = cursor.listView
        return cursor.gapContentY(g) - (lv ? lv.contentY : 0)
    }

    // Whether a scene point is over text one of the blocks either side of
    // seam `g` would take a caret in. Where that is true the click belongs to
    // the block: pressing on a paragraph puts the caret in the paragraph.
    function overNeighbourText(g, sceneX, sceneY) {
        var lv = cursor.listView
        if (!lv)
            return false
        for (var i = g - 1; i <= g; ++i) {
            if (i < 0 || i >= BlockModel.count)
                continue
            var item = (lv.itemAtIndex(i) as BlockDelegateBase)
            if (item && item.pointInText(sceneX, sceneY))
                return true
        }
        return false
    }

    // The seam under a point, or -1. `cx`/`cy` are the list's content
    // coordinates and `sceneX`/`sceneY` the same point in the scene, which is
    // the coordinate system a block answers pointInText in.
    function gapUnder(cx, cy, sceneX, sceneY) {
        var lv = cursor.listView
        if (!lv || cursor.suspended || BlockModel.count === 0)
            return -1
        if (cx < cursor.leftInset || cx > lv.width - cursor.rightInset)
            return -1

        // Which seams are candidates: above the rows it can only be the first
        // and below them only the last; inside, the ones bounding the row the
        // point is in, or — when the point is in the spacing itself, where
        // there is no row to find — the seam between the rows either side.
        var candidates = []
        if (cy < 0) {
            candidates.push(0)
        } else if (cy > lv.contentHeight) {
            candidates.push(BlockModel.count)
        } else {
            var probeX = Math.max(1, Math.min(cx, lv.width - 1))
            var idx = lv.indexAt(probeX, cy)
            if (idx >= 0) {
                candidates.push(idx)
                candidates.push(idx + 1)
            } else {
                var above = lv.indexAt(probeX, cy - lv.spacing - 1)
                if (above >= 0)
                    candidates.push(above + 1)
                var below = lv.indexAt(probeX, cy + lv.spacing + 1)
                if (below >= 0)
                    candidates.push(below)
            }
        }

        var best = -1
        var bestDistance = cursor.reach
        for (var i = 0; i < candidates.length; ++i) {
            var g = candidates[i]
            if (g < 0 || g > BlockModel.count)
                continue
            var d = Math.abs(cy - cursor.gapContentY(g))
            if (d <= bestDistance) {
                bestDistance = d
                best = g
            }
        }
        if (best < 0)
            return -1
        // Inside the blank space between the rows there is nothing else the
        // point could belong to. Past it — which in practice means the space
        // above the first block or below the last, since a row claims the
        // pointer over itself before this is asked — the seam still yields to
        // any text under the point.
        if (bestDistance <= lv.spacing / 2)
            return best
        return cursor.overNeighbourText(best, sceneX, sceneY) ? -1 : best
    }

    // ---- The mode -------------------------------------------------------

    function place(g) {
        if (!cursor.listView || BlockModel.count === 0)
            return
        DocumentSelection.clear()
        cursor.gap = Math.max(0, Math.min(g, BlockModel.count))
        cursor.forceActiveFocus()
        cursor.reveal()
        A11y.announce(qsTr("Insertion point between blocks"))
    }

    function dismiss() {
        cursor.gap = -1
    }

    function reveal() {
        var lv = cursor.listView
        if (!lv || cursor.gap < 0 || BlockModel.count === 0)
            return
        lv.positionViewAtIndex(
            Math.max(0, Math.min(cursor.gap, BlockModel.count - 1)),
            ListView.Contain)
    }

    function moveBy(delta) {
        var next = cursor.gap + delta
        if (next < 0 || next > BlockModel.count)
            return
        cursor.gap = next
        cursor.reveal()
    }

    // Put a paragraph in the seam and leave the caret in it. `typed` is the
    // character that asked for the block, or "" when Enter did; it is handed
    // to the new row rather than written into the model, so the block menu
    // and the prefix conversions see it as typing.
    function insertHere(typed) {
        if (cursor.gap < 0 || !cursor.appWindow)
            return
        var at = cursor.gap
        cursor.dismiss()
        BlockModel.insertBlock(at, Block.Paragraph, "")
        cursor.appWindow.focusBlockAtIndex(at, false, typed)
    }

    // Leave for the text either side: the end of the block above, or the
    // start of the first block when the caret was above it.
    function leaveToText() {
        var g = cursor.gap
        cursor.dismiss()
        if (!cursor.appWindow || BlockModel.count === 0)
            return
        cursor.appWindow.focusBlockAtIndex(g > 0 ? g - 1 : 0, g > 0)
    }

    // A click anywhere in a block moves the focus, which is the end of the
    // mode; nothing else has to notice.
    onActiveFocusChanged: {
        if (!cursor.activeFocus)
            cursor.dismiss()
    }

    // A drag draws its own indicator in these seams.
    onSuspendedChanged: {
        if (cursor.suspended) {
            cursor.hoverGap = -1
            cursor.dismiss()
        }
    }

    Connections {
        target: BlockModel
        // Loading another note leaves a seam index that means nothing now.
        function onCountChanged() {
            if (cursor.gap > BlockModel.count)
                cursor.dismiss()
            cursor.hoverGap = -1
        }
    }

    Keys.onPressed: function(event) {
        if (cursor.gap < 0)
            return

        if (event.key === Qt.Key_Escape) {
            cursor.leaveToText()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            cursor.moveBy(event.key === Qt.Key_Down ? 1 : -1)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            cursor.insertHere("")
            event.accepted = true
            return
        }

        // Anything that types a character starts a paragraph holding it.
        // AltGr reaches here as Ctrl+Alt on X11 and Windows, so the two
        // together are a typed character rather than a shortcut.
        var ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        var alt = (event.modifiers & Qt.AltModifier) !== 0
        if (event.text.length > 0 && ((!ctrl && !alt) || (ctrl && alt))) {
            var code = event.text.charCodeAt(0)
            if (code >= 0x20 && code !== 0x7f) {
                cursor.insertHere(event.text)
                event.accepted = true
            }
        }
    }

    // ---- What the pointer sees ------------------------------------------

    // The hover probe: where the pointer is, when it is not on a block.
    //
    // Behind the rows, deliberately, and that is the whole trick. Qt walks the
    // items front to back looking for one that takes the hover and stops at
    // the first that does, so an overlay on top of the list silently takes
    // every row's hover with it — and a row's hover is what fades its gutter
    // buttons in. Sitting at the back of the list's content item instead, this
    // is offered the pointer only where no row claimed it, which is exactly
    // the blank space between two rows. `blocking: false` is not enough on its
    // own; the stacking is what matters.
    //
    // It reaches a little past the rows at both ends so the space above the
    // first block and below the last one can be pointed at too.
    Item {
        id: probe
        objectName: "gapCursorProbe"
        parent: cursor.listView ? cursor.listView.contentItem : null
        z: -1
        x: 0
        y: -cursor.reach
        width: cursor.listView ? cursor.listView.width : 0
        height: cursor.listView
            ? cursor.listView.contentHeight + cursor.reach * 2 : 0

        HoverHandler {
            id: seamHover
            blocking: false

            onPointChanged: {
                var p = seamHover.point.position
                var scene = probe.mapToItem(null, p.x, p.y)
                cursor.hoverGap = cursor.gapUnder(p.x, p.y + probe.y,
                                                  scene.x, scene.y)
            }
        }

        Connections {
            target: seamHover
            function onHoveredChanged() {
                if (!seamHover.hovered)
                    cursor.hoverGap = -1
            }
        }
    }

    // The strip a click lands in. It exists only where the pointer already
    // is, so it takes no press the seam was not offered.
    MouseArea {
        id: seamBand
        objectName: "gapCursorBand"
        parent: cursor.listView
        z: 2
        visible: cursor.listView !== null && cursor.hoverGap >= 0
        x: cursor.leftInset
        y: cursor.gapViewportY(cursor.hoverGap) - height / 2
        width: cursor.listView
            ? Math.max(0, cursor.listView.width - cursor.leftInset
                          - cursor.rightInset)
            : 0
        // The blank space itself, with a couple of pixels' tolerance so a
        // press that drifts off the line still lands. It cannot usefully be
        // larger: leaving the blank space puts the pointer on a row, which
        // takes the hover back and this strip goes with it.
        height: cursor.listView ? Math.max(6, cursor.listView.spacing) : 8
        acceptedButtons: Qt.LeftButton
        // Deliberately not hover-enabled: a MouseArea that takes hover would
        // take it from the row underneath, and this strip overlaps one.
        hoverEnabled: false
        cursorShape: Qt.IBeamCursor
        onClicked: cursor.place(cursor.hoverGap)
    }

    // The caret. Blinks, which is the thing that says a keystroke would go
    // somewhere; reduced motion (§14.3) stills it rather than removing it.
    Rectangle {
        id: caret
        objectName: "gapCursorCaret"
        parent: cursor.listView
        z: 3
        visible: cursor.listView !== null && cursor.armed
        x: cursor.leftInset
        y: cursor.gapViewportY(cursor.gap) - height / 2
        width: cursor.listView
            ? Math.max(0, cursor.listView.width - cursor.leftInset
                          - cursor.rightInset)
            : 0
        height: 3
        radius: 1.5
        color: Theme.accent
        // The dim half of the blink stays above the hover line's weight, so a
        // placed caret never reads as less than a mere offer.
        opacity: blinkTimer.on ? 1 : 0.4

        Timer {
            id: blinkTimer
            property bool on: true
            interval: 530
            repeat: true
            running: caret.visible && Theme.motionScale > 0
            onTriggered: blinkTimer.on = !blinkTimer.on
            onRunningChanged: {
                if (!blinkTimer.running)
                    blinkTimer.on = true
            }
        }
    }

    // Where a click would put the caret, while it is somewhere else or
    // nowhere. Thinner and faint: an offer, not a position.
    Rectangle {
        objectName: "gapCursorHint"
        parent: cursor.listView
        z: 3
        visible: cursor.listView !== null && cursor.hoverGap >= 0
                 && cursor.hoverGap !== cursor.gap
        x: cursor.leftInset
        y: cursor.gapViewportY(cursor.hoverGap) - height / 2
        width: cursor.listView
            ? Math.max(0, cursor.listView.width - cursor.leftInset
                          - cursor.rightInset)
            : 0
        height: 2
        radius: 1
        color: Theme.accent
        opacity: 0.35
    }
}
