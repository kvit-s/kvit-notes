// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The picture an image block shows in a drawn document (selection.md
// "A document drawn read-only").
//
// An image block keeps its markdown expression — ![alt|width](path "caption")
// — as its content, so a surface that sent every non-verbatim block through
// the text engine drew the expression instead of the picture. This is the
// read-only counterpart of qml/ImageBlock.qml: the same parse, the same path
// resolution and the same remote-image consent gate, without the resize
// handle, the effects popover, the editable caption and the lightbox, none of
// which a surface has any way to act on.
//
// Path resolution needs to know where the document being drawn lives, which
// is not necessarily where the open note lives: a stored version sits in the
// backup tree, and a surface may be built from a string with no file behind
// it at all. That is what `baseDir` is, handed down from the surface.
//
// A remote image is not fetched because a preview drew it. A note is
// untrusted input (docs/adr/0003-network-egress-policy.md) and a preview of
// one is no different, so the source goes through EgressPolicy exactly as the
// editor's does: empty until the reader approves the origin, with the same
// consent tile offering to load it rather than a broken-image placeholder.
Item {
    id: picture

    // ---- what the row hands the picture ----

    // The block's stored markdown expression.
    property string content: ""
    // The directory a relative path in that expression is written against.
    property string baseDir: ""
    // A media block (audio or video) rather than an image. A surface cannot
    // play anything, so a media block draws a tile naming the file instead of
    // a player; it is otherwise treated exactly as a picture is, so a sweep
    // that crosses one takes it whole and copies its expression.
    property bool media: false

    // ---- the expression, resolved ----

    readonly property var img: ImageAssets.parse(picture.content)
    readonly property string resolvedSource: ImageAssets.resolve(
        picture.img.path, picture.baseDir,
        NoteCollection.isOpen ? NoteCollection.rootPath : "")
    // What the Image actually loads: a local file passes through, an http(s)
    // image is routed to the image://remote provider once the reader has
    // approved its origin and is "" until then. Reading the policy's revision
    // keeps this live across an approval, so a picture appears as soon as the
    // origin is allowed without the pane around it being rebuilt.
    readonly property string displaySource: {
        var revision = EgressPolicy.revision
        return EgressPolicy.imageSourceFor(picture.resolvedSource)
    }
    readonly property bool awaitingConsent: !picture.media
        && picture.resolvedSource !== "" && picture.displaySource === ""
    // Whether there is a picture on screen, as opposed to one of the two
    // tiles below. Asked of the resolved source rather than of the Image's
    // own `source`, which is a url value and does not compare equal to the
    // empty string.
    readonly property bool showsPicture: !picture.media
        && picture.displaySource !== "" && image.status !== Image.Error

    // ---- geometry ----

    // The stored width, honoured, but never wider than the pane the surface
    // was given: a preview is narrower than the editor and an image sized for
    // the editor would otherwise be cut off at the pane's edge.
    readonly property int maxWidth: Math.max(40, Math.floor(picture.width))
    readonly property int displayWidth: {
        var w = picture.img.width > 0 ? picture.img.width
              : (image.implicitWidth > 0 ? image.implicitWidth : 320)
        return Math.min(w, picture.maxWidth)
    }
    // The width to decode at, which is deliberately NOT the displayed width.
    // An image with no stored width is shown at its natural size, so the
    // displayed width is read back out of what was decoded; asking to decode
    // at the displayed width would make the two define each other, which QML
    // reports as a binding loop and then resolves arbitrarily. The pane's own
    // width is the ceiling instead, and a ceiling is all sourceSize is: a
    // file smaller than it still decodes at its own size.
    readonly property int decodeWidth: picture.img.width > 0
        ? Math.min(picture.img.width, picture.maxWidth) : picture.maxWidth
    // The height a tile takes when there is no picture to measure: an
    // unresolved path, an unapproved origin, or a media file.
    readonly property int tileHeight: 160

    implicitHeight: layout.implicitHeight

    Column {
        id: layout
        width: picture.width
        spacing: 4

        Item {
            id: frame
            objectName: "readOnlyPictureFrame"
            width: picture.displayWidth
            height: (picture.showsPicture && image.status === Image.Ready
                     && image.implicitWidth > 0)
                ? Math.round(picture.displayWidth
                             * (image.implicitHeight / image.implicitWidth))
                : picture.tileHeight

            // Alt text surfaced to assistive technology, falling back to the
            // caption and then to a generic label, so a picture in a preview
            // is never nameless (accessibility.md).
            Accessible.role: Accessible.Graphic
            Accessible.name: picture.img.alt !== "" ? picture.img.alt
                : (picture.img.caption !== "" ? picture.img.caption
                   : (picture.media ? qsTr("Media") : qsTr("Image")))

            Image {
                id: image
                objectName: "readOnlyPictureImage"
                anchors.fill: parent
                source: picture.media ? "" : picture.displaySource
                asynchronous: true
                cache: true
                // Display geometry is width/height; sourceSize is what asks
                // for a scaled decode, so a photograph shown 400 px wide in a
                // preview pane is not decoded at its full camera resolution.
                sourceSize.width: Math.max(
                    1, Math.ceil(picture.decodeWidth
                                 * frame.Screen.devicePixelRatio))
                fillMode: Image.PreserveAspectFit
                visible: picture.showsPicture
                // The editor's own image effects (rounded corners, shadow,
                // border, the stretch override) are block attributes rather
                // than part of the expression, and a surface draws a
                // document rather than a note's presentation, so they are
                // not applied here.
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: image.status === Image.Loading
                visible: running
                width: 32
                height: 32
            }

            // An unapproved remote image: an inert tile that offers to load
            // it. Distinct from the broken-path tile below, because nothing
            // is broken — the image simply has not been requested.
            Rectangle {
                objectName: "readOnlyPictureConsent"
                anchors.fill: parent
                visible: picture.awaitingConsent
                color: Theme.codePanelBackground
                border.color: Theme.border
                radius: 6
                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "🔗"
                        font.pixelSize: Interface.px(24)
                        color: Theme.textFaint
                        Accessible.ignored: true
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Remote image not loaded")
                        color: Theme.textMuted
                        font.pixelSize: Interface.body
                        Accessible.ignored: true
                    }
                    // A real Button rather than a rectangle with a mouse
                    // handler, so approving an origin has a name, a role and
                    // a tab stop. It is the one control a surface's rows
                    // hold, and the surface stacks its rows above the sweep
                    // area so that this press reaches it.
                    Button {
                        id: loadButton
                        objectName: "readOnlyPictureLoadButton"
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: EgressPolicy.canRequestConsent(
                            picture.resolvedSource)
                        activeFocusOnTab: true
                        padding: 4
                        font.pixelSize: Interface.small
                        text: qsTr("Load image")
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Load this remote image")
                        background: Rectangle {
                            radius: 4
                            color: Theme.hoverTint
                            border.width: loadButton.visualFocus ? 2 : 1
                            border.color: loadButton.visualFocus
                                          ? Theme.focusRing
                                          : (loadButton.hovered ? Theme.accent
                                             : Theme.borderStrong)
                        }
                        // Approving an origin is a change to the reader's own
                        // policy rather than to the document being drawn, so
                        // this leaves the surface as read-only as it was.
                        onClicked: EgressPolicy.allowOrigin(
                            picture.resolvedSource)
                    }
                }
            }

            // A media file, or a path that resolved to nothing.
            Rectangle {
                objectName: "readOnlyPicturePlaceholder"
                anchors.fill: parent
                visible: !picture.awaitingConsent && !picture.showsPicture
                color: Theme.codePanelBackground
                border.color: Theme.border
                radius: 6
                Column {
                    anchors.centerIn: parent
                    spacing: 4
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: picture.media ? "▶" : "▨"
                        font.pixelSize: Interface.px(28)
                        color: Theme.textFaint
                        Accessible.ignored: true
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(implicitWidth, frame.width - 16)
                        elide: Text.ElideMiddle
                        color: Theme.textMuted
                        font.pixelSize: Interface.body
                        text: {
                            if (picture.img.path === "")
                                return picture.media ? qsTr("No media")
                                                     : qsTr("No image")
                            if (picture.media)
                                return picture.img.path
                            return qsTr("Image not found: ") + picture.img.path
                        }
                        Accessible.ignored: true
                    }
                }
            }
        }

        // The caption, as text rather than as the editable field the editor
        // puts here: a surface draws a document and never writes one.
        Text {
            objectName: "readOnlyPictureCaption"
            visible: picture.img.caption !== ""
            width: frame.width
            text: picture.img.caption
            wrapMode: Text.Wrap
            font.pixelSize: Interface.body
            font.italic: true
            color: Theme.textMuted
            Accessible.role: Accessible.StaticText
            Accessible.name: picture.img.caption
        }
    }
}
