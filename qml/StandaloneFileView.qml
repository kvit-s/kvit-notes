// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

Rectangle {
    id: view
    objectName: "standaloneFileView"
    color: Theme.windowBackground

    property string path: ""
    property string kind: ""

    function openFile(filePath, fileKind) {
        path = filePath
        kind = fileKind
        if (fileKind === "image")
            imageSurface.forceActiveFocus(Qt.OtherFocusReason)
        else if (fileKind === "media")
            mediaCard.forceActiveFocus(Qt.OtherFocusReason)
    }

    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Interface.px(36)
        color: Theme.panelBackground
        border.color: Theme.border
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Interface.px(12)
            anchors.rightMargin: Interface.px(8)
            Label {
                Layout.fillWidth: true
                text: view.path
                elide: Text.ElideMiddle
                color: Theme.textSecondary
                font.pixelSize: Interface.small
            }
            Button {
                text: qsTr("Open with desktop")
                onClicked: UrlLauncher.open(
                    DocumentManager.toLocalFileUrl(view.path).toString())
            }
        }
    }

    ImageViewerSurface {
        id: imageSurface
        objectName: "standaloneImageSurface"
        visible: view.kind === "image"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        // Keep standalone images behind the same decoded-pixel budget as
        // pictures embedded in notes and the lightbox.
        source: visible ? EgressPolicy.imageSourceFor(
                              DocumentManager.toLocalFileUrl(
                                  view.path).toString()) : ""
        accessibleName: view.path.split("/").pop()
    }

    MediaPlayerCard {
        id: mediaCard
        objectName: "standaloneMediaCard"
        visible: view.kind === "media"
        anchors.centerIn: mediaArea
        width: Math.min(implicitWidth, mediaArea.width - Interface.px(48))
        source: visible ? DocumentManager.toLocalFileUrl(view.path).toString() : ""
        displayName: view.path
        title: view.path.split("/").pop()
        showDesktopAction: true
    }
    Item {
        id: mediaArea
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        z: -1
    }
}
