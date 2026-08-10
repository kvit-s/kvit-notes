// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
pragma ComponentBehavior: Bound

import QtQuick
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
BlockDelegateBase {
    id: root

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
    // The gutter's MouseAreas sit over hoverArea and steal its hover; fold
    // the gutter's own hover back in so the buttons do not vanish the moment
    // the pointer reaches them (as EditableBlock does).
    property bool isHovered: hoverArea.containsMouse || blockHandle.hovered

    readonly property var media: ImageAssets.parse(content)
    readonly property string noteDir: {
        var p = DocumentManager.currentFilePath
        var idx = p.lastIndexOf("/")
        return idx >= 0 ? p.substring(0, idx) : ""
    }
    readonly property string resolvedSource:
        ImageAssets.resolve(media.path, noteDir,
                            NoteCollection.isOpen ? NoteCollection.rootPath : "")
    // QtMultimedia never receives an http(s) URL. Approved media is fetched
    // through the guarded transport and played from a bounded temporary file.
    readonly property bool isRemote: /^https?:\/\//i.test(root.resolvedSource)
    readonly property string playbackSource: {
        var policyRevision = EgressPolicy.revision
        var cacheRevision = RemoteMediaCache.revision
        if (!root.isRemote)
            return root.resolvedSource
        if (!EgressPolicy.isAllowed(root.resolvedSource))
            return ""
        return RemoteMediaCache.sourceFor(root.resolvedSource)
    }
    readonly property bool awaitingConsent:
        root.isRemote && !EgressPolicy.isAllowed(root.resolvedSource)
    readonly property bool downloadFailed:
        root.isRemote && RemoteMediaCache.failedFor(root.resolvedSource)
    readonly property bool awaitingDownload:
        root.isRemote && !root.awaitingConsent && !root.downloadFailed
        && root.playbackSource === ""

    function requestRemoteMedia() {
        if (root.isRemote && EgressPolicy.isAllowed(root.resolvedSource))
            RemoteMediaCache.request(root.resolvedSource)
    }
    Component.onCompleted: requestRemoteMedia()
    onResolvedSourceChanged: requestRemoteMedia()
    Connections {
        target: EgressPolicy
        function onRevisionChanged() { root.requestRemoteMedia() }
    }

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
        sharedCard.hasError
    readonly property int maxWidth: Math.max(120, root.width - 96)
    readonly property int videoWidth:
        Math.min(media.width > 0 ? media.width : 480, maxWidth)
    // Exposed for tests (which do not import QtMultimedia) and for chrome.
    readonly property bool isPlaying:
        sharedCard.isPlaying

    readonly property bool blockSelected: {
        var revision = DocumentSelection.revision // dependency only
        return DocumentSelection.isBlockSelected(root.index)
            || DocumentSelection.portionForBlock(root.index).selected === true
    }

    function markdownPositionAt(sceneX, sceneY) { return 0 }
    function pointInText(sceneX, sceneY) { return false }
    function lineStepPosition(mdPos, dir) { return -1 }
    function entryPositionAtX(x, fromTop) { return 0 }
    function xAtMarkdown(mdPos) { return 0 }

    readonly property bool isDragSource: {
        if (!root.shell || !root.shell.blockDrag || !root.shell.blockDrag.active) return false
        return root.shell.blockDrag.isMulti ? root.blockSelected
                                     : root.shell.blockDrag.sourceIndex === root.index
    }
    // The editor window this row is in, typed. Null for any other window,
    // so the guards below still mean what they meant.
    readonly property KvitShell shell: Window.window as KvitShell

    function focusSelectionHandler() {
        AppActions.requestSelectionFocus()
    }
    onIsFocusedChanged: {
        if (isFocused) {
            if (root.shell && root.shell.lastFocusedBlock !== undefined) root.shell.lastFocusedBlock = index
        }
    }

    blockContentHeight: sharedCard.height + 16

    ListView.onPooled: { isPooled = true; opacity = 0; sharedCard.stop() }
    ListView.onReused: { isPooled = false; opacity = 1 }

    function focusAtStart() { focusTarget.forceActiveFocus() }
    function focusAtEnd() { focusTarget.forceActiveFocus() }
    function focusAtPosition(markdownPos) { focusTarget.forceActiveFocus() }
    function isCursorOnFirstLine() { return true }
    function isCursorOnLastLine() { return true }

    function deleteCurrentBlock() {
        var prevIndex = root.index - 1
        BlockModel.removeBlock(root.index)
        Qt.callLater(function() {
            if (listView && prevIndex >= 0) {
                listView.currentIndex = prevIndex
                var item = (listView.itemAtIndex(prevIndex) as BlockDelegateBase)
                if (item) item.focusAtEnd()
            }
        })
    }
    function createBlockBelow() {
        var newIndex = root.index + 1
        BlockModel.insertBlock(newIndex, 0, "")
        Qt.callLater(function() {
            if (listView) {
                listView.currentIndex = newIndex
                var item = (listView.itemAtIndex(newIndex) as BlockDelegateBase)
                if (item) item.focusAtStart()
            }
        })
    }
    function insertBlockBelowAndOpenMenu() {
        var newIndex = root.index + 1
        BlockModel.insertBlock(newIndex, 0, "")
        var lv = listView
        Qt.callLater(function() {
            if (!lv) return
            lv.currentIndex = newIndex
            var item = (lv.itemAtIndex(newIndex) as BlockDelegateBase)
            if (item) { item.focusAtStart(); if (item.openBlockMenu) item.openBlockMenu("insert") }
        })
    }

    MediaPlayerCard {
        id: sharedCard
        objectName: "mediaBlockCard"
        x: 52
        y: 8
        width: root.isVideo && !root.hasError ? root.videoWidth
              : Math.min(Interface.px(520), root.maxWidth)
        source: root.isPooled ? "" : root.playbackSource
        displayName: root.media.path
        title: root.media.alt !== "" ? root.media.alt : root.media.path
        audioHint: root.isAudio
        videoHint: root.isVideo
        forcedErrorMessage: root.awaitingConsent
            ? qsTr("Remote media not loaded")
            : root.awaitingDownload ? qsTr("Loading remote media…")
            : root.downloadFailed ? qsTr("Remote media download failed")
            : root.resolvedSource === "" ? qsTr("File not found") : ""
        showDesktopAction: !root.isRemote && root.resolvedSource !== ""
        desktopUrl: !root.isRemote ? root.resolvedSource : ""
        fallbackActionText: root.awaitingConsent
             && EgressPolicy.canRequestConsent(root.resolvedSource)
            ? qsTr("Load media") : ""
        onFallbackActionRequested: {
            EgressPolicy.allowOrigin(root.resolvedSource)
            root.requestRemoteMedia()
        }
        opacity: root.isDragSource ? 0.35 : 1
    }

    Item {
        id: focusTarget
        objectName: "mediaFocusItem"
        anchors.fill: parent
        activeFocusOnTab: true
        Keys.onPressed: function(event) {
            if (root.handleContextMenuKey(event))
                return
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)) {
                if (root.listView) root.listView.currentIndex = root.index
                DocumentSelection.selectBlock(root.index)
                root.focusSelectionHandler(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Space) {
                root.togglePlay(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Up && root.index > 0 && root.listView) {
                var pi = root.index - 1; root.listView.currentIndex = pi
                var prev = (root.listView.itemAtIndex(pi) as BlockDelegateBase); if (prev) prev.focusAtEnd()
                event.accepted = true; return
            }
            if (event.key === Qt.Key_Down && root.index < BlockModel.count - 1 && root.listView) {
                var ni = root.index + 1; root.listView.currentIndex = ni
                var next = (root.listView.itemAtIndex(ni) as BlockDelegateBase); if (next) next.focusAtStart()
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
        sharedCard.togglePlay()
    }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 44
        anchors.rightMargin: 8
        radius: 4
        opacity: root.isDragSource ? 0.35 : 1
        color: root.blockSelected ? Theme.blockSelectionTint
             : (root.isHovered ? Theme.blockHoverTint : "transparent")
        border.color: root.blockSelected ? Theme.accent : "transparent"
        border.width: root.blockSelected ? 1 : 0
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    // The gutter: plus / delete / drag handle, shared with every other
    // block delegate so the strip does not shift as the pointer moves down
    // a document. The reorder itself goes to the window's coordinator.
    BlockGutter {
        id: blockHandle
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 4

        rowHovered: root.isHovered
        dragEnabled: root.shell !== null && root.shell.blockDrag !== null

        onInsertRequested: root.insertBlockBelowAndOpenMenu()
        onDeleteRequested: root.deleteCurrentBlock()
        onHandleMenuRequested: AppActions.requestBlockHandleMenu(root)
        onBlockSelectRequested: {
            if (root.listView)
                root.listView.currentIndex = root.index
            DocumentSelection.selectBlock(root.index)
            root.focusSelectionHandler()
        }
        onDragStarted: function(sceneX, sceneY) {
            root.shell.blockDrag.begin(root.index, sceneX, sceneY)
        }
        onDragMoved: function(sceneX, sceneY) {
            root.shell.blockDrag.update(sceneX, sceneY)
        }
        onDragDropped: {
            if (root.shell && root.shell.blockDrag)
                root.shell.blockDrag.drop()
        }
        onDragCanceled: {
            if (root.shell && root.shell.blockDrag)
                root.shell.blockDrag.cancel()
        }
    }
}
