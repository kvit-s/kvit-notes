// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The proxy stacks its snapshots through a Repeater, whose delegate is a
// separate component scope. Binding it lets the image address the proxy it
// is sized against instead of relying on injection.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// What a block drag draws: the proxy that follows the pointer, and the line
// showing where a multi-block drop would land.
//
// A single-block drag live-moves its row through the list, so the row itself
// shows where the block is going and the proxy is what the pointer carries.
// A multi-block drag cannot do that — the blocks are not contiguous — so it
// leaves them in place and draws the insertion gap instead. The indicator
// re-parents to the list's viewport rather than its content item, which is
// why its position subtracts contentY.
//
// Escape belongs here too: cancelling is part of the gesture, and this is the
// drag's only item in the window's tree.
Item {
    id: layer

    // Wired by main.qml.
    property var dragState
    property var listView

    // The controller drives the layer, and the proxy is an implementation
    // detail of it. These three forward to the item that holds the snapshot
    // model, so the controller has one object to talk to rather than two.
    function buildFrom(indexes) { dragProxy.buildFrom(indexes) }
    function moveTo(sceneX, sceneY) { dragProxy.moveTo(sceneX, sceneY) }
    function clear() { dragProxy.clear() }

    // The floating drag proxy: snapshots of up to three dragged blocks
    // stacked under the pointer, with a count badge for larger
    // selections.
    Item {
        id: dragProxy
        objectName: "dragProxy"
        visible: layer.dragState.active
        z: 1000
        width: 300
        height: proxyColumn.height
        opacity: 0.85

        function grabShot(slot, sourceItem) {
            sourceItem.grabToImage(function(result) {
                if (slot < proxyImages.count)
                    proxyImages.setProperty(slot, "shotUrl", result.url.toString())
            })
        }

        function buildFrom(indexes) {
            proxyImages.clear()
            var shots = Math.min(3, indexes.length)
            for (var i = 0; i < shots; i++) {
                var item = (layer.listView.itemAtIndex(
                Number(indexes[i]) as BlockDelegateBase))
                if (!item)
                    continue
                // A delegate can nominate its content item for the shot
                // (the math block nominates the rendered formula): grabbing
                // a full-width row and fitting it into the proxy shrinks
                // narrow content far below the intended 60%.
                var src = (item.dragGrabItem && item.dragGrabItem.visible)
                    ? item.dragGrabItem : item
                proxyImages.append({ shotUrl: "",
                                     shotHeight: Math.round(src.height * 0.6) })
                grabShot(proxyImages.count - 1, src)
            }
        }

        function moveTo(sceneX, sceneY) {
            x = sceneX + 12
            y = sceneY - 10
        }

        function clear() {
            proxyImages.clear()
        }

        ListModel { id: proxyImages }

        Column {
            id: proxyColumn
            spacing: 2
            Repeater {
                model: proxyImages
                Image {
                    id: proxyShot
                    required property real shotHeight
                    required property url shotUrl
                    width: dragProxy.width
                    height: proxyShot.shotHeight
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignLeft
                    source: proxyShot.shotUrl
                }
            }
        }

        Rectangle {
            visible: layer.dragState.dragCount > 1
            anchors.left: proxyColumn.right
            anchors.top: proxyColumn.top
            anchors.leftMargin: -12
            anchors.topMargin: -8
            width: 22
            height: 22
            radius: 11
            color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: layer.dragState.dragCount
                color: Theme.onAccent
                font.pixelSize: 11
                font.bold: true
            }
        }
    }

    // Multi-block drop indicator: a line naming the
    // insertion gap. Parented to the ListView viewport (not its
    // contentItem), so the y computation subtracts contentY.
    Rectangle {
        id: dropIndicator
        objectName: "dropIndicator"
        parent: layer.listView
        visible: layer.dragState.active && layer.dragState.isMulti
                 && layer.dragState.indicatorGap >= 0
        x: 40
        width: layer.listView.width - 48
        height: 3
        radius: 1.5
        color: Theme.accent
        y: {
            var gap = layer.dragState.indicatorGap
            if (gap < 0)
                return 0
            var yContent = 0
            if (gap < layer.listView.count) {
                var item = (layer.listView.itemAtIndex(gap) as BlockDelegateBase)
                yContent = item ? item.y - layer.listView.spacing / 2 : 0
            } else {
                var last = (layer.listView.itemAtIndex(
                    layer.listView.count - 1) as BlockDelegateBase)
                yContent = last ? last.y + last.height
                                  + layer.listView.spacing / 2 : 0
            }
            return yContent - layer.listView.contentY - height / 2
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: layer.dragState.active
        onActivated: layer.dragState.cancel()
    }
}
