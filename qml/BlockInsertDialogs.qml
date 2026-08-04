// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Window
import Kvit 1.0

// Putting an image, a web embed or a table into an empty block (features.md
// §4.2, §4.3, §1.2.14).
//
// All three work the same way: ask for what the block needs, then convert the
// target block in one model call, which makes the insertion one undo step,
// and focus it afterwards. The image dialog is shared with audio and video,
// because the type follows from the path — a media extension lands a Media
// block and everything else an Image, including a path that turns out to be
// neither, which renders as a placeholder rather than failing.
//
// A block-type conversion may replace the row's delegate, so the row is
// asked for by index afterwards rather than held across the call: the window
// scrolls it into view and gives it the caret once its delegate exists.
Item {
    id: inserts

    // Asked of the window once a dialog has converted its block: bring that
    // row into view and give it the caret. The window's router is what does
    // both, because a converted block's delegate is rebuilt and can be
    // several frames away from existing at its final height — an inserted
    // table is the visible case, since it replaces a one-line paragraph with
    // a grid that is usually taller than the space the paragraph occupied.
    signal focusBlockRequested(int index)

    // Whether the block a dialog was opened for is still the block it was
    // opened for.
    //
    // Each of these dialogs holds a row number, and the document under that
    // row can be replaced while the dialog is on screen: a tray action opens
    // another note, an externally-changed note is reloaded, a linked module
    // switches the view. Committing then converts a block in a note the reader
    // was not editing, and a conversion replaces that block's content
    // outright. Comparing the text the dialog was opened over is the whole
    // check — if the row still reads the same, it is still the same row.
    function targetIsStill(idx, snapshot) {
        return idx >= 0 && idx < BlockModel.count
            && BlockModel.getContent(idx) === snapshot
    }

    function refuseStaleTarget() {
        AppActions.requestTransientStatus(
            qsTr("The document changed while the dialog was open, so nothing was inserted."))
    }

    // Insert an image or a local audio/video file into an (empty) block by
    // file or URL (features.md §4.3). One dialog serves both entries in the
    // block menu, because the block type follows from the path that comes
    // back rather than from what was asked for. What was asked for is still
    // what the dialog says and what its file picker offers: choosing
    // "Audio / Video" and being handed a picker called "Choose an image" that
    // filters every audio file out is the dialog answering a question nobody
    // put to it. kind is "image" (the default) or "media".
    function insertImage(idx, kind) {
        imageInsertDialog.targetIndex = idx
        imageInsertDialog.targetContent = BlockModel.getContent(idx)
        imageInsertDialog.kind = (kind === "media") ? "media" : "image"
        imagePathField.text = ""
        imageInsertDialog.open()
        imagePathField.forceActiveFocus()
    }

    // §1.2.14 web embed: prompt for a URL and insert an ![](url) image
    // expression, which the content classifier renders as a preview card.
    function insertEmbed(idx) {
        embedInsertDialog.targetIndex = idx
        embedInsertDialog.targetContent = BlockModel.getContent(idx)
        embedInsertDialog.editing = false
        embedUrlField.text = ""
        embedInsertDialog.open()
        embedUrlField.forceActiveFocus()
    }

    // Change the URL an existing embed card names (§1.2.14). The same dialog,
    // seeded with the current URL and selected so typing replaces it; the card
    // is rewritten in place rather than re-converted, so its stored size
    // survives the edit.
    function editEmbed(idx, url) {
        embedInsertDialog.targetIndex = idx
        embedInsertDialog.targetContent = BlockModel.getContent(idx)
        embedInsertDialog.editing = true
        embedUrlField.text = url
        embedInsertDialog.open()
        embedUrlField.forceActiveFocus()
        embedUrlField.selectAll()
    }

    KvitDialog {
        id: embedInsertDialog
        // Opens on the field it is about, so a screen reader
        // announces something to type into and a keyboard user
        // does not have to guess how many tabs reach it.
        initialFocusItem: embedUrlField
        objectName: "embedInsertDialog"
        title: editing ? qsTr("Edit web embed") : qsTr("Insert web embed")
        modal: true
        anchors.centerIn: parent
        width: Interface.px(420)
        standardButtons: Dialog.Ok | Dialog.Cancel
        property int targetIndex: -1
        property string targetContent: ""
        property bool editing: false
        // What the typed text will actually be inserted as: a bare host gains
        // https://, and text that cannot be a web address yields "", which is
        // what disables OK below.
        readonly property string resolvedUrl:
            ImageAssets.normalizeEmbedUrl(embedUrlField.text)
        function commit() {
            var url = resolvedUrl
            if (url === "" || targetIndex < 0)
                return
            if (!inserts.targetIsStill(targetIndex, targetContent)) {
                inserts.refuseStaleTarget()
                return
            }
            var md = "![](" + url + ")"
            // updateContent leaves the block's type and attributes alone,
            // which is what keeps a resized card's width and height; the
            // insert path has neither to preserve and converts the block.
            if (editing)
                BlockModel.updateContent(targetIndex, md)
            else
                BlockModel.convertBlock(targetIndex, Block.Image, md)
            inserts.focusBlockRequested(targetIndex)
        }
        onAccepted: commit()
        // OK stays disabled until the field holds something that can be
        // fetched, so the dialog cannot leave a block naming an address the
        // card would only be able to report as broken.
        onOpened: {
            var ok = standardButton(Dialog.Ok)
            if (ok)
                ok.enabled = Qt.binding(function() {
                    return embedInsertDialog.resolvedUrl !== ""
                })
        }
        contentItem: Column {
            spacing: Interface.px(4)
            TextField {
                id: embedUrlField
                objectName: "embedUrlField"
                width: parent.width
                placeholderText: qsTr("Web page or video URL (example.com, https://…)")
                onAccepted: {
                    if (embedInsertDialog.resolvedUrl === "")
                        return
                    embedInsertDialog.commit()
                    embedInsertDialog.close()
                }
            }
            // Shown only when the typed text is not what gets stored, so the
            // reader sees the scheme that was added for them.
            Text {
                objectName: "embedUrlHint"
                width: parent.width
                visible: embedInsertDialog.resolvedUrl !== ""
                    && embedInsertDialog.resolvedUrl !== embedUrlField.text.trim()
                text: qsTr("Opens %1").arg(embedInsertDialog.resolvedUrl)
                elide: Text.ElideMiddle
                font.pixelSize: Interface.small
                color: Theme.textFaint
            }
        }
    }

    // Insert a table via the grid-size picker (features.md §4.2).
    function insertTable(idx) {
        tableSizePicker.targetIndex = idx
        tableSizePicker.targetContent = BlockModel.getContent(idx)
        tableSizePicker.open()
    }

    // Table grid-size picker (§4.2). On a size choice it converts the target
    // block to a Table with an empty grid of that size.
    TableSizePicker {
        id: tableSizePicker
        objectName: "tableSizePicker"
        anchors.centerIn: parent
        property int targetIndex: -1
        property string targetContent: ""
        onSizePicked: function(cols, rows) {
            if (targetIndex < 0) return
            if (!inserts.targetIsStill(targetIndex, targetContent)) {
                inserts.refuseStaleTarget()
                return
            }
            BlockModel.convertBlock(targetIndex, Block.Table,
                                    TableTools.emptyTable(cols, rows))
            inserts.focusBlockRequested(targetIndex)
        }
    }

    // Insert-image dialog (§4.3): a path/URL field with a file browser. On
    // accept it converts the target block into an Image block whose content
    // is the built markdown expression (one undo step).
    KvitDialog {
        id: imageInsertDialog
        // Opens on the field it is about, so a screen reader
        // announces something to type into and a keyboard user
        // does not have to guess how many tabs reach it.
        initialFocusItem: imagePathField
        objectName: "imageInsertDialog"
        title: mediaKind ? qsTr("Insert audio or video") : qsTr("Insert image")
        modal: true
        anchors.centerIn: parent
        width: Interface.px(420)
        standardButtons: Dialog.Ok | Dialog.Cancel
        property int targetIndex: -1
        property string targetContent: ""
        // Which of the two menu entries opened this: "image" or "media".
        property string kind: "image"
        readonly property bool mediaKind: kind === "media"

        function commit() {
            var path = imagePathField.text.trim()
            if (path === "" || targetIndex < 0)
                return
            if (!inserts.targetIsStill(targetIndex, targetContent)) {
                inserts.refuseStaleTarget()
                return
            }
            var md = ImageAssets.build(path, "", "", 0)
            // An audio/video path lands a Media block; everything else an
            // Image. The dialog is shared.
            var type = ImageAssets.parse(md).kind === "media"
                     ? Block.Media : Block.Image
            BlockModel.convertBlock(targetIndex, type, md)
            inserts.focusBlockRequested(targetIndex)
        }
        onAccepted: commit()

        contentItem: Row {
            spacing: Interface.px(6)
            TextField {
                id: imagePathField
                objectName: "imagePathField"
                width: Interface.px(320)
                placeholderText: imageInsertDialog.mediaKind
                    ? qsTr("Audio or video file path or URL")
                    : qsTr("Image file path or URL")
                onAccepted: { imageInsertDialog.commit(); imageInsertDialog.close() }
            }
            Button {
                text: qsTr("Browse…")
                onClicked: imageFileDialog.open()
            }
        }
    }

    FileDialog {
        id: imageFileDialog
        objectName: "imageFileDialog"
        // Both lines are about who owns this dialog, and neither changes a
        // platform that has a file chooser of its own: Qt still uses the
        // native one, and hands it the parent window it should be modal to.
        //
        // Where there is no native chooser, Qt builds the dialog itself, and
        // an unowned one becomes a top-level window. Closing that window on
        // Wayland leaves the compositor with nothing to give the keyboard
        // focus back to, and the application cannot take it back either,
        // because a Wayland client cannot activate itself. The window keeps
        // drawing and keeps following the pointer while nothing typed
        // arrives anywhere, which reads as a hang. Popup.Item keeps that
        // built dialog inside this window, where no focus changes hands.
        parentWindow: inserts.Window.window
        popupType: Popup.Item
        title: imageInsertDialog.mediaKind ? qsTr("Choose an audio or video file")
                                           : qsTr("Choose an image")
        // From the extension sets the classifier itself reads, so the picker
        // cannot offer a file the app would refuse or hide one it accepts.
        nameFilters: ImageAssets.nameFilters(imageInsertDialog.kind)
        onAccepted: {
            // Store the chosen file's path; ingestion/copy comes later.
            // The conversion goes through C++: stripping "file://" left
            // %20/%23/%25 escapes in the path, so a picked file whose name
            // contains a space or a hash was inserted as Markdown pointing at
            // a file that does not exist.
            imagePathField.text = DocumentManager.toLocalPath(selectedFile)
        }
    }
}
