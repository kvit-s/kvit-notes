// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Kvit 1.0

// Image block (features.md §1.2.8). The block's content is its markdown
// expression — ![alt|width](path "caption") — parsed by ImageAssets; the
// delegate renders the image with a caption below and an alt tooltip,
// resizes by dragging a corner handle (writing width, one undo step), opens
// a lightbox on click, and shows a broken-path placeholder when the source
// does not resolve. It keeps the non-text focus API of DividerDelegate so
// block navigation, selection, and drag stay uniform.
BlockDelegateBase {
    id: delegate

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
    required property string language
    // Per-block presentation attributes: alignment and the image effects
    // (rounded/shadow/border/aspect).
    required property string attributes

    // Image alignment (features.md §9.2). Unlike text, an image defaults to
    // centered, so an unstyled image is unchanged; only an explicit
    // align=left|right moves it.
    readonly property string imageAlign:
        BlockAttributes.str(attributes, "align", "center")
    function setBlockAlignment(value) {
        var next = (value === "center" || value === "")
            ? BlockAttributes.without(attributes, "align")
            : BlockAttributes.withValue(attributes, "align", value)
        BlockModel.setBlockAttributes(delegate.index, next)
    }

    // ---- Image effects (features.md §1.2.8) ----
    // rounded (radius, default 12), shadow, border (optional custom color), and
    // the maintain-aspect toggle (`aspect=stretch` drops proportion locking).
    readonly property bool imgRounded: BlockAttributes.has(attributes, "rounded")
    readonly property int imgRadius: {
        var v = BlockAttributes.num(attributes, "rounded", 0)
        return v > 0 ? v : 12
    }
    readonly property bool imgShadow: BlockAttributes.has(attributes, "shadow")
    readonly property bool imgBorder: BlockAttributes.has(attributes, "border")
    readonly property color imgBorderColor: {
        var c = BlockAttributes.str(attributes, "border", "")
        return c !== "" ? c : Theme.border
    }
    readonly property bool imgStretch:
        BlockAttributes.str(attributes, "aspect", "") === "stretch"
    // Set new image-effect attributes as one undo step (used by the popover).
    function setImageEffects(payload) {
        BlockModel.setBlockAttributes(delegate.index, payload)
    }

    property int blockIndex: index
    property bool isPooled: false
    property ListView listView: ListView.view
    property bool isFocused: focusTarget.activeFocus
    property bool isHovered: hoverArea.containsMouse

    // ---- Parsed image + resolved source ----
    readonly property var img: ImageAssets.parse(content)
    readonly property string noteDir: {
        var p = DocumentManager.currentFilePath
        var idx = p.lastIndexOf("/")
        return idx >= 0 ? p.substring(0, idx) : ""
    }
    readonly property string resolvedSource:
        ImageAssets.resolve(img.path,
                            noteDir,
                            NoteCollection.isOpen ? NoteCollection.rootPath : "")
    // What the Image actually loads. A local file passes through; an http(s)
    // image is routed to the image://remote provider once the reader has
    // approved its origin, and is "" until then. A note is untrusted input,
    // so an image URL in one must not become a request just by being opened —
    // that would disclose the reader's address and reading time to whoever
    // wrote the note. Reading EgressPolicy.revision keeps this live across an
    // approval.
    readonly property string displaySource: {
        var r = EgressPolicy.revision
        return EgressPolicy.imageSourceFor(delegate.resolvedSource)
    }
    // A remote image the reader has not approved yet: the placeholder offers
    // to load it instead of showing a broken-image tile.
    readonly property bool awaitingConsent:
        delegate.resolvedSource !== "" && delegate.displaySource === ""

    readonly property int maxWidth: Math.max(80, delegate.width - 96)
    // Displayed width: the stored width (capped), else the natural width
    // (capped). Height follows the aspect ratio (PreserveAspectFit).
    readonly property int displayWidth: {
        var w = img.width > 0 ? img.width
              : (image.implicitWidth > 0 ? image.implicitWidth : 320)
        return Math.min(w, maxWidth)
    }
    // Live width while a resize drag is in flight; 0 when none is. The frame
    // binds to this instead of being assigned during the drag: assigning to
    // width would destroy its binding to displayWidth permanently, and this
    // delegate is pooled, so the next block to reuse it would inherit the
    // stale width and stop tracking its own model row.
    property int previewWidth: 0
    readonly property int effectiveWidth:
        previewWidth > 0 ? previewWidth : displayWidth

    readonly property bool blockSelected: {
        var revision = DocumentSelection.revision // dependency only
        return DocumentSelection.isBlockSelected(delegate.index)
            || DocumentSelection.portionForBlock(delegate.index).selected === true
    }

    // Cross-block position helpers (single position, like a divider).
    function markdownPositionAt(sceneX, sceneY) { return 0 }
    function pointInText(sceneX, sceneY) { return false }
    function lineStepPosition(mdPos, dir) { return -1 }
    function entryPositionAtX(x, fromTop) { return 0 }
    function xAtMarkdown(mdPos) { return 0 }

    readonly property bool isDragSource: {
        if (!delegate.shell || !delegate.shell.blockDrag || !delegate.shell.blockDrag.active)
            return false
        return delegate.shell.blockDrag.isMulti ? delegate.blockSelected
                                     : delegate.shell.blockDrag.sourceIndex === delegate.index
    }

    function focusSelectionHandler() {
        AppActions.requestSelectionFocus()
    }

    onIsFocusedChanged: {
        if (isFocused) {
            if (delegate.shell && delegate.shell.lastFocusedBlock !== undefined)
                delegate.shell.lastFocusedBlock = index
        }
    }

    implicitHeight: contentColumn.implicitHeight + 16

    ListView.onPooled: {
        captionField.commitPendingCaption()
        isPooled = true
        focusTarget.focus = false
        opacity = 0
        previewWidth = 0
    }
    ListView.onReused: {
        isPooled = false
        opacity = 1
        captionField.text = Qt.binding(function() { return delegate.img.caption })
        // A drag interrupted by scrolling must not follow the delegate to
        // whatever block reuses it.
        previewWidth = 0
    }

    // Focus API parity with EditableBlock; an image has no cursor.
    function focusAtStart() { focusTarget.forceActiveFocus() }
    function focusAtEnd() { focusTarget.forceActiveFocus() }
    function focusAtPosition(markdownPos) { focusTarget.forceActiveFocus() }
    function isCursorOnFirstLine() { return true }
    function isCursorOnLastLine() { return true }

    // Rewrite the block's markdown through the model as one undo step.
    function writeImage(path, alt, caption, width) {
        BlockModel.updateContent(delegate.index,
                                 ImageAssets.build(path, alt, caption, width))
    }

    function deleteCurrentBlock() {
        var prevIndex = delegate.index - 1
        BlockModel.removeBlock(delegate.index)
        Qt.callLater(function() {
            if (listView && prevIndex >= 0) {
                listView.currentIndex = prevIndex
                var item = (listView.itemAtIndex(prevIndex) as BlockDelegateBase)
                if (item) item.focusAtEnd()
            }
        })
    }
    function createBlockBelow() {
        var newIndex = delegate.index + 1
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
        var newIndex = delegate.index + 1
        BlockModel.insertBlock(newIndex, 0, "")
        var lv = listView
        Qt.callLater(function() {
            if (!lv) return
            lv.currentIndex = newIndex
            var item = (lv.itemAtIndex(newIndex) as BlockDelegateBase)
            if (item) {
                item.focusAtStart()
                if (item.openBlockMenu) item.openBlockMenu("insert")
            }
        })
    }

    Item {
        id: focusTarget
        objectName: "imageFocusItem"
        anchors.fill: parent
        activeFocusOnTab: true

        Keys.onPressed: function(event) {
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && (event.modifiers & Qt.ControlModifier)
                && (event.modifiers & Qt.ShiftModifier)) {
                if (delegate.listView)
                    delegate.listView.currentIndex = delegate.index
                DocumentSelection.selectBlock(delegate.index)
                delegate.focusSelectionHandler()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                DocumentSelection.selectAllBlocks()
                delegate.focusSelectionHandler()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_D && (event.modifiers & Qt.ControlModifier)
                && (event.modifiers & Qt.ShiftModifier)) {
                delegate.deleteCurrentBlock()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_D && (event.modifiers & Qt.ControlModifier)) {
                BlockModel.duplicateBlocks([delegate.index])
                var lv = delegate.listView
                var cloneIndex = delegate.index + 1
                Qt.callLater(function() {
                    if (!lv) return
                    lv.currentIndex = cloneIndex
                    var item = (lv.itemAtIndex(cloneIndex) as BlockDelegateBase)
                    if (item) item.focusAtStart()
                })
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Up && delegate.index > 0 && delegate.listView) {
                var prevIndex = delegate.index - 1
                delegate.listView.currentIndex = prevIndex
                var prev = (delegate.listView.itemAtIndex(prevIndex) as BlockDelegateBase)
                if (prev) prev.focusAtEnd()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Down && delegate.index < BlockModel.count - 1
                && delegate.listView) {
                var nextIndex = delegate.index + 1
                delegate.listView.currentIndex = nextIndex
                var next = (delegate.listView.itemAtIndex(nextIndex) as BlockDelegateBase)
                if (next) next.focusAtStart()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                delegate.deleteCurrentBlock()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                delegate.createBlockBelow()
                event.accepted = true
                return
            }
        }
    }

    // Selection / focus / hover background.
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 8
        radius: 4
        opacity: delegate.isDragSource ? 0.35 : 1
        color: delegate.blockSelected ? Theme.blockSelectionTint
             : delegate.isFocused ? Theme.focusTint
             : (delegate.isHovered ? Theme.blockHoverTint : "transparent")
        // A visible keyboard-focus ring (§14.1) in addition to the tint.
        border.color: delegate.blockSelected ? Theme.accent
                    : delegate.isFocused ? Theme.focusRing : "transparent"
        border.width: (delegate.blockSelected || delegate.isFocused) ? 2 : 0
    }

    Column {
        id: contentColumn
        anchors.top: parent.top
        anchors.topMargin: 8
        // Alignment (features.md §9.2): centered by default, or pinned
        // left (past the gutter) / right. Detaching the unused anchors
        // with `undefined`.
        anchors.horizontalCenter: delegate.imageAlign === "center"
            ? parent.horizontalCenter : undefined
        anchors.horizontalCenterOffset: 12   // clear the gutter (center only)
        anchors.left: delegate.imageAlign === "left" ? parent.left : undefined
        anchors.leftMargin: 36
        anchors.right: delegate.imageAlign === "right" ? parent.right : undefined
        anchors.rightMargin: 8
        spacing: 4
        opacity: delegate.isDragSource ? 0.35 : 1

        // The image (or the broken-path placeholder).
        Item {
            id: imageFrame
            objectName: "imageAccessible"
            // Alt text surfaced to assistive technology (§14.2). Falls back to
            // the caption, then a generic label, so an image is never nameless.
            Accessible.role: Accessible.Graphic
            Accessible.name: delegate.img.alt !== "" ? delegate.img.alt
                : (delegate.img.caption !== "" ? delegate.img.caption
                                               : qsTr("Image"))
            width: delegate.effectiveWidth
            height: image.status === Image.Ready && image.implicitHeight > 0
                ? delegate.effectiveWidth * (image.implicitHeight / image.implicitWidth)
                : 160
            anchors.horizontalCenter: parent.horizontalCenter

            // Whether an effect needs the MultiEffect layer (rounding/shadow);
            // a plain image draws directly for zero overhead (the common case).
            readonly property bool effectsActive: delegate.imgRounded || delegate.imgShadow

            Image {
                id: image
                anchors.fill: parent
                source: delegate.displaySource
                asynchronous: true
                cache: true
                // Maintain aspect by default (§1.2.8); `aspect=stretch` fills.
                fillMode: delegate.imgStretch ? Image.Stretch : Image.PreserveAspectFit
                // Hidden while the MultiEffect renders it rounded/shadowed.
                visible: source !== "" && status !== Image.Error
                        && !imageFrame.effectsActive
                // Alt text as a tooltip (accessibility, §1.2.8).
                ToolTip.visible: hoverArea.containsMouse && delegate.img.alt !== ""
                ToolTip.text: delegate.img.alt
            }

            // Rounded-corner mask source (offscreen).
            Rectangle {
                id: roundMask
                anchors.fill: parent
                radius: delegate.imgRadius
                visible: false
                layer.enabled: true
            }

            // Rounded corners and/or drop shadow (§1.2.8) via one MultiEffect.
            MultiEffect {
                anchors.fill: image
                source: image
                visible: imageFrame.effectsActive
                        && delegate.resolvedSource !== "" && image.status === Image.Ready
                maskEnabled: delegate.imgRounded
                maskSource: roundMask
                shadowEnabled: delegate.imgShadow
                shadowColor: "#66000000"
                shadowBlur: 0.7
                shadowVerticalOffset: 3
                autoPaddingEnabled: false
            }

            // Optional border, matching the corner radius (§1.2.8).
            Rectangle {
                anchors.fill: image
                visible: delegate.imgBorder && delegate.resolvedSource !== ""
                color: "transparent"
                radius: delegate.imgRounded ? delegate.imgRadius : 0
                border.width: 2
                border.color: delegate.imgBorderColor
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: image.status === Image.Loading
                visible: running
                width: 32; height: 32
            }

            // Unapproved remote image: an inert tile that offers to load it.
            // Distinct from the broken-path placeholder below, because
            // nothing is broken — the image simply has not been requested.
            Rectangle {
                objectName: "imageConsentPlaceholder"
                anchors.fill: parent
                visible: delegate.awaitingConsent
                color: Theme.codePanelBackground
                border.color: Theme.border
                radius: 6
                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "🔗"
                        font.pixelSize: 24
                        color: Theme.textFaint
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Remote image not loaded")
                        color: Theme.textMuted
                        font.pixelSize: 12
                    }
                    Rectangle {
                        objectName: "imageLoadButton"
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: imgLoadLabel.implicitWidth + 16
                        height: imgLoadLabel.implicitHeight + 8
                        radius: 4
                        visible: EgressPolicy.canRequestConsent(delegate.resolvedSource)
                        color: Theme.hoverTint
                        border.color: imgLoadArea.containsMouse ? Theme.accent : Theme.border
                        Text {
                            id: imgLoadLabel
                            anchors.centerIn: parent
                            text: qsTr("Load image")
                            font.pixelSize: 11
                            color: imgLoadArea.containsMouse ? Theme.textPrimary
                                                             : Theme.textMuted
                        }
                        MouseArea {
                            id: imgLoadArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: EgressPolicy.allowOrigin(delegate.resolvedSource)
                        }
                    }
                }
            }

            // Broken-path / unsupported placeholder (§1.2.8).
            Rectangle {
                anchors.fill: parent
                visible: !delegate.awaitingConsent
                    && (delegate.resolvedSource === "" || image.status === Image.Error)
                color: Theme.codePanelBackground
                border.color: Theme.border
                radius: 6
                Column {
                    anchors.centerIn: parent
                    spacing: 4
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "▨"
                        font.pixelSize: 28
                        color: Theme.textFaint
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: delegate.img.path === "" ? qsTr("No image")
                              : qsTr("Image not found: ") + delegate.img.path
                        color: Theme.textMuted
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                        width: Math.min(implicitWidth, imageFrame.width - 16)
                    }
                }
            }

            // Click opens the lightbox (only a resolved image).
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                enabled: delegate.displaySource !== "" && image.status === Image.Ready
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    // The lightbox gets the gated source, not the raw URL:
                    // handing it the URL would reopen the direct-load path
                    // this delegate just closed.
                        AppActions.requestLightbox(delegate.displaySource, delegate.img.alt)
                }
            }

            // Resize handle (bottom-right corner): drag to set width; aspect
            // is preserved (only width is stored). Visible on hover/focus.
            Rectangle {
                id: resizeHandle
                objectName: "imageResizeHandle"
                width: 14; height: 14; radius: 3
                color: Theme.accent
                opacity: (delegate.isHovered || delegate.isFocused)
                         && delegate.resolvedSource !== "" ? 0.9 : 0
                visible: opacity > 0
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                Behavior on opacity { NumberAnimation { duration: 120 } }

                MouseArea {
                    id: resizeArea
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.SizeFDiagCursor
                    preventStealing: true
                    property real startW: 0
                    property real pressX: 0
                    property int liveWidth: 0
                    onPressed: function(mouse) {
                        startW = imageFrame.width
                        pressX = mapToItem(delegate, mouse.x, mouse.y).x
                        liveWidth = Math.round(startW)
                        delegate.previewWidth = liveWidth
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        var cur = mapToItem(delegate, mouse.x, mouse.y).x
                        var w = Math.round(startW + (cur - pressX))
                        w = Math.max(40, Math.min(w, delegate.maxWidth))
                        liveWidth = w
                        delegate.previewWidth = w   // live preview
                    }
                    onReleased: {
                        // Commit the new width as one undo step, then hand the
                        // frame back to its binding. writeImage updates the
                        // model row this delegate parses, so displayWidth
                        // already carries the new width and nothing flickers.
                        delegate.writeImage(delegate.img.path, delegate.img.alt,
                                            delegate.img.caption, liveWidth)
                        delegate.previewWidth = 0
                    }
                }
            }

            // Effects button (top-right): opens the rounded/shadow/border/
            // aspect popover (§1.2.8). Visible on hover/focus.
            Rectangle {
                id: effectsButton
                objectName: "imageEffectsButton"
                width: 22; height: 22; radius: 4
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 6
                color: effectsArea.containsMouse ? Theme.hoverTint
                     : Qt.rgba(0, 0, 0, 0.35)
                opacity: (delegate.isHovered || delegate.isFocused
                          || imageEffectsPopover.visible)
                         && delegate.resolvedSource !== "" ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
                Text {
                    anchors.centerIn: parent
                    text: "✦"; color: "white"; font.pixelSize: 13
                }
                MouseArea {
                    id: effectsArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: imageEffectsPopover.open()
                }
                ImageEffectsPopover {
                    id: imageEffectsPopover
                    x: parent.width - width
                    y: parent.height + 4
                    attributes: delegate.attributes
                    onApplied: function(payload) { delegate.setImageEffects(payload) }
                }
            }
        }

        // Caption: an editable single-line field below the image (§1.2.8).
        TextField {
            id: captionField
            objectName: "imageCaption"
            width: imageFrame.width
            anchors.horizontalCenter: parent.horizontalCenter
            visible: delegate.resolvedSource !== "" || text !== ""
            text: delegate.img.caption
            placeholderText: qsTr("Add a caption…")
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 12
            font.italic: true
            color: Theme.textMuted
            background: null
            // Same hazard as the other deferred editors: editingFinished may
            // never arrive if the model is replaced first, so the caption is
            // also committed on the document-level flush.
            function commitPendingCaption() {
                if (text !== delegate.img.caption)
                    delegate.writeImage(delegate.img.path, delegate.img.alt,
                                        text, delegate.img.width)
            }
            Connections {
                target: DocumentManager
                function onPendingEditsRequested() {
                    captionField.commitPendingCaption()
                }
            }
            onEditingFinished: {
                captionField.commitPendingCaption()
            }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // Let the image/caption/handle areas above take their clicks; this
        // covers the surrounding block region for selection gestures.
        propagateComposedEvents: true
        onPressed: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                mouse.accepted = false
                return
            }
            mouse.accepted = false
        }
        onClicked: function(mouse) {
            if (mouse.modifiers & Qt.ControlModifier) {
                DocumentSelection.toggleBlock(delegate.index)
                if (DocumentSelection.hasBlockSelection)
                    delegate.focusSelectionHandler()
                else
                    focusTarget.forceActiveFocus()
                return
            }
            if (mouse.modifiers & Qt.ShiftModifier) {
                var anchor = delegate.shell && delegate.shell.lastFocusedBlock !== undefined
                        ? delegate.shell.lastFocusedBlock : -1
                if (!DocumentSelection.hasBlockSelection
                    && anchor >= 0 && anchor !== delegate.index)
                    DocumentSelection.selectBlock(anchor)
                DocumentSelection.extendBlockSelectionTo(delegate.index)
                delegate.focusSelectionHandler()
                return
            }
            if (DocumentSelection.hasBlockSelection
                || DocumentSelection.hasTextSelection)
                DocumentSelection.clear()
            focusTarget.forceActiveFocus()
        }
    }

    // Gutter plus-button.
    Rectangle {
        objectName: "plusButton"
        width: 18; height: 18; x: 10
        y: 8
        radius: 4
        color: plusArea.containsMouse ? Theme.hoverTint : "transparent"
        opacity: delegate.isHovered ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Text {
            anchors.centerIn: parent
            text: "+"; color: Theme.textMuted; font.pixelSize: 14; font.bold: true
        }
        MouseArea {
            id: plusArea
            anchors.fill: parent
            anchors.margins: -2
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: delegate.insertBlockBelowAndOpenMenu()
        }
    }

    // Drag handle dots.
    Item {
        objectName: "imageHandle"
        width: 14; height: 18; x: 30
        y: 8
        opacity: delegate.isHovered || imageHandleArea.pressed ? 0.6 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Column {
            anchors.centerIn: parent
            spacing: 2
            Repeater {
                model: 2
                Row {
                    spacing: 2
                    Repeater {
                        model: 2
                        Rectangle { width: 3; height: 3; radius: 1.5; color: Theme.textFaint }
                    }
                }
            }
        }
        MouseArea {
            id: imageHandleArea
            objectName: "dragHandle"
            anchors.fill: parent
            anchors.margins: -2
            hoverEnabled: true
            cursorShape: Qt.OpenHandCursor
            preventStealing: true
            property real pressX: 0
            property real pressY: 0
            property bool dragging: false
            onPressed: function(mouse) {
                pressX = mouse.x; pressY = mouse.y; dragging = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                if (!delegate.shell || !delegate.shell.blockDrag) return
                var sp = imageHandleArea.mapToItem(null, mouse.x, mouse.y)
                if (!dragging) {
                    if (Math.abs(mouse.x - pressX) < 5
                        && Math.abs(mouse.y - pressY) < 5)
                        return
                    dragging = true
                    delegate.shell.blockDrag.begin(delegate.index, sp.x, sp.y)
                } else {
                    delegate.shell.blockDrag.update(sp.x, sp.y)
                }
            }
            onReleased: {
                if (dragging) {
                    dragging = false
                    if (delegate.shell && delegate.shell.blockDrag) delegate.shell.blockDrag.drop()
                    return
                }
                if (delegate.listView)
                    delegate.listView.currentIndex = delegate.index
                DocumentSelection.selectBlock(delegate.index)
                delegate.focusSelectionHandler()
            }
            onCanceled: {
                if (dragging) {
                    dragging = false
                    if (delegate.shell && delegate.shell.blockDrag) delegate.shell.blockDrag.cancel()
                }
            }
        }
    }
}
