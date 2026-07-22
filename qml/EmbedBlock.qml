// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Window
import Kvit 1.0

// Embed preview card (features.md §1.2.14): an image
// expression ![](url) whose URL is a web page or video host, rendered as a
// card (thumbnail, title, description, source) built from OpenGraph metadata
// fetched off-thread and cached. Clicking opens the URL externally; a video
// host shows a play affordance; a failed fetch falls back to a card naming the
// URL. Storage is the image expression, so this round-trips byte-identically.
// Carries the block focus/selection/drag API like the other non-text blocks.
BlockDelegateBase {
    id: root

    // The editor window this row is in, typed. Null for any other window,
    // so the guards below still mean what they meant.
    readonly property KvitShell shell: Window.window as KvitShell


    required property int index
    required property string blockId
    required property int blockType
    required property string content
    required property int indentLevel
    required property bool checked
    required property int ordinal
    // Per-block presentation attributes (features.md §1.2.14):
    // configurable embed card width and height. Absent = the default
    // full-width card.
    required property string attributes

    property int blockIndex: index
    property bool isPooled: false
    property ListView listView: ListView.view
    property bool isFocused: focusTarget.activeFocus
    property bool isHovered: hoverArea.containsMouse

    // Configurable embed dimensions (§1.2.14): stored width/height in px.
    readonly property int embedWidth: BlockAttributes.num(attributes, "width", 0)
    readonly property int embedHeight: BlockAttributes.num(attributes, "height", 0)
    // Live card size while a resize drag is in flight; 0 when none is. The
    // card binds to these rather than being assigned during the drag, because
    // assigning to width, implicitHeight, or anchors.right destroys those
    // bindings for good — and this delegate is pooled, so the next block to
    // reuse it would keep the dragged geometry and ignore its own attributes.
    property int previewWidth: 0
    property int previewHeight: 0
    readonly property int effectiveWidth:
        previewWidth > 0 ? previewWidth : embedWidth
    readonly property int effectiveHeight:
        previewHeight > 0 ? previewHeight : embedHeight
    function setEmbedSize(payload) {
        BlockModel.setBlockAttributes(root.index, payload)
    }

    // The URL inside ![alt](url).
    readonly property string embedUrl: {
        var m = content.match(/!\[[^\]]*\]\(([^)]*)\)/)
        return m ? m[1].split(" ")[0] : ""
    }
    property var meta: ({})
    readonly property bool loaded: meta && meta.url !== undefined
    readonly property bool failed: loaded && meta.ok === false
    readonly property bool isVideo: loaded && meta.video === true

    // Whether this URL's origin may be contacted. Reading EgressPolicy.revision
    // is what makes the binding live: isAllowed() is a plain function call, so
    // without the revision dependency the card would never notice the reader
    // approving the origin.
    readonly property bool remoteAllowed: {
        var r = EgressPolicy.revision
        return EgressPolicy.isAllowed(root.embedUrl)
    }
    // Nothing cached and no permission to fetch: the inert state.
    readonly property bool awaitingConsent: !loaded && !remoteAllowed
    readonly property bool canOfferLoad: EgressPolicy.canRequestConsent(embedUrl)

    // Cached metadata is displayed; a fetch happens only once the origin is
    // approved. Opening a note must not contact the hosts the note names —
    // that would disclose the reader's address and reading time to whoever
    // wrote it, and aim the editor at whatever the URL points to.
    function refreshMeta() {
        if (embedUrl === "")
            return
        var cached = EmbedMetadata.cachedMetadata(embedUrl)
        if (cached && cached.url !== undefined) {
            meta = cached
            return
        }
        meta = ({})
        if (remoteAllowed)
            EmbedMetadata.requestMetadata(embedUrl)
    }
    // The reader asked for this card specifically: approve the origin, which
    // covers the page and the thumbnail and favicon it names, then fetch.
    function loadPreview() {
        if (embedUrl === "")
            return
        EgressPolicy.allowOrigin(embedUrl)
        EmbedMetadata.requestMetadata(embedUrl)
    }
    Component.onCompleted: refreshMeta()
    onEmbedUrlChanged: refreshMeta()
    onRemoteAllowedChanged: refreshMeta()
    Connections {
        target: EmbedMetadata
        function onMetadataReady(u) {
            if (u === root.embedUrl)
                root.meta = EmbedMetadata.cachedMetadata(u)
        }
    }

    function openEmbed() {
        // The guard SELECTS behaviour rather than checking for null: with an
        // editor shell the link routes through its opener; without one — a
        // preview hosted in some other window — it opens externally.
        // KvitShell.openLink answers whether it handled the link, so the
        // fallback survives without the delegate naming the opener object.
        if (!root.shell || !root.shell.openLink(embedUrl))
            Qt.openUrlExternally(embedUrl)
    }

    readonly property bool blockSelected: {
        var revision = DocumentSelection.revision
        return DocumentSelection.isBlockSelected(root.index)
            || DocumentSelection.portionForBlock(root.index).selected === true
    }
    function markdownPositionAt(sceneX, sceneY) { return 0 }
    function pointInText(sceneX, sceneY) { return false }
    function lineStepPosition(mdPos, dir) { return -1 }
    function entryPositionAtX(x, fromTop) { return 0 }
    function xAtMarkdown(mdPos) { return 0 }

    readonly property bool isDragSource: {
        if (!root.shell || !root.shell.blockDrag || !root.shell.blockDrag.active)
            return false
        return root.shell.blockDrag.isMulti ? root.blockSelected
                                     : root.shell.blockDrag.sourceIndex === root.index
    }
    function focusSelectionHandler() {
        AppActions.requestSelectionFocus()
    }
    onIsFocusedChanged: {
        if (isFocused) {
            if (root.shell && root.shell.lastFocusedBlock !== undefined)
                root.shell.lastFocusedBlock = index
        }
    }

    implicitHeight: card.implicitHeight + 12

    ListView.onPooled: {
        isPooled = true; focusTarget.focus = false; opacity = 0
        previewWidth = 0; previewHeight = 0
    }
    ListView.onReused: {
        isPooled = false; opacity = 1
        // A drag interrupted by scrolling must not follow the delegate to
        // whatever block reuses it.
        previewWidth = 0; previewHeight = 0
    }

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

    Item {
        id: focusTarget
        objectName: "embedFocusItem"
        anchors.fill: parent
        activeFocusOnTab: true
        Keys.onPressed: function(event) {
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && (event.modifiers & Qt.ControlModifier)
                && (event.modifiers & Qt.ShiftModifier)) {
                if (root.listView) root.listView.currentIndex = root.index
                DocumentSelection.selectBlock(root.index)
                root.focusSelectionHandler(); event.accepted = true; return
            }
            if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                DocumentSelection.selectAllBlocks(); root.focusSelectionHandler()
                event.accepted = true; return
            }
            if (event.key === Qt.Key_Up && root.index > 0 && root.listView) {
                var p = root.index - 1; root.listView.currentIndex = p
                var prev = (root.listView.itemAtIndex(p) as BlockDelegateBase); if (prev) prev.focusAtEnd()
                event.accepted = true; return
            }
            if (event.key === Qt.Key_Down && root.index < BlockModel.count - 1 && root.listView) {
                var n = root.index + 1; root.listView.currentIndex = n
                var next = (root.listView.itemAtIndex(n) as BlockDelegateBase); if (next) next.focusAtStart()
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

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: function(mouse) {
            if (mouse.modifiers & Qt.ControlModifier) {
                DocumentSelection.toggleBlock(root.index)
                if (DocumentSelection.hasBlockSelection) root.focusSelectionHandler()
                else focusTarget.forceActiveFocus()
                return
            }
            if (DocumentSelection.hasBlockSelection || DocumentSelection.hasTextSelection)
                DocumentSelection.clear()
            focusTarget.forceActiveFocus()
        }
    }

    // The card.
    // Maximum card width inside the block (past the gutter).
    readonly property int embedMaxWidth: Math.max(160, root.width - 56)

    Rectangle {
        id: card
        objectName: "embedCard"
        anchors.left: parent.left
        // A configured width drops the right anchor for an explicit size
        // (§1.2.14); the default card spans the full content width.
        anchors.right: root.effectiveWidth > 0 ? undefined : parent.right
        anchors.leftMargin: 48
        anchors.rightMargin: 8
        anchors.top: parent.top
        anchors.topMargin: 4
        width: root.effectiveWidth > 0
            ? Math.min(root.effectiveWidth, root.embedMaxWidth) : undefined
        radius: 8
        clip: true
        color: root.blockSelected ? Theme.blockSelectionTint
             : (root.isFocused ? Theme.focusTint : Theme.panelBackground)
        // A visible keyboard-focus ring (§14.1) in addition to the tint.
        border.color: root.blockSelected ? Theme.accent
                    : root.isFocused ? Theme.focusRing : Theme.border
        border.width: root.isFocused ? 2 : 1
        opacity: root.isDragSource ? 0.35 : 1
        implicitHeight: root.effectiveHeight > 0
            ? root.effectiveHeight : Math.max(84, cardRow.implicitHeight + 20)

        Row {
            id: cardRow
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12
            // Above the card-wide "open the link" MouseArea declared below,
            // so the Load button gets the click. The rest of the row is text
            // and images, which do not accept mouse events, so clicks there
            // still fall through to opening the link.
            z: 1

            // Thumbnail (or a placeholder tile).
            Rectangle {
                id: thumb
                width: 120
                height: 74
                radius: 4
                color: Theme.hoverTint
                clip: true
                Image {
                    objectName: "embedThumb"
                    anchors.fill: parent
                    // Routed through the image://remote provider, never bound
                    // to the URL directly: the thumbnail is chosen by the
                    // fetched page, so it is as untrusted as the page and has
                    // to travel over the checked transport like everything
                    // else. An unapproved origin yields no source at all.
                    source: EgressPolicy.imageSourceFor(root.loaded ? root.meta.image : "")
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    // The tile is 120x74. Without this the remote provider is
                    // told nothing about the size wanted and decodes whatever
                    // the page linked, which its own guard permits up to 32
                    // million pixels — around 128 MiB of pixmap for a card
                    // thumbnail.
                    sourceSize.width: thumb.width
                    sourceSize.height: thumb.height
                }
                // Play affordance for a video host.
                Rectangle {
                    visible: root.isVideo
                    anchors.centerIn: parent
                    width: 30; height: 30; radius: 15
                    color: "#cc000000"
                    Text { anchors.centerIn: parent; text: "▶"; color: "white"; font.pixelSize: 14 }
                }
                Text {
                    visible: !root.loaded || !root.meta.image
                    anchors.centerIn: parent
                    text: root.isVideo ? "▶" : "🔗"
                    color: Theme.textFaint
                    font.pixelSize: 22
                }
            }

            Column {
                width: parent.width - thumb.width - parent.spacing
                spacing: 3
                Text {
                    objectName: "embedTitle"
                    width: parent.width
                    text: root.loaded
                        ? (root.meta.title && root.meta.title.length > 0
                            ? root.meta.title : root.embedUrl)
                        : root.embedUrl
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    font.pixelSize: 14
                    font.bold: true
                    color: Theme.textPrimary
                }
                Text {
                    visible: root.loaded && root.meta.description
                        && root.meta.description.length > 0
                    width: parent.width
                    text: root.loaded ? root.meta.description : ""
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
                // The inert card's affordance. Until this is clicked the block
                // is a piece of text naming a URL, and nothing has been
                // requested from that URL's host.
                Row {
                    visible: root.awaitingConsent
                    spacing: 8
                    Rectangle {
                        objectName: "embedLoadButton"
                        width: loadLabel.implicitWidth + 16
                        height: loadLabel.implicitHeight + 8
                        radius: 4
                        visible: root.canOfferLoad
                        color: Theme.hoverTint
                        border.color: loadArea.containsMouse ? Theme.accent : Theme.border
                        Text {
                            id: loadLabel
                            anchors.centerIn: parent
                            text: qsTr("Load preview")
                            font.pixelSize: 11
                            color: loadArea.containsMouse ? Theme.textPrimary
                                                          : Theme.textMuted
                        }
                        MouseArea {
                            id: loadArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.loadPreview()
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.canOfferLoad
                            ? qsTr("· not loaded")
                            : qsTr("· %1").arg(EgressPolicy.refusalReason(root.embedUrl))
                        font.pixelSize: 11
                        color: Theme.textFaint
                    }
                }

                Row {
                    spacing: 6
                    Image {
                        visible: source != ""
                        source: EgressPolicy.imageSourceFor(root.loaded ? root.meta.favicon : "")
                        width: 14; height: 14
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        // A site is free to serve a 1024-pixel icon here.
                        sourceSize.width: 14
                        sourceSize.height: 14
                    }
                    Text {
                        text: {
                            var u = root.embedUrl
                            var m = u.match(/^https?:\/\/([^\/]+)/)
                            return m ? m[1] : u
                        }
                        font.pixelSize: 11
                        color: Theme.textFaint
                    }
                    Text {
                        visible: root.failed
                        text: qsTr("· preview unavailable")
                        font.pixelSize: 11
                        color: Theme.textFaint
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            acceptedButtons: Qt.LeftButton
            onClicked: root.openEmbed()
            // Let modifier-clicks fall through to the selection MouseArea below.
            propagateComposedEvents: true
        }

        // Resize handle (bottom-right): drag to set the card's width and height
        // as one undo step (§1.2.14). Visible on hover/focus.
        Rectangle {
            id: embedResize
            objectName: "embedResizeHandle"
            width: 14; height: 14; radius: 3
            color: Theme.accent
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 2
            opacity: (root.isHovered || root.isFocused) ? 0.9 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.SizeFDiagCursor
                preventStealing: true
                property real startW: 0
                property real startH: 0
                property real pressX: 0
                property real pressY: 0
                property int liveW: 0
                property int liveH: 0
                onPressed: function(mouse) {
                    startW = card.width
                    startH = card.height
                    var p = mapToItem(root, mouse.x, mouse.y)
                    pressX = p.x; pressY = p.y
                    liveW = Math.round(startW); liveH = Math.round(startH)
                    root.previewWidth = liveW; root.previewHeight = liveH
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    var p = mapToItem(root, mouse.x, mouse.y)
                    liveW = Math.max(160, Math.min(Math.round(startW + (p.x - pressX)),
                                                   root.embedMaxWidth))
                    liveH = Math.max(64, Math.round(startH + (p.y - pressY)))
                    root.previewWidth = liveW
                    root.previewHeight = liveH
                }
                onReleased: {
                    // Commit, then hand the card back to its bindings. The
                    // written attributes feed embedWidth/embedHeight, so the
                    // committed size is already in place when the preview
                    // clears.
                    var payload = BlockAttributes.withValue(
                        BlockAttributes.withValue(root.attributes, "width", String(liveW)),
                        "height", String(liveH))
                    root.setEmbedSize(payload)
                    root.previewWidth = 0
                    root.previewHeight = 0
                }
            }
        }
    }

    // Gutter plus + drag handle.
    Rectangle {
        objectName: "plusButton"
        width: 18; height: 18; x: 10; y: 10; radius: 4
        color: plusArea.containsMouse ? Theme.hoverTint : "transparent"
        opacity: root.isHovered ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Text { anchors.centerIn: parent; text: "+"; color: Theme.textMuted; font.pixelSize: 14; font.bold: true }
        MouseArea {
            id: plusArea; anchors.fill: parent; anchors.margins: -2
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: root.insertBlockBelowAndOpenMenu()
        }
    }
    Item {
        objectName: "embedHandle"
        width: 14; height: 18; x: 30; y: 10
        opacity: root.isHovered || embedHandleArea.pressed ? 0.6 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Column {
            anchors.centerIn: parent; spacing: 2
            Repeater { model: 2; Row { spacing: 2; Repeater { model: 2
                Rectangle { width: 3; height: 3; radius: 1.5; color: Theme.textFaint } } } }
        }
        MouseArea {
            id: embedHandleArea
            objectName: "dragHandle"
            anchors.fill: parent; anchors.margins: -2
            hoverEnabled: true; cursorShape: Qt.OpenHandCursor; preventStealing: true
            property real pressX: 0; property real pressY: 0; property bool dragging: false
            onPressed: function(mouse) { pressX = mouse.x; pressY = mouse.y; dragging = false }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                if (!root.shell || !root.shell.blockDrag) return
                var sp = embedHandleArea.mapToItem(null, mouse.x, mouse.y)
                if (!dragging) {
                    if (Math.abs(mouse.x - pressX) < 5 && Math.abs(mouse.y - pressY) < 5) return
                    dragging = true; root.shell.blockDrag.begin(root.index, sp.x, sp.y)
                } else root.shell.blockDrag.update(sp.x, sp.y)
            }
            onReleased: {
                if (dragging) { dragging = false; if (root.shell && root.shell.blockDrag) root.shell.blockDrag.drop(); return }
                if (root.listView) root.listView.currentIndex = root.index
                DocumentSelection.selectBlock(root.index); root.focusSelectionHandler()
            }
            onCanceled: {
                if (dragging) { dragging = false;                    if (root.shell && root.shell.blockDrag) root.shell.blockDrag.cancel() }
            }
        }
    }
}
