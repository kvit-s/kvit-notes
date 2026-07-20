// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// Full-window image lightbox (features.md §1.2.8): a dimmed overlay showing
// the image at natural size (capped to the window), dismissed by Escape or
// a click. Instantiated once in main.qml and opened via
// win.openLightbox(source, alt).
Item {
    id: lightbox
    anchors.fill: parent
    visible: shown
    z: 2000

    property bool shown: false
    property string source: ""
    property string altText: ""

    function open(src, alt) {
        source = src
        altText = alt || ""
        shown = true
        keyCatcher.forceActiveFocus()
    }
    function close() {
        shown = false
        source = ""
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.82
        MouseArea {
            anchors.fill: parent
            onClicked: lightbox.close()
        }
    }

    Image {
        anchors.centerIn: parent
        source: lightbox.source
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        width: Math.min(implicitWidth, lightbox.width - 80)
        height: Math.min(implicitHeight, lightbox.height - 80)
        // A click on the image itself also closes (Obsidian behavior).
        MouseArea {
            anchors.fill: parent
            onClicked: lightbox.close()
        }
    }

    Item {
        id: keyCatcher
        focus: lightbox.shown
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                lightbox.close()
                event.accepted = true
            }
        }
    }
}
