// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
// A block-type conversion may replace the row's delegate, so focus is
// re-established by index on the next tick rather than held across the call.
Item {
    id: inserts

    // Wired by main.qml.
    property var listView

    // Insert an image into an (empty) block by file or URL (features.md §4.3).
    function insertImage(idx) {
        imageInsertDialog.targetIndex = idx
        imagePathField.text = ""
        imageInsertDialog.open()
        imagePathField.forceActiveFocus()
    }

    // §1.2.14 web embed: prompt for a URL and insert an ![](url) image
    // expression, which the content classifier renders as a preview card.
    function insertEmbed(idx) {
        embedInsertDialog.targetIndex = idx
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
        embedInsertDialog.editing = true
        embedUrlField.text = url
        embedInsertDialog.open()
        embedUrlField.forceActiveFocus()
        embedUrlField.selectAll()
    }

    Dialog {
        id: embedInsertDialog
        objectName: "embedInsertDialog"
        title: editing ? qsTr("Edit web embed") : qsTr("Insert web embed")
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.Ok | Dialog.Cancel
        property int targetIndex: -1
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
            var md = "![](" + url + ")"
            // updateContent leaves the block's type and attributes alone,
            // which is what keeps a resized card's width and height; the
            // insert path has neither to preserve and converts the block.
            if (editing)
                BlockModel.updateContent(targetIndex, md)
            else
                BlockModel.convertBlock(targetIndex, Block.Image, md)
            var idx = targetIndex
            Qt.callLater(function() {
                var item = (inserts.listView.itemAtIndex(idx) as BlockDelegateBase)
                if (item && item.focusAtStart) item.focusAtStart()
            })
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
            spacing: 4
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
                font.pixelSize: 11
                color: Theme.textFaint
            }
        }
    }

    // Insert a table via the grid-size picker (features.md §4.2).
    function insertTable(idx) {
        tableSizePicker.targetIndex = idx
        tableSizePicker.open()
    }

    // Table grid-size picker (§4.2). On a size choice it converts the target
    // block to a Table with an empty grid of that size.
    TableSizePicker {
        id: tableSizePicker
        objectName: "tableSizePicker"
        anchors.centerIn: parent
        property int targetIndex: -1
        onSizePicked: function(cols, rows) {
            if (targetIndex < 0) return
            BlockModel.convertBlock(targetIndex, Block.Table,
                                    TableTools.emptyTable(cols, rows))
            var idx = targetIndex
            Qt.callLater(function() {
                var item = (inserts.listView.itemAtIndex(idx) as BlockDelegateBase)
                if (item && item.focusAtStart) item.focusAtStart()
            })
        }
    }

    // Insert-image dialog (§4.3): a path/URL field with a file browser. On
    // accept it converts the target block into an Image block whose content
    // is the built markdown expression (one undo step).
    Dialog {
        id: imageInsertDialog
        objectName: "imageInsertDialog"
        title: qsTr("Insert image")
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.Ok | Dialog.Cancel
        property int targetIndex: -1

        function commit() {
            var path = imagePathField.text.trim()
            if (path === "" || targetIndex < 0)
                return
            var md = ImageAssets.build(path, "", "", 0)
            // An audio/video path lands a Media block; everything else an
            // Image. The dialog is shared.
            var type = ImageAssets.parse(md).kind === "media"
                     ? Block.Media : Block.Image
            BlockModel.convertBlock(targetIndex, type, md)
            var idx = targetIndex
            Qt.callLater(function() {
                var item = (inserts.listView.itemAtIndex(idx) as BlockDelegateBase)
                if (item && item.focusAtStart) item.focusAtStart()
            })
        }
        onAccepted: commit()

        contentItem: Row {
            spacing: 6
            TextField {
                id: imagePathField
                objectName: "imagePathField"
                width: 320
                placeholderText: qsTr("Image file path or URL")
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
        title: qsTr("Choose an image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.webp *.svg *.bmp)"),
                      qsTr("All files (*)")]
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
