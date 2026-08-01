// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The measuring twins below are separate scopes that read this run's own
// properties.
pragma ComponentBehavior: Bound

import QtQuick
import Kvit 1.0

// A line of rendered text the pointer can select part of.
//
// The blocks that draw their content rather than edit it — the web embed
// card, the collection query's result grid, the table of contents — put every
// piece of text on screen in a `Text`, which lays text out but cannot say
// which character is under a point and cannot highlight a span of itself. A
// read-only `TextEdit` can do both, so that is what this is: a `TextEdit`
// dressed as the `Text` it replaces, with the mouse handling switched off so
// that RenderedTextSelection, which coordinates a sweep across all the runs in
// one block, is the only thing that ever moves the selection.
//
// `Text` properties that `TextEdit` does not have are reproduced here rather
// than dropped, because dropping them would change how every one of those
// cards is laid out:
//
//   elide             a single-line run is elided through TextMetrics
//   maximumLineCount  a wrapped run is measured in a hidden twin holding the
//                     full text, then cut to the last character that fits
//
// Selection and copying both work on what is on screen, so a run elided to
// "Some very long ti…" selects and copies those characters and not the title
// behind them. That is the same rule the rest of the block follows: what the
// reader sees is what the reader gets.
Item {
    id: run

    // ---- the Text-shaped surface ----
    property string text: ""
    property color color: Theme.textPrimary
    property int wrapMode: Text.NoWrap
    property int elide: Text.ElideNone
    // 0 means unlimited, matching Text's default of INT_MAX closely enough
    // for the call sites here.
    property int maximumLineCount: 0
    property int horizontalAlignment: Text.AlignLeft
    property int textFormat: Text.PlainText
    property real bottomPadding: 0
    property alias font: body.font
    property alias lineCount: body.lineCount

    // ---- what RenderedTextSelection asks of a run ----
    // Marks this item as a run while the coordinator walks the block's item
    // tree; nothing else in the tree answers to it.
    readonly property bool isSelectableRun: true
    // The text actually on screen, which is what gets selected and copied.
    readonly property string runText: body.text
    readonly property int runLength: body.length

    function selectRange(from, to) {
        var a = Math.max(0, Math.min(from, body.length))
        var b = Math.max(0, Math.min(to, body.length))
        if (body.selectionStart === a && body.selectionEnd === b)
            return
        body.select(a, b)
    }
    function deselectRun() {
        if (body.selectionEnd > body.selectionStart)
            body.deselect()
    }
    // The character offset under a scene point, clamped into this run: a
    // sweep that has left the run sideways or vertically still resolves to
    // the nearest end of it rather than to nothing.
    function positionAtPoint(sceneX, sceneY) {
        var p = body.mapFromItem(null, sceneX, sceneY)
        var cx = Math.max(0, Math.min(p.x, Math.max(1, body.width) - 1))
        var cy = Math.max(0, Math.min(p.y, Math.max(1, body.height) - 1))
        return body.positionAt(cx, cy)
    }
    function slice(from, to) { return body.getText(from, to) }

    implicitWidth: body.implicitWidth
    implicitHeight: body.implicitHeight

    // A screen reader read these as static text when they were `Text` items,
    // and the editor the run is built on is switched off, so it reports
    // itself as a disabled text field instead. Said here, the run reads as
    // what it looks like.
    Accessible.role: Accessible.StaticText
    Accessible.name: run.text

    // Whether the run is cut to a line count (wrapped) or to a width (a
    // single line). The two need different machinery, and most runs need
    // neither, so both measuring paths load only where they are used.
    readonly property bool clampsLines: run.elide !== Text.ElideNone
        && run.maximumLineCount > 0 && run.wrapMode !== Text.NoWrap
    readonly property bool clampsWidth: run.elide !== Text.ElideNone
        && !run.clampsLines

    TextEdit {
        id: body
        objectName: "selectableRunBody"
        width: run.width
        readOnly: true
        // The coordinator owns the selection. Left to itself a TextEdit
        // selects by mouse (the default since Qt 6.4), which would give each
        // run a selection of its own that no other run knew about.
        selectByMouse: false
        // Switching that off is not enough to keep the pointer away: a
        // TextEdit accepts the left button whatever it is going to do with
        // it, and an accepted press is never offered to the handlers on the
        // card behind — so every sweep that started on a run would have
        // started on nothing. Disabling takes the item out of event delivery
        // altogether and changes nothing about how it draws.
        enabled: false
        selectByKeyboard: false
        activeFocusOnPress: false
        activeFocusOnTab: false
        cursorVisible: false
        // The selection has to stay painted once the pointer is released and
        // the keyboard has gone to the block's focus item.
        persistentSelection: true
        color: run.color
        wrapMode: run.wrapMode
        textFormat: run.textFormat
        horizontalAlignment: run.horizontalAlignment
        bottomPadding: run.bottomPadding
        selectionColor: Theme.selectionActiveTint
        selectedTextColor: Theme.textPrimary
        text: run.clampsLines ? run.clampedText
            : run.widthMetrics ? run.widthMetrics.elidedText
            : run.text
    }

    // Single-line eliding: the same measurement Text does internally.
    Loader {
        id: widthClamp
        active: run.clampsWidth
        sourceComponent: TextMetrics {
            font: body.font
            text: run.text
            elide: run.elide
            elideWidth: run.width
        }
    }

    // Wrapped eliding. The full text is laid out in a twin that is never
    // drawn, so asking where the allowed lines end cannot feed back into what
    // the visible run is showing.
    Loader {
        id: lineClamp
        active: run.clampsLines
        sourceComponent: TextEdit {
            opacity: 0
            // Invisible to the pointer as well as to the eye. An item at zero
            // opacity is still in the scene and still accepts the left
            // button, and this one lies over whatever the card puts beside
            // its text — on the embed card, over the button that edits the
            // URL, which stopped working the moment this twin appeared.
            enabled: false
            width: run.width
            font: body.font
            text: run.text
            wrapMode: run.wrapMode
            textFormat: run.textFormat
        }
    }

    // A Loader's `item` is typed QObject, so everything asked of it below
    // would be an unchecked property lookup. Named here with the type each
    // loader actually holds, the measurements are checked like anything else.
    readonly property TextMetrics widthMetrics: widthClamp.item as TextMetrics
    readonly property TextEdit lineTwin: lineClamp.item as TextEdit

    readonly property string clampedText: {
        var twin = run.lineTwin
        if (!twin || twin.lineCount <= run.maximumLineCount)
            return run.text
        var lineHeight = twin.contentHeight / Math.max(1, twin.lineCount)
        // Half a line into the last one that is allowed, at its right edge:
        // the offset just past the last character that will be visible.
        var cut = twin.positionAt(
            twin.width, lineHeight * (run.maximumLineCount - 0.5))
        var head = run.text.substring(0, Math.max(0, cut - 1))
        return head.replace(/\s+$/, "") + "…"
    }
}
