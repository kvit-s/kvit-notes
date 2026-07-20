// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// A draggable seam between panels (features.md §9.1 "resizable panels
// with drag handles"). Sits to the RIGHT of the panel it resizes;
// dragging emits resized() with the clamped new width — the window owns
// and persists the width state.
Item {
    id: seam

    property int minWidth: 140
    property int maxWidth: 400
    property int panelWidth: 0

    signal resized(int newWidth)

    // True while the seam is being dragged. The reinstated panel-collapse
    // animation (§14.3) reads this to stay instant during a resize drag, so the
    // width animation and seam drag never fight (the Phase 9 conflict).
    readonly property bool dragging: dragArea.pressed

    // The visible line; the whole 6px strip is the grab area, widened
    // a little more by the MouseArea margins.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        height: parent.height
        width: dragArea.pressed ? 2 : 1
        color: dragArea.pressed || dragArea.containsMouse
            ? theme.accent : theme.border
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        anchors.leftMargin: -2
        anchors.rightMargin: -2
        hoverEnabled: true
        cursorShape: Qt.SplitHCursor
        acceptedButtons: Qt.LeftButton

        // Scene-coordinate deltas: the seam itself moves while
        // dragging, so item-relative positions would feed back.
        property real pressSceneX: 0
        property int startWidth: 0

        onPressed: function(mouse) {
            pressSceneX = mapToItem(null, mouse.x, 0).x
            startWidth = seam.panelWidth
        }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            var dx = mapToItem(null, mouse.x, 0).x - pressSceneX
            var clamped = Math.max(seam.minWidth,
                                   Math.min(seam.maxWidth,
                                            startWidth + Math.round(dx)))
            if (clamped !== seam.panelWidth)
                seam.resized(clamped)
        }
    }
}
