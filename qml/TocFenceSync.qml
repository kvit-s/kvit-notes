// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Keeping a table-of-contents fence's stored body in step with the document's
// headings.
//
// The delegate renders the live outline, so this is persistence rather than
// display: what it writes is what the file says, and what an export or a
// serialize therefore sees. It writes through updateContentSilently, which
// bypasses the undo stack, so a heading edit never spawns an undo entry of its
// own here and a freshly-loaded note is not dirtied by being looked at.
//
// The work is coalesced behind a short timer because both triggers — the
// outline changing and the set of fences changing — can fire repeatedly while
// the document loads.
Item {
    id: sync

    function syncTocBlocks() {
        if (!BlockModel || BlockModel.tocBlockCount === 0)
            return
        var tocIndexes = BlockModel.tocBlockIndexes()
        if (tocIndexes.length === 0)
            return

        var perfOn = PerfLog && PerfLog.enabled
        var scanned = 0
        var updated = 0
        if (perfOn)
            PerfLog.begin("toc.sync", {
                "blocks": BlockModel.count,
                "tocBlocks": tocIndexes.length
            })
        try {
            var toc = DocumentOutline.tocMarkdown()
            for (var n = 0; n < tocIndexes.length; n++) {
                var i = tocIndexes[n]
                scanned++
                var b = BlockModel.blockAt(i)
                if (b && b.blockType === 8 && b.language === "toc"
                    && b.content !== toc) {
                    BlockModel.updateContentSilently(i, toc)
                    updated++
                }
            }
        } finally {
            if (perfOn)
                PerfLog.end("toc.sync", {
                    "scanned": scanned,
                    "updated": updated
                })
        }
    }

    Timer {
        id: tocSyncTimer
        interval: 50
        onTriggered: sync.syncTocBlocks()
    }

    // The headings changed.
    Connections {
        target: DocumentOutline
        function onRevisionChanged() { tocSyncTimer.restart() }
    }

    // The set of fences changed.
    Connections {
        target: BlockModel
        function onTocBlockIndexesChanged() { tocSyncTimer.restart() }
    }
}
