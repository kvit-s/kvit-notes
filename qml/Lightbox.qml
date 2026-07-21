// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Window

// Full-window image lightbox (features.md §1.2.8): a dimmed overlay showing
// the image at natural size (capped to the window), dismissed by Escape, the
// Close button, or a click outside the image. Instantiated once in main.qml
// and opened via win.openLightbox(source, alt).
//
// It is a modal Popup rather than a high-z Item because painting over the
// window is not the same as owning the input: Tab moved through the editor
// behind the overlay, nothing restored the focus the viewer took when it
// closed, and a screen reader was handed an anonymous zero-size item to
// announce. A modal Popup keeps Tab inside itself while it is open, returns
// the focus to whatever had it before, and lets the image and the Close
// button carry their own accessible names.
Popup {
    id: lightbox

    modal: true
    focus: true
    // The background below is the dimmer, covering the whole window; the
    // overlay's own dimming would only double it.
    dim: false
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    // Kept for the callers and tests that ask whether the viewer is up.
    readonly property bool shown: lightbox.opened
    property string source: ""
    property string altText: ""

    // Whatever held the keyboard when the viewer opened, so closing can hand
    // it back rather than leaving the editor with no caret.
    property Item focusBeforeOpen: null
    // Untyped: the attached Window.window is a QQuickWindow, which does not
    // assign to the QML `Window` type.
    readonly property var hostWindow:
        lightbox.parent ? lightbox.parent.Window.window : null

    // Named openImage rather than open() so it does not shadow Popup's own.
    function openImage(src, alt) {
        lightbox.focusBeforeOpen =
            lightbox.hostWindow ? lightbox.hostWindow.activeFocusItem : null
        lightbox.source = src
        lightbox.altText = alt || ""
        lightbox.open()
    }

    background: Rectangle {
        color: "#000000"
        opacity: 0.82
    }

    onClosed: {
        lightbox.source = ""
        if (lightbox.focusBeforeOpen)
            lightbox.focusBeforeOpen.forceActiveFocus()
        lightbox.focusBeforeOpen = null
    }

    contentItem: FocusScope {
        focus: true
        // The dialog's own name for a screen reader; it lives on the content
        // item because Accessible attaches to Items, not to the Popup.
        Accessible.role: Accessible.Dialog
        Accessible.name: lightbox.altText !== "" ? lightbox.altText
                                                 : qsTr("Image viewer")

        Image {
            id: fullImage
            objectName: "lightboxImage"
            anchors.centerIn: parent
            source: lightbox.source
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            width: Math.min(implicitWidth, lightbox.width - 80)
            height: Math.min(implicitHeight, lightbox.height - 80)
            // The image is what this dialog is about, so it is what a screen
            // reader should read: its alt text, or a plain fallback.
            Accessible.role: Accessible.Graphic
            Accessible.name: lightbox.altText !== "" ? lightbox.altText
                                                     : qsTr("Image")
            // A click on the image itself also closes (Obsidian behavior).
            MouseArea {
                anchors.fill: parent
                onClicked: lightbox.close()
            }
        }

        // A named, keyboard-reachable way out, for anyone who cannot reach
        // Escape or does not know it applies here. It takes the initial
        // focus, so the popup opens with something operable selected.
        Button {
            id: closeButton
            objectName: "lightboxCloseButton"
            focus: true
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 12
            text: qsTr("Close")
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Close image viewer")
            onClicked: lightbox.close()
        }
    }
}
