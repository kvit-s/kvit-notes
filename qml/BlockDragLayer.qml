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

// What a block drag draws: the compact proxy for a multi-block selection and
// the line showing where that selection would land.
//
// A single-block drag live-moves its row through the list, so that row is the
// complete drag visual. A second, scaled copy under the pointer only made the
// block appear twice. A multi-block drag cannot live-move a discontiguous
// selection, so it keeps the compact proxy and draws an insertion gap. The
// indicator re-parents to the list's viewport rather than its content item,
// which is why its position subtracts contentY.
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

    // The floating multi-block proxy: snapshots of up to three selected
    // blocks stacked under the pointer, with a count badge for larger
    // selections. A single block is already represented by its live-moving
    // row and deliberately has no duplicate snapshot.
    Item {
        id: dragProxy
        objectName: "dragProxy"
        visible: layer.dragState.active && layer.dragState.isMulti
        z: 1000
        width: Interface.px(300)
        height: proxyColumn.height
        opacity: 0.85

        function grabShot(slot, sourceItem) {
            // Without a target size the grab renders the item at full
            // resolution, and a tall table, code fence or kanban delegate is
            // a large temporary image to make for a 300-pixel-wide proxy.
            // The scale below is the one the proxy displays at anyway: the
            // Image fits the shot into 300 x (height * 0.6), so grabbing
            // larger only produces pixels that are thrown away.
            var scale = Math.min(1,
                                 dragProxy.width / Math.max(1, sourceItem.width),
                                 0.6)
            var target = Qt.size(Math.max(1, Math.round(sourceItem.width * scale)),
                                 Math.max(1, Math.round(sourceItem.height * scale)))
            sourceItem.grabToImage(function(result) {
                if (slot < proxyImages.count)
                    proxyImages.setProperty(slot, "shotUrl", result.url.toString())
            }, target)
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
            spacing: Interface.px(2)
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
            width: Interface.px(22)
            height: Interface.px(22)
            radius: Interface.px(11)
            color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: layer.dragState.dragCount
                color: Theme.onAccent
                font.pixelSize: Interface.small
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
        height: Interface.px(3)
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
