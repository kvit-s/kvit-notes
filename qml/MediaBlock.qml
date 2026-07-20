// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtMultimedia
import Kvit 1.0

// Local media block (features.md §1.2.14). The block shares the image
// markdown — ![alt|width](path) — but its path has an audio or video
// extension, so it parses as a Media block and hosts a QtMultimedia player:
// audio as a compact control bar, video as a sized frame, each with
// play/pause, a seek bar, elapsed/total time, and a volume control.
// A missing file or a codec the backend rejects shows the §1.2.14 fallback
// card naming the path and the reason, never a blank. It keeps the non-text
// focus API of the other wave-2 blocks so navigation, selection, and drag stay
// uniform.
Item {
    id: root

    required property int index
    required property string blockId
    required property int blockType
    required property string content
    required property int indentLevel
    required property bool checked
    required property int ordinal
    required property string language
    required property string calloutTitle

    property int blockIndex: index
    property bool isPooled: false
    property ListView listView: ListView.view
    property bool isFocused: focusTarget.activeFocus
    property bool isHovered: hoverArea.containsMouse

    readonly property var media: ImageAssets.parse(content)
    readonly property string noteDir: {
        var p = DocumentManager.currentFilePath
        var idx = p.lastIndexOf("/")
        return idx >= 0 ? p.substring(0, idx) : ""
    }
    readonly property string resolvedSource:
        ImageAssets.resolve(media.path, noteDir,
                            NoteCollection.isOpen ? NoteCollection.rootPath : "")
    // Remote media needs the reader's approval before the player is given a
    // URL, for the same reason images do: opening a note must not contact the
    // hosts it names.
    //
    // Consent is where the enforcement stops for media. Playback streams
    // through QtMultimedia's own network stack, so once an origin is approved
    // the address validation and byte caps EgressFetcher applies elsewhere do
    // not cover the media stream. Routing a seekable stream through the
    // fetcher would mean buffering whole files, so the gate is the approval;
    // devel.md records the gap.
    readonly property bool isRemote: /^https?:\/\//i.test(root.resolvedSource)
    readonly property string playbackSource: {
        var r = EgressPolicy.revision
        if (!root.isRemote)
            return root.resolvedSource
        return EgressPolicy.isAllowed(root.resolvedSource) ? root.resolvedSource : ""
    }
    readonly property bool awaitingConsent:
        root.resolvedSource !== "" && root.playbackSource === ""

    readonly property string extension: {
        var p = media.path
        var dot = p.lastIndexOf(".")
        return dot >= 0 ? p.substring(dot + 1).toLowerCase() : ""
    }
    readonly property bool isAudio:
        ["mp3", "wav", "ogg", "flac", "m4a"].indexOf(extension) !== -1
    readonly property bool isVideo:
        ["mp4", "webm", "mkv", "mov"].indexOf(extension) !== -1
    // The player has nothing to play: the file is missing, the backend
    // rejects the codec, or the media is remote and not yet approved. All
    // three show the fallback card rather than a dead control bar; the card
    // itself distinguishes the consent case.
    readonly property bool hasError:
        resolvedSource === "" || awaitingConsent
        || player.error !== MediaPlayer.NoError
    readonly property int maxWidth: Math.max(120, root.width - 96)
    readonly property int videoWidth:
        Math.min(media.width > 0 ? media.width : 480, maxWidth)
    // Exposed for tests (which do not import QtMultimedia) and for chrome.
    readonly property bool isPlaying:
        player.playbackState === MediaPlayer.PlayingState

    function fmtTime(ms) {
        if (ms <= 0) return "0:00"
        var total = Math.floor(ms / 1000)
        var m = Math.floor(total / 60)
        var s = total % 60
        return m + ":" + (s < 10 ? "0" + s : s)
    }

    readonly property bool blockSelected: {
        var revision = documentSelection.revision // dependency only
        return documentSelection.isBlockSelected(root.index)
            || documentSelection.portionForBlock(root.index).selected === true
    }

    function markdownPositionAt(sceneX, sceneY) { return 0 }
    function pointInText(sceneX, sceneY) { return false }
    function lineStepPosition(mdPos, dir) { return -1 }
    function entryPositionAtX(x, fromTop) { return 0 }
    function xAtMarkdown(mdPos) { return 0 }

    readonly property bool isDragSource: {
        var win = Window.window
        if (!win || !win.blockDrag || !win.blockDrag.active) return false
        return win.blockDrag.isMulti ? root.blockSelected
                                     : win.blockDrag.sourceIndex === root.index
    }
    function focusSelectionHandler() {
        var win = Window.window
        if (win && win.selectionKeyHandler) win.selectionKeyHandler.forceActiveFocus()
    }
    onIsFocusedChanged: {
        if (isFocused) {
            var win = Window.window
            if (win && win.lastFocusedBlock !== undefined) win.lastFocusedBlock = index
        }
    }

    implicitHeight: card.height + 16

    ListView.onPooled: { isPooled = true; opacity = 0; player.pause() }
    ListView.onReused: { isPooled = false; opacity = 1 }

    function focusAtStart() { focusTarget.forceActiveFocus() }
    function focusAtEnd() { focusTarget.forceActiveFocus() }
    function focusAtPosition(markdownPos) { focusTarget.forceActiveFocus() }
    function isCursorOnFirstLine() { return true }
    function isCursorOnLastLine() { return true }

    function deleteCurrentBlock() {
        var prevIndex = root.index - 1
        blockModel.removeBlock(root.index)
        Qt.callLater(function() {
            if (listView && prevIndex >= 0) {
                listView.currentIndex = prevIndex
                var item = listView.itemAtIndex(prevIndex)
                if (item) item.focusAtEnd()
            }
        })
    }
    function createBlockBelow() {
        var newIndex = root.index + 1
        blockModel.insertBlock(newIndex, 0, "")
        Qt.callLater(function() {
            if (listView) {
                listView.currentIndex = newIndex
                var item = listView.itemAtIndex(newIndex)
                if (item) item.focusAtStart()
            }
        })
    }
    function insertBlockBelowAndOpenMenu() {
        var newIndex = root.index + 1
        blockModel.insertBlock(newIndex, 0, "")
        var lv = listView
        Qt.callLater(function() {
            if (!lv) return
            lv.currentIndex = newIndex
            var item = lv.itemAtIndex(newIndex)
            if (item) { item.focusAtStart(); if (item.openBlockMenu) item.openBlockMenu("insert") }
        })
    }

    MediaPlayer {
        id: player
        objectName: "mediaPlayer"
        source: root.playbackSource
        audioOutput: AudioOutput { id: audioOut; volume: 0.8 }
        videoOutput: root.isVideo ? videoFrame : null
    }

    Item {
        id: focusTarget
        objectName: "mediaFocusItem"
        anchors.fill: parent
        activeFocusOnTab: true
        Keys.onPressed: function(event) {
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)) {
                if (root.listView) root.listView.currentIndex = root.index
                documentSelection.selectBlock(root.index)
                root.focusSelectionHandler(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Space) {
                root.togglePlay(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Up && root.index > 0 && root.listView) {
                var pi = root.index - 1; root.listView.currentIndex = pi
                var prev = root.listView.itemAtIndex(pi); if (prev) prev.focusAtEnd()
                event.accepted = true; return
            }
            if (event.key === Qt.Key_Down && root.index < blockModel.count - 1 && root.listView) {
                var ni = root.index + 1; root.listView.currentIndex = ni
                var next = root.listView.itemAtIndex(ni); if (next) next.focusAtStart()
                event.accepted = true; return
            }
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                root.deleteCurrentBlock(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.createBlockBelow(); event.accepted = true; return
            }
        }
    }

    function togglePlay() {
        if (player.playbackState === MediaPlayer.PlayingState) player.pause()
        else player.play()
    }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 8
        radius: 4
        opacity: root.isDragSource ? 0.35 : 1
        color: root.blockSelected ? theme.blockSelectionTint
             : (root.isHovered ? theme.blockHoverTint : "transparent")
        border.color: root.blockSelected ? theme.accent : "transparent"
        border.width: root.blockSelected ? 1 : 0
    }

    // The media card: video frame or audio bar, or the fallback.
    Rectangle {
        id: card
        x: 34; y: 8
        width: root.isVideo && !root.hasError ? root.videoWidth : Math.min(420, root.maxWidth)
        height: contentCol.implicitHeight + 16
        radius: 6
        color: theme.panelBackground
        border.color: theme.border; border.width: 1
        opacity: root.isDragSource ? 0.35 : 1

        Column {
            id: contentCol
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 6

            // ---- Fallback card (missing file / rejected codec) ----
            Column {
                width: parent.width
                spacing: 3
                visible: root.hasError
                Text {
                    text: (root.isAudio ? "♪  " : "▷  ") + qsTr("Media unavailable")
                    color: theme.textPrimary; font.bold: true; font.pixelSize: 13
                }
                Text {
                    width: parent.width
                    text: root.media.path
                    color: theme.textMuted; font.pixelSize: 11; elide: Text.ElideMiddle
                }
                Text {
                    visible: !root.awaitingConsent
                    text: root.resolvedSource === ""
                          ? qsTr("File not found")
                          : qsTr("Cannot play this file: ") + player.errorString
                    color: theme.danger; font.pixelSize: 11
                    width: parent.width; wrapMode: Text.Wrap
                }
                Row {
                    visible: root.awaitingConsent
                    spacing: 8
                    Rectangle {
                        objectName: "mediaLoadButton"
                        width: mediaLoadLabel.implicitWidth + 16
                        height: mediaLoadLabel.implicitHeight + 8
                        radius: 4
                        visible: EgressPolicy.canRequestConsent(root.resolvedSource)
                        color: theme.hoverTint
                        border.color: mediaLoadArea.containsMouse ? theme.accent
                                                                  : theme.border
                        Text {
                            id: mediaLoadLabel
                            anchors.centerIn: parent
                            text: qsTr("Load media")
                            font.pixelSize: 11
                            color: mediaLoadArea.containsMouse ? theme.textPrimary
                                                               : theme.textMuted
                        }
                        MouseArea {
                            id: mediaLoadArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: EgressPolicy.allowOrigin(root.resolvedSource)
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Remote media not loaded")
                        color: theme.textMuted; font.pixelSize: 11
                    }
                }
            }

            // ---- Video frame ----
            VideoOutput {
                id: videoFrame
                visible: root.isVideo && !root.hasError
                width: parent.width
                height: visible ? width * 9 / 16 : 0
                fillMode: VideoOutput.PreserveAspectFit
            }

            // ---- Audio label ----
            Row {
                width: parent.width
                spacing: 8
                visible: root.isAudio && !root.hasError
                Text {
                    text: "♪"; font.pixelSize: 18; color: theme.textMuted
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 30
                    text: root.media.alt !== "" ? root.media.alt : root.media.path
                    color: theme.textPrimary; font.pixelSize: 12; elide: Text.ElideMiddle
                }
            }

            // ---- Transport controls (audio + video) ----
            Row {
                width: parent.width
                spacing: 8
                visible: !root.hasError

                // Play / pause.
                Rectangle {
                    objectName: "mediaPlayButton"
                    width: 30; height: 30; radius: 15
                    anchors.verticalCenter: parent.verticalCenter
                    color: playHover.containsMouse ? theme.accent : theme.chipBackground
                    // Play triangle (▶ renders reliably); pause is drawn as two
                    // bars, since the ⏸ glyph is missing from the base font.
                    Text {
                        anchors.centerIn: parent
                        visible: !root.isPlaying
                        text: "▶"
                        color: playHover.containsMouse ? theme.onAccent : theme.textPrimary
                        font.pixelSize: 13
                    }
                    Row {
                        anchors.centerIn: parent
                        visible: root.isPlaying
                        spacing: 3
                        Repeater { model: 2
                            Rectangle { width: 3; height: 12; radius: 1
                                color: playHover.containsMouse ? theme.onAccent : theme.textPrimary } }
                    }
                    MouseArea {
                        id: playHover; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.togglePlay()
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.fmtTime(player.position)
                    color: theme.textMuted; font.pixelSize: 11
                    width: 34; horizontalAlignment: Text.AlignRight
                }

                // Seek bar.
                Slider {
                    id: seek
                    objectName: "mediaSeek"
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 30 - 34 - 34 - 24 - 60 - 5 * 8
                    from: 0; to: Math.max(1, player.duration)
                    value: player.position
                    onMoved: player.position = value
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.fmtTime(player.duration)
                    color: theme.textMuted; font.pixelSize: 11
                    width: 34
                }

                // Volume (a drawn speaker — the 🔊 glyph is missing from the
                // base font). ◀ is the cone; the arcs are the sound, dropped
                // when muted.
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: audioOut.volume <= 0 ? "◀" : "◀))"
                    color: theme.textMuted
                    font.pixelSize: 12
                }
                Slider {
                    objectName: "mediaVolume"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 60
                    from: 0; to: 1; value: audioOut.volume
                    onMoved: audioOut.volume = value
                }
            }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    // Gutter plus-button.
    Rectangle {
        objectName: "plusButton"
        width: 18; height: 18; x: 10; y: 8; radius: 4
        color: plusArea.containsMouse ? theme.hoverTint : "transparent"
        opacity: root.isHovered ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Text { anchors.centerIn: parent; text: "+"; color: theme.textMuted; font.pixelSize: 14; font.bold: true }
        MouseArea { id: plusArea; anchors.fill: parent; anchors.margins: -2
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: root.insertBlockBelowAndOpenMenu() }
    }
    // Drag handle.
    Item {
        objectName: "mediaHandle"
        width: 14; height: 18; x: 30; y: 8
        opacity: root.isHovered || mediaHandle.pressed ? 0.6 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Column { anchors.centerIn: parent; spacing: 2
            Repeater { model: 2; Row { spacing: 2; Repeater { model: 2
                Rectangle { width: 3; height: 3; radius: 1.5; color: theme.textFaint } } } } }
        MouseArea {
            id: mediaHandle
            objectName: "dragHandle"
            anchors.fill: parent; anchors.margins: -2
            hoverEnabled: true; cursorShape: Qt.OpenHandCursor; preventStealing: true
            property real pressX: 0; property real pressY: 0; property bool dragging: false
            onPressed: function(mouse) { pressX = mouse.x; pressY = mouse.y; dragging = false }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                var win = Window.window
                if (!win || !win.blockDrag) return
                var sp = mediaHandle.mapToItem(null, mouse.x, mouse.y)
                if (!dragging) {
                    if (Math.abs(mouse.x - pressX) < 5 && Math.abs(mouse.y - pressY) < 5) return
                    dragging = true; win.blockDrag.begin(root.index, sp.x, sp.y)
                } else { win.blockDrag.update(sp.x, sp.y) }
            }
            onReleased: {
                var win = Window.window
                if (dragging) { dragging = false; if (win && win.blockDrag) win.blockDrag.drop(); return }
                if (root.listView) root.listView.currentIndex = root.index
                documentSelection.selectBlock(root.index)
                root.focusSelectionHandler()
            }
            onCanceled: { if (dragging) { dragging = false; var win = Window.window
                if (win && win.blockDrag) win.blockDrag.cancel() } }
        }
    }
}
