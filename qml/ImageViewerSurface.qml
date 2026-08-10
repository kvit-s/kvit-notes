// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The image surface shared by the document lightbox and the standalone file
// view. It fits initially, then zooms around the pointer and pans through the
// ordinary Flickable interaction. Callers supply an already-approved source;
// the component never turns a remote URL into a request itself.
FocusScope {
    id: viewer

    property string source: ""
    property string accessibleName: qsTr("Image")
    property real zoom: 1.0
    readonly property bool ready: picture.status === Image.Ready
    readonly property bool failed: picture.status === Image.Error
    readonly property real fitScale: {
        if (picture.implicitWidth <= 0 || picture.implicitHeight <= 0)
            return 1
        return Math.min(1, Math.min(canvas.width / picture.implicitWidth,
                                    canvas.height / picture.implicitHeight))
    }
    Accessible.role: Accessible.Graphic
    Accessible.name: accessibleName

    function resetView() {
        zoom = fitScale
        Qt.callLater(function() {
            canvas.contentX = Math.max(0, (canvas.contentWidth - canvas.width) / 2)
            canvas.contentY = Math.max(0, (canvas.contentHeight - canvas.height) / 2)
        })
    }

    function setZoom(next, localX, localY) {
        if (!ready)
            return
        var bounded = Math.max(fitScale * 0.5, Math.min(8, next))
        var oldWidth = Math.max(1, canvas.contentWidth)
        var oldHeight = Math.max(1, canvas.contentHeight)
        var relX = (canvas.contentX + localX) / oldWidth
        var relY = (canvas.contentY + localY) / oldHeight
        zoom = bounded
        Qt.callLater(function() {
            canvas.contentX = Math.max(0, Math.min(
                canvas.contentWidth - canvas.width,
                relX * canvas.contentWidth - localX))
            canvas.contentY = Math.max(0, Math.min(
                canvas.contentHeight - canvas.height,
                relY * canvas.contentHeight - localY))
        })
    }

    onSourceChanged: {
        zoom = 1
        canvas.contentX = 0
        canvas.contentY = 0
    }

    Keys.onPressed: function(event) {
        if (event.modifiers & Qt.ControlModifier) {
            if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
                setZoom(zoom * 1.25, width / 2, height / 2)
                event.accepted = true
            } else if (event.key === Qt.Key_Minus) {
                setZoom(zoom / 1.25, width / 2, height / 2)
                event.accepted = true
            } else if (event.key === Qt.Key_0) {
                resetView()
                event.accepted = true
            }
        }
    }

    Flickable {
        id: canvas
        objectName: "imageViewerFlickable"
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, picture.implicitWidth * viewer.zoom)
        contentHeight: Math.max(height, picture.implicitHeight * viewer.zoom)

        Image {
            id: picture
            objectName: "imageViewerImage"
            x: Math.max(0, (canvas.contentWidth - width) / 2)
            y: Math.max(0, (canvas.contentHeight - height) / 2)
            width: Math.max(1, implicitWidth * viewer.zoom)
            height: Math.max(1, implicitHeight * viewer.zoom)
            source: viewer.source
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectFit
            Accessible.role: Accessible.Graphic
            Accessible.name: viewer.accessibleName
            onStatusChanged: if (status === Image.Ready)
                                 Qt.callLater(viewer.resetView)
        }

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) {
                var direction = event.angleDelta.y >= 0 ? 1.2 : 1 / 1.2
                viewer.setZoom(viewer.zoom * direction,
                               event.x, event.y)
            }
        }

        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: picture.status === Image.Loading
        visible: running
    }

    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Interface.px(12)
        spacing: Interface.px(4)
        visible: viewer.ready
        Button {
            text: "−"
            Accessible.name: qsTr("Zoom out")
            onClicked: viewer.setZoom(viewer.zoom / 1.25,
                                      viewer.width / 2, viewer.height / 2)
        }
        Button {
            text: Math.round(viewer.zoom * 100) + "%"
            Accessible.name: qsTr("Fit image")
            onClicked: viewer.resetView()
        }
        Button {
            text: "+"
            Accessible.name: qsTr("Zoom in")
            onClicked: viewer.setZoom(viewer.zoom * 1.25,
                                      viewer.width / 2, viewer.height / 2)
        }
    }
}
