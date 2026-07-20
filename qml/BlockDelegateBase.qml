// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

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

    // The slash / block menu, anchored at this block's caret. Only the text
    // delegates raise it — a divider or a media card has no caret to anchor
    // to — so callers guard on it and the default here does nothing, which
    // is what those callers already expected to happen.
    function openBlockMenu(mode) {}
}
