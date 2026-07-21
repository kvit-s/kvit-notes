// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// What happens when something is dropped on the editor from outside the
// application (features.md §5.3, §5.4).
//
// A drop can arrive as three different things, and they are tried in that
// order: raw image bytes, which are written into the collection's asset
// store; URLs, where a local image or media file is ingested the same way and
// a remote one is linked as it stands; and plain text, where a bare image URL
// becomes an image and anything else splits into paragraphs at the drop
// point. Whatever the payload turns out to be, it lands as typed blocks after
// the block the pointer was over, and the last of them takes focus.
//
// A file: URL is handed over whole rather than stripped of its scheme, so
// that percent-encoded characters survive: a file named "photo #2.png"
// resolved to nothing when the scheme was cut off here, and the drop was
// silently ignored.
//
// The gesture itself cannot be scripted, so it is verified by screenshot; the
// ingestion underneath is unit-tested.
DropArea {
    id: dropArea

    // Wired by main.qml.
    property var appWindow
    property var listView

    property real dropY: -1

    function currentNoteSlug() {
        var p = DocumentManager.currentFilePath
        var fn = p.substring(p.lastIndexOf("/") + 1).replace(/\.[^.]+$/, "")
        var slug = fn.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "")
        return slug === "" ? "image" : slug
    }

    function assetRoot() {
        return NoteCollection.isOpen ? NoteCollection.rootPath : ""
    }

    // The block index a drop at content-y lands after (-1 → append).
    function dropTargetIndex(dropY) {
        var p = dropArea.mapToItem(dropArea.listView.contentItem, 0, dropY)
        var idx = dropArea.listView.indexAt(10, p.y)
        return idx  // -1 when below the last block
    }

    function insertBlocksAt(afterIndex, typedBlocks) {
        // typedBlocks: [{type, content}]. Insert after `afterIndex` (append
        // when -1); focus the last inserted block.
        var at = afterIndex < 0 ? BlockModel.count : afterIndex + 1
        var last = at
        for (var i = 0; i < typedBlocks.length; ++i) {
            BlockModel.insertBlock(at, typedBlocks[i].type, typedBlocks[i].content)
            last = at
            at++
        }
        // The dropped blocks can land outside the viewport, where no delegate
        // exists yet, so the window's retrying focus router is what puts the
        // caret in the last one rather than a single deferred itemAtIndex().
        dropArea.appWindow.focusBlockAtIndex(last)
    }

    // Turn a stored image/media path into the right block type by extension.
    function blockForPath(stored) {
        var kind = ImageAssets.kindOf(stored)
        var type = kind === "media" ? Block.Media
                 : Block.Image  // default images (and unknown local files show placeholder)
        return { type: type, content: ImageAssets.build(stored, "", "", 0) }
    }

    function handleEditorDrop(drop) {
        var afterIndex = dropArea.dropTargetIndex(drop.y)
        var slug = dropArea.currentNoteSlug()
        var root2 = dropArea.assetRoot()
        var nd = dropArea.appWindow.currentNoteDir()
        var blocks = []

        // 1) Raw image bytes (spike b's bytes arm), if delivered.
        var fmts = drop.formats || []
        for (var f = 0; f < fmts.length; ++f) {
            if (fmts[f] === "application/x-qt-image"
                || fmts[f].indexOf("image/") === 0) {
                var buf = drop.getDataAsArrayBuffer(fmts[f])
                if (buf) {
                    var storedB = AssetStore.ingestImageBytes(buf, slug, root2, nd)
                    if (storedB !== "") {
                        blocks.push({ type: Block.Image,
                            content: ImageAssets.build(storedB, "", "", 0) })
                        dropArea.insertBlocksAt(afterIndex, blocks)
                        return
                    }
                }
            }
        }

        // 2) URLs: local files ingest/link; http(s) image URLs stay remote.
        if (drop.hasUrls) {
            for (var u = 0; u < drop.urls.length; ++u) {
                var url = "" + drop.urls[u]
                if (url.indexOf("file://") === 0) {
                    // Hand the whole file:// URL over and let ingestLocalFile
                    // decode it with QUrl::toLocalFile(). Stripping the scheme
                    // here left %23 and %25 in the path, so a file named
                    // "photo #2.png" resolved to nothing and the drop was
                    // silently ignored.
                    if (ImageAssets.kindOf(url) === "none")
                        continue  // not an image/media file
                    var stored = AssetStore.ingestLocalFile(url, slug, root2, nd)
                    if (stored !== "")
                        blocks.push(dropArea.blockForPath(stored))
                } else if (url.indexOf("http") === 0) {
                    if (ImageAssets.kindOf(url) === "media")
                        blocks.push({ type: Block.Media, content: ImageAssets.build(url, "", "", 0) })
                    else
                        blocks.push({ type: Block.Image, content: ImageAssets.build(url, "", "", 0) })
                }
            }
            if (blocks.length > 0) {
                dropArea.insertBlocksAt(afterIndex, blocks)
                return
            }
        }

        // 3) Plain text: a bare image URL becomes a remote image; otherwise
        //    the text splits into paragraph blocks at the drop point (§5.4).
        if (drop.hasText) {
            var txt = ("" + drop.text).trim()
            if (txt.indexOf("http") === 0 && ImageAssets.kindOf(txt) !== "none") {
                blocks.push(dropArea.blockForPath(txt))
            } else {
                var lines = txt.split("\n")
                for (var l = 0; l < lines.length; ++l)
                    blocks.push({ type: Block.Paragraph, content: lines[l] })
            }
            if (blocks.length > 0)
                dropArea.insertBlocksAt(afterIndex, blocks)
        }
    }

    onEntered: function(drag) { drag.accepted = true; dropY = drag.y }
    onPositionChanged: function(drag) { dropY = drag.y }
    onExited: dropY = -1
    onDropped: function(drop) { dropY = -1; dropArea.handleEditorDrop(drop) }

    Rectangle {
        visible: dropArea.containsDrag
        width: parent.width
        height: 2
        radius: 1
        color: Theme.accent
        y: Math.max(0, dropArea.dropY)
    }
}
