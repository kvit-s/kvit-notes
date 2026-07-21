// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// The edge auto-scroll shared by the two pointer gestures over the block list
// (features.md §21.3 "smooth accelerated scrolling").
//
// A gesture whose pointer reaches the top or bottom band of the viewport has
// run out of list to work with, so the list moves under it, faster the nearer
// the pointer is to the edge. A cross-block text selection and a block drag
// both need this, which is why it is one object they share rather than a copy
// inside each.
Item {
    id: scroller

    // Wired by main.qml.
    property var listView

    // Set by whichever gesture is running: the pointer's y in the list's
    // viewport coordinates, and whether to act on it at all.
    property bool active: false
    property real pointerY: 0
    readonly property int band: 48

    Timer {
        interval: 16
        repeat: true
        running: scroller.active
        onTriggered: {
            var lv = scroller.listView
            if (lv.contentHeight <= lv.height)
                return
            var speed = 0
            if (scroller.pointerY < scroller.band)
                speed = -(scroller.band - scroller.pointerY) / 4
            else if (scroller.pointerY > lv.height - scroller.band)
                speed = (scroller.pointerY - (lv.height - scroller.band)) / 4
            if (speed === 0)
                return
            lv.contentY = Math.max(0, Math.min(lv.contentY + speed,
                                               lv.contentHeight - lv.height))
        }
    }
}
