// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// The status bar (features.md §9.7): what the window reports about the
// document it is showing — save state and last-saved time, caret position,
// block type and count, word and character counts, and progress toward the
// note's writing goal — plus the two passive notices that have nowhere else
// to live, a pending update and the offer to turn a lone file's folder into
// a vault.
//
// The counting is the reason this is one component rather than a strip of
// labels. A count is taken over display text, so it walks every block through
// the markdown parser; the coalescing timer, the several selection cases and
// the whole-document fallback in docCounter below are all one piece of
// behaviour, and the labels are its readout.
//
// Everything it needs from the window arrives through the properties below,
// and the two dialogs it can open belong to the window rather than to this
// bar, so it asks for them by signal.
Rectangle {
    id: statusBar
    objectName: "statusBar"

    // Wired by main.qml, which also anchors this bar and decides when it is
    // visible. Reads are guarded because a binding here can run before the
    // window has assigned them.
    property var appWindow
    property var listView
    // The delegate holding the caret, from the toolbar. Its caret position
    // and in-block selection are two of the readouts.
    property var targetBlock
    property StatisticsPanel statisticsPanel

    // The two dialogs a click here opens are the window's, so the bar asks
    // for them rather than holding them.
    signal writingGoalRequested()
    signal createVaultRequested()

    height: 28
    color: Theme.footerBackground

    // Thousands separators, so a long document reads as 12,480 words rather
    // than 12480.
    function formatCount(value) {
        var n = Number(value)
        if (!isFinite(n))
            n = 0
        var sign = n < 0 ? "-" : ""
        var digits = Math.floor(Math.abs(n)).toString()
        return sign + digits.replace(/\B(?=(\d{3})+(?!\d))/g, ",")
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 16

        // Transient note: internal-link resolution
        // feedback etc. Takes no space when empty.
        Text {
            objectName: "transientStatusText"
            visible: statusBar.appWindow && statusBar.appWindow.transientStatus !== ""
            text: statusBar.appWindow ? statusBar.appWindow.transientStatus : ""
            font.pixelSize: 11
            color: Theme.accent
        }

        // Passive update notice: appears only when the opt-out daily
        // check found a newer release; click opens the release page in
        // the browser. Never a popup.
        Text {
            objectName: "updateNoticeText"
            visible: UpdateChecker.updateAvailable
            text: UpdateChecker.updateAvailable
                ? qsTr("Update available: v%1").arg(UpdateChecker.latestVersion)
                : ""
            font.pixelSize: 11
            font.underline: updateNoticeMouse.containsMouse
            color: Theme.accent

            MouseArea {
                id: updateNoticeMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Qt.openUrlExternally(UpdateChecker.releaseUrl)
            }
        }

        // Single-file mode's quiet upgrade path: a lone .md is open
        // with no vault, and one click away is turning
        // its folder into one. Deliberately a passive status-bar line,
        // never a popup or a first-run prompt.
        Text {
            objectName: "createVaultAffordance"
            visible: statusBar.appWindow && !statusBar.appWindow.collectionOpen
                     && DocumentManager.hasFile
            text: qsTr("Create vault from this folder…")
            font.pixelSize: 11
            font.underline: createVaultMouse.containsMouse
            color: Theme.textMuted

            MouseArea {
                id: createVaultMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: statusBar.createVaultRequested()
            }
        }

        // Save state indicator with dot
        Row {
            spacing: 6

            Rectangle {
                width: 8
                height: 8
                radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: DocumentManager && DocumentManager.isDirty ? Theme.warning : Theme.success
            }

            Text {
                objectName: "saveStateText"
                text: DocumentManager && DocumentManager.isDirty ? "Unsaved" : "Saved"
                font.pixelSize: 11
                color: Theme.textMuted
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // §9.7 last-saved time: relative, absolute on hover.
        Text {
            objectName: "savedTimeText"
            visible: text !== ""
            font.pixelSize: 11
            color: Theme.textFaint

            // Re-rendered every 30 s so "2 min ago" stays honest.
            property int clockTick: 0
            Timer {
                interval: 30000
                running: true
                repeat: true
                onTriggered: parent.clockTick++
            }

            text: {
                var tick = clockTick  // periodic re-evaluation
                if (!DocumentManager || DocumentManager.isDirty)
                    return ""
                var at = DocumentManager.lastSavedAt
                if (!at || isNaN(at.getTime()))
                    return ""
                var secs = (Date.now() - at.getTime()) / 1000
                if (secs < 60) return qsTr("just now")
                if (secs < 3600)
                    return Math.floor(secs / 60) + qsTr(" min ago")
                return Qt.formatTime(at, "hh:mm")
            }
            ToolTip.visible: savedTimeHover.hovered
            ToolTip.text: DocumentManager && DocumentManager.lastSavedAt
                ? Qt.formatDateTime(DocumentManager.lastSavedAt,
                                    "yyyy-MM-dd hh:mm:ss") : ""
            HoverHandler { id: savedTimeHover }
        }

        // Separator
        Rectangle {
            width: 1
            height: 12
            color: Theme.textDisabled
        }

        // Caret position (features.md §9.7): block-relative, the
        // honest coordinate in a block editor.
        Text {
            objectName: "cursorPositionText"
            visible: text !== ""
            font.pixelSize: 11
            color: Theme.textMuted
            text: {
                var target = statusBar.targetBlock
                if (!target || target.cursorLineColumn === undefined)
                    return ""
                var lc = target.cursorLineColumn
                return qsTr("Block ") + (target.index + 1)
                    + " · " + qsTr("Ln ") + lc.line
                    + ", " + qsTr("Col ") + lc.column
            }
        }

        // Separator
        Rectangle {
            width: 1
            height: 12
            color: Theme.textDisabled
        }

        // Block type indicator
        Text {
            id: blockTypeText
            objectName: "blockTypeText"

            // Bumped on any model change so the binding below
            // re-evaluates when blocks shift or change type; the
            // binding itself must never be assigned imperatively
            // (that would sever it from currentIndex changes).
            property int modelRevision: 0

            property int currentBlockType: {
                var revision = modelRevision  // dependency only
                var currentIndex = statusBar.listView
                    ? statusBar.listView.currentIndex : -1
                // Default to first block if no selection
                if (currentIndex < 0 && BlockModel && BlockModel.count > 0) {
                    currentIndex = 0
                }
                if (currentIndex < 0 || !BlockModel || currentIndex >= BlockModel.count) {
                    return 0
                }
                var block = BlockModel.blockAt(currentIndex)
                return block ? block.blockType : 0
            }

            text: {
                switch (currentBlockType) {
                    case 1: return "Heading 1"
                    case 2: return "Heading 2"
                    case 3: return "Heading 3"
                    case 4: return "Bulleted List"
                    case 5: return "Numbered List"
                    case 6: return "To-do"
                    case 7: return "Quote"
                    case 8: return "Code Block"
                    case 9: return "Divider"
                    case 10: return "Heading 4"
                    default: return "Paragraph"
                }
            }
            font.pixelSize: 11
            color: Theme.textMuted

            Connections {
                target: BlockModel
                function onDataChanged(topLeft, bottomRight, roles) {
                    blockTypeText.modelRevision++
                }
                function onCountChanged() {
                    blockTypeText.modelRevision++
                }
            }
        }

        // Separator
        Rectangle {
            width: 1
            height: 12
            color: Theme.textDisabled
        }

        // File path or "New Document"
        Text {
            objectName: "filePathText"
            text: DocumentManager && DocumentManager.hasFile ? DocumentManager.currentFilePath : "New Document"
            elide: Text.ElideMiddle
            Layout.fillWidth: true
            font.pixelSize: 11
            color: Theme.textMuted
        }

        // Separator
        Rectangle {
            width: 1
            height: 12
            color: Theme.textDisabled
        }

        // Block count
        Text {
            objectName: "blockCountText"
            text: statusBar.formatCount(BlockModel ? BlockModel.count : 0)
                  + " blocks"
            font.pixelSize: 11
            color: Theme.textMuted
        }

        // Separator
        Rectangle {
            width: 1
            height: 12
            color: Theme.textDisabled
        }

        // features.md §9.7 word/character counts over display text,
        // selection-aware: a block selection, cross-block text
        // selection, or in-block selection counts itself; otherwise
        // the whole document. The display-text rules come from the
        // collection (wordCountForMarkdown), so this bar and the
        // note list never disagree.
        Item {
            id: docCounter
            width: 0; height: 0

            // NOT a live binding: recomputing walks every block
            // through the display-text parser, so it runs behind a
            // coalescing timer — one pass 200 ms after the last
            // model/selection change, off the keystroke and load
            // paths (the §21.7 gates measure those).
            property var counts: ({ words: 0, chars: 0, sel: false })

            Timer {
                id: countTimer
                interval: 200
                onTriggered: docCounter.recompute()
            }
            Connections {
                target: BlockModel
                function onDataChanged() { countTimer.restart() }
                function onCountChanged() { countTimer.restart() }
                function onDocumentCountsChanged() { countTimer.restart() }
            }
            Connections {
                target: DocumentSelection
                function onRevisionChanged() { countTimer.restart() }
            }
            readonly property string inBlockSelDep:
                statusBar.targetBlock
                && statusBar.targetBlock.selectedDisplayText
                       !== undefined
                    ? statusBar.targetBlock.selectedDisplayText : ""
            onInBlockSelDepChanged: countTimer.restart()
            Component.onCompleted: recompute()

            // The whole-document word count, kept current regardless of
            // any selection — the writing-goal ring reads it.
            property int docWords: 0

            function recompute() {
                var perfOn = PerfLog && PerfLog.enabled
                if (perfOn)
                    PerfLog.begin("statusbar.count", {
                        "blocks": BlockModel ? BlockModel.count : 0
                    })
                try {
                docWords = BlockModel ? BlockModel.documentWordCount : 0

                var target = statusBar.targetBlock
                var inBlockSel =
                    (target && target.selectedDisplayText !== undefined)
                        ? target.selectedDisplayText : ""

                var words = 0
                var chars = 0

                if (DocumentSelection.hasBlockSelection) {
                    var indexes = DocumentSelection.selectedIndexes()
                    for (var i = 0; i < indexes.length; i++) {
                        words += BlockModel.wordCountAt(indexes[i])
                        chars += BlockModel.charCountAt(indexes[i], true)
                    }
                    counts = { words: words, chars: chars, sel: true }
                    return
                }

                if (DocumentSelection.hasTextSelection) {
                    var range = DocumentSelection.orderedTextRange()
                    for (var b = range.startIndex;
                         b <= range.endIndex; b++) {
                        var content = BlockModel.getContent(b)
                        var from = b === range.startIndex
                            ? range.startPos : 0
                        var to = b === range.endIndex
                            ? range.endPos : content.length
                        if (from === 0 && to === content.length) {
                            words += BlockModel.wordCountAt(b)
                            chars += BlockModel.charCountAt(b, true)
                        } else {
                            var frag = content.substring(from, to)
                            var vb = BlockModel.blockAt(b).blockType === 8
                            words += NoteCollection.wordCountForMarkdown(
                                frag, vb)
                            chars += NoteCollection.charCountForMarkdown(
                                frag, vb)
                        }
                    }
                    counts = { words: words, chars: chars, sel: true }
                    return
                }

                if (inBlockSel.length > 0) {
                    // Already display text: count verbatim.
                    counts = {
                        words: NoteCollection.wordCountForMarkdown(
                            inBlockSel, true),
                        chars: inBlockSel.length,
                        sel: true
                    }
                    return
                }

                if (!BlockModel) {
                    counts = { words: 0, chars: 0, sel: false }
                    return
                }
                counts = {
                    words: BlockModel.documentWordCount,
                    chars: BlockModel.documentCharCount,
                    sel: false
                }
                } finally {
                    if (perfOn)
                        PerfLog.end("statusbar.count", {
                            "words": counts.words,
                            "chars": counts.chars,
                            "selection": counts.sel
                        })
                }
            }
        }

        // Word count — clicking opens the §19.1 statistics popover.
        Text {
            id: wordCountText
            objectName: "wordCountText"
            text: statusBar.formatCount(docCounter.counts.words)
                  + (docCounter.counts.words === 1 ? qsTr(" word")
                                                   : qsTr(" words"))
                  + (docCounter.counts.sel ? qsTr(" selected") : "")
            font.pixelSize: 11
            color: docCounter.counts.sel ? Theme.accent : Theme.textMuted
            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    statusBar.statisticsPanel.parent = wordCountText
                    statusBar.statisticsPanel.open()
                    Qt.callLater(function() {
                        // Open upward and align the panel's right edge with
                        // the trigger, so it stays inside the window.
                        statusBar.statisticsPanel.x =
                            wordCountText.width - statusBar.statisticsPanel.width
                        statusBar.statisticsPanel.y = -statusBar.statisticsPanel.height - 8
                    })
                }
            }
        }

        // Separator
        Rectangle {
            width: 1
            height: 12
            color: Theme.textDisabled
        }

        // Character count
        Text {
            objectName: "charCountText"
            text: statusBar.formatCount(docCounter.counts.chars) + " chars"
            font.pixelSize: 11
            color: docCounter.counts.sel ? Theme.accent : Theme.textMuted
        }

        // §19.2 writing-goal ring: progress toward the per-note word
        // target. Only in collection mode (the goal is front-matter).
        // Clicking sets or clears the goal.
        Item {
            objectName: "goalRing"
            visible: statusBar.appWindow && statusBar.appWindow.collectionOpen
                     && statusBar.appWindow.currentNoteRelPath !== ""
            width: visible ? 60 : 0
            height: 18
            Layout.alignment: Qt.AlignVCenter

            property int goal: {
                var r = NoteCollection.revision  // dependency only
                var rel = statusBar.appWindow
                    ? statusBar.appWindow.currentNoteRelPath : ""
                return rel !== "" ? NoteCollection.goalFor(rel) : 0
            }
            property int words: docCounter.docWords
            property real fraction: goal > 0
                ? Math.min(1, words / goal) : 0

            Canvas {
                id: ringCanvas
                width: 16; height: 16
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                property real frac: parent.fraction
                property color trackColor: Theme.border
                property color fillColor: parent.fraction >= 1
                    ? Theme.success : Theme.accent
                onFracChanged: requestPaint()
                onTrackColorChanged: requestPaint()
                onFillColorChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var cx = width / 2, cy = height / 2, r = 6
                    ctx.lineWidth = 2.5
                    ctx.strokeStyle = trackColor
                    ctx.beginPath()
                    ctx.arc(cx, cy, r, 0, 2 * Math.PI)
                    ctx.stroke()
                    if (frac > 0) {
                        ctx.strokeStyle = fillColor
                        ctx.beginPath()
                        ctx.arc(cx, cy, r, -Math.PI / 2,
                                -Math.PI / 2 + frac * 2 * Math.PI)
                        ctx.stroke()
                    }
                }
            }
            Text {
                anchors.left: ringCanvas.right
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                text: parent.goal > 0
                    ? (Math.round(parent.fraction * 100) + "%")
                    : qsTr("goal")
                font.pixelSize: 11
                color: parent.fraction >= 1 && parent.goal > 0
                    ? Theme.success : Theme.textMuted
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                ToolTip.visible: containsMouse
                ToolTip.text: parent.goal > 0
                    ? (parent.words + " / " + parent.goal + qsTr(" words"))
                    : qsTr("Set a writing goal")
                hoverEnabled: true
                onClicked: statusBar.writingGoalRequested()
            }
        }
    }
}
