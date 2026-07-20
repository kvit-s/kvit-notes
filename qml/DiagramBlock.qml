// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Window
import Kvit 1.0

// Mermaid diagram block (diagrams-prd.md §5.1). The block is a `mermaid` code
// fence; its content is the verbatim Mermaid source. Unfocused, it shows the
// natively rendered diagram (parsed and laid out off the UI thread by
// DiagramCanvas), fit to the visible window (both dimensions) with pan when
// zoomed in, with hover controls for fit/zoom, Copy source, Save PNG
// (Phase 4), and treat-as-code; the current zoom level shows bottom-right.
// Focused, it shows the source in a monospace editor above a debounced live
// preview that keeps the last valid render while the new source is invalid,
// with line/column diagnostics. Unsupported diagram families fall back to
// editable source with a clear diagnostic; the Markdown is never discarded.
// Keeps the non-text focus API of the other wave-2 blocks.
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
    property bool isFocused: sourceArea.activeFocus
    property bool isHovered: hoverArea.containsMouse

    readonly property bool editing: sourceArea.activeFocus
    property string previewSource: content

    property bool fitMode: true
    property real zoomLevel: 1.0
    readonly property int maxReadHeight: 720
    readonly property int labelFontSize: typography.sizeForBlockType(Block.Paragraph)

    readonly property bool blockSelected: {
        var revision = documentSelection.revision
        return documentSelection.isBlockSelected(root.index)
            || documentSelection.portionForBlock(root.index).selected === true
    }

    // ---- non-text focus API (matches MathBlock) ----
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
            previewSource = content
        }
    }
    // A pooled delegate reused for a different block must not carry the
    // previous block's rendered scene ("preview is from the last valid
    // source" is per-block) nor its zoom state.
    onBlockIdChanged: {
        readCanvas.resetScene()
        previewCanvas.resetScene()
        fitMode = true
        zoomLevel = 1.0
        previewSource = content
    }

    implicitHeight: contentColumn.implicitHeight + 16

    ListView.onPooled: { isPooled = true; opacity = 0 }
    ListView.onReused: { isPooled = false; opacity = 1 }

    function focusAtStart() { sourceArea.forceActiveFocus(); sourceArea.cursorPosition = 0 }
    function focusAtEnd() { sourceArea.forceActiveFocus(); sourceArea.cursorPosition = sourceArea.length }

    // ---- §20.1 read-state selection ----
    function selectDiagramElement(nodeId, edgeIndex) {
        if (nodeId !== "")
            readCanvas.selectedNodeId = nodeId
        else if (edgeIndex >= 0)
            readCanvas.selectedEdgeIndex = edgeIndex
        selectionKeys.forceActiveFocus()
        announceSelection()
    }
    // §20.5 read state: expose the element's source line through the status
    // affordance without entering the editor.
    function announceSelection() {
        if (!readCanvas.hasSelection)
            return
        var off = readCanvas.sourceOffsetForSelection()
        var line = readCanvas.sourceLineForOffset(off)
        var msg = readCanvas.selectionLabel()
                + (line > 0 ? qsTr(" — line %1").arg(line) : "")
        var win = Window.window
        if (win && win.showTransientStatus)
            win.showTransientStatus(msg)
        if (typeof a11y !== "undefined")
            a11y.announce(msg)
    }
    // Enter the source editor with the cursor on the selected element.
    function editSelectionSource() {
        var off = readCanvas.sourceOffsetForSelection()
        sourceArea.forceActiveFocus()
        sourceArea.cursorPosition =
            off >= 0 ? Math.min(off, sourceArea.length) : sourceArea.length
    }
    // §20.2: apply a gesture's source edit as ONE undo step, or surface the
    // refusal through the status affordance and announcer.
    function applyGesture(src, doneMessage) {
        if (src !== "" && src !== root.content) {
            blockModel.updateContent(root.index, src)
            var win = Window.window
            if (doneMessage !== undefined && doneMessage !== "") {
                if (win && win.showTransientStatus)
                    win.showTransientStatus(doneMessage)
                if (typeof a11y !== "undefined")
                    a11y.announce(doneMessage)
            }
            return true
        }
        var err = readCanvas.gestureError
        if (err !== "") {
            var w = Window.window
            if (w && w.showTransientStatus)
                w.showTransientStatus(err)
            if (typeof a11y !== "undefined")
                a11y.announce(err)
        }
        return false
    }
    function focusAtPosition(markdownPos) { sourceArea.forceActiveFocus() }
    function isCursorOnFirstLine() {
        var cursorRect = sourceArea.positionToRectangle(sourceArea.cursorPosition)
        var firstRect = sourceArea.positionToRectangle(0)
        return Math.abs(cursorRect.y - firstRect.y) < 1
    }
    function isCursorOnLastLine() {
        var cursorRect = sourceArea.positionToRectangle(sourceArea.cursorPosition)
        var lastRect = sourceArea.positionToRectangle(sourceArea.length)
        return Math.abs(cursorRect.y - lastRect.y) < 1
    }
    function focusAdjacentBlock(direction) {
        var targetIndex = root.index + direction
        if (!root.listView || targetIndex < 0 || targetIndex >= blockModel.count)
            return false
        root.listView.currentIndex = targetIndex
        var target = root.listView.itemAtIndex(targetIndex)
        if (!target) return false
        if (direction < 0) target.focusAtEnd(); else target.focusAtStart()
        return true
    }
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

    Timer {
        id: debounce
        interval: 250
        onTriggered: {
            root.previewSource = sourceArea.text
            if (sourceArea.text !== root.content)
                blockModel.updateContent(root.index, sourceArea.text)
        }
    }

    // Shared theme bindings for a DiagramCanvas.
    component ThemedCanvas: DiagramCanvas {
        fontFamily: typography.fontFamily
        fontPixelSize: root.labelFontSize
        nodeFillColor: theme.chipBackground
        nodeStrokeColor: theme.accent
        edgeColor: theme.textSecondary
        labelColor: theme.textPrimary
        edgeLabelColor: theme.textMuted
        edgeLabelBackground: theme.panelBackground
        subgraphFillColor: theme.blockHoverTint
        subgraphStrokeColor: theme.border
        noteFillColor: theme.highlightBackground
        noteStrokeColor: theme.warning
        activationFillColor: theme.hoverTint
        pageBackgroundColor: theme.windowBackground
        selectionRingColor: theme.focusRing
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

    Column {
        id: contentColumn
        x: 34; y: 8
        width: root.width - 46
        spacing: 6
        opacity: root.isDragSource ? 0.35 : 1

        // ---- Rendered (read) view ----
        Rectangle {
            id: readPanel
            width: parent.width
            visible: !root.editing
            height: root.editing ? 0 : readBody.height + 10
            radius: 6
            color: theme.panelBackground
            border.color: theme.border
            border.width: 1

            Column {
                id: readBody
                x: 8; y: 5
                width: parent.width - 16
                spacing: 4

                Flickable {
                    id: readFlick
                    objectName: "diagramReadFlick"
                    width: parent.width
                    height: Math.max(24, Math.min(readCanvas.implicitHeight, root.maxReadHeight))
                    visible: readCanvas.hasScene
                    clip: true
                    contentWidth: readCanvas.implicitWidth
                    contentHeight: readCanvas.implicitHeight
                    interactive: contentWidth > width || contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds

                    ThemedCanvas {
                        id: readCanvas
                        objectName: "diagramReadCanvas"
                        source: root.content
                        // Fit must satisfy BOTH dimensions: a long TD
                        // flowchart fits its width at 1.0 yet overflows the
                        // maxReadHeight cap, so the height ratio governs.
                        renderScale: root.fitMode
                            ? Math.min(1.0,
                                       sceneWidth > 0 ? readFlick.width / sceneWidth : 1.0,
                                       sceneHeight > 0 ? root.maxReadHeight / sceneHeight : 1.0)
                            : root.zoomLevel
                        width: implicitWidth
                        height: implicitHeight

                        // §20.1: clicking a node or edge selects it; empty
                        // canvas enters the source editor. Dragging a node is
                        // manual arrangement (§20.3), committed on release as
                        // one undo step. Both are gated on the scene matching
                        // the current source revision.
                        MouseArea {
                            id: canvasArea
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            preventStealing: dragging || pressNode !== ""
                            property bool dragging: false
                            property real pressX: 0
                            property real pressY: 0
                            property string pressNode: ""
                            property int pressEdge: -1
                            onPressed: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    // §20.1 context menu on the element under
                                    // the cursor (or the current selection).
                                    if (readCanvas.sceneCurrent) {
                                        var nid = readCanvas.nodeAt(mouse.x, mouse.y)
                                        if (nid !== "") {
                                            root.selectDiagramElement(nid, -1)
                                        } else {
                                            var e = readCanvas.edgeAt(mouse.x, mouse.y)
                                            if (e >= 0)
                                                root.selectDiagramElement("", e)
                                        }
                                        contextMenu.popup()
                                    }
                                    return
                                }
                                pressX = mouse.x
                                pressY = mouse.y
                                dragging = false
                                pressNode = readCanvas.sceneCurrent
                                    ? readCanvas.nodeAt(mouse.x, mouse.y) : ""
                                pressEdge = readCanvas.sceneCurrent
                                    ? readCanvas.edgeAt(mouse.x, mouse.y) : -1
                            }
                            onDoubleClicked: function(mouse) {
                                if (mouse.button !== Qt.LeftButton
                                    || !readCanvas.sceneCurrent)
                                    return
                                // §20.1: double-click opens the inline label
                                // editor on the node.
                                var nid = readCanvas.nodeAt(mouse.x, mouse.y)
                                if (nid !== "") {
                                    root.selectDiagramElement(nid, -1)
                                    labelEditor.openFor(false)
                                }
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                if (!dragging && pressNode !== ""
                                    && readCanvas.supportsArrangement
                                    && (Math.abs(mouse.x - pressX) > 4
                                        || Math.abs(mouse.y - pressY) > 4)) {
                                    dragging = readCanvas.startNodeDrag(
                                        pressNode, pressX, pressY)
                                }
                                if (dragging)
                                    readCanvas.updateNodeDrag(mouse.x, mouse.y)
                            }
                            onReleased: function(mouse) {
                                if (mouse.button !== Qt.LeftButton)
                                    return
                                if (dragging) {
                                    dragging = false
                                    var newSrc = readCanvas.finishNodeDragSource()
                                    if (newSrc !== "" && newSrc !== root.content) {
                                        blockModel.updateContent(root.index, newSrc)
                                        var win = Window.window
                                        if (win && win.showTransientStatus)
                                            win.showTransientStatus(
                                                qsTr("Arranged %1").arg(pressNode))
                                        if (typeof a11y !== "undefined")
                                            a11y.announce(qsTr("Moved %1").arg(pressNode))
                                    }
                                    pressNode = ""
                                    return
                                }
                                if (!readCanvas.sceneCurrent) {
                                    root.focusAtEnd()
                                    pressNode = ""
                                    return
                                }
                                // Phase 5d drag: one position per gesture.
                                if (readCanvas.supportsSequenceReorder) {
                                    var dx = mouse.x - pressX
                                    var dy = mouse.y - pressY
                                    if (pressNode !== "" && Math.abs(dx) > 30
                                        && Math.abs(dx) > Math.abs(dy)) {
                                        root.selectDiagramElement(pressNode, -1)
                                        root.applyGesture(
                                            readCanvas.moveSelectedParticipantSource(
                                                dx > 0 ? 1 : -1),
                                            qsTr("Moved %1").arg(pressNode))
                                        pressNode = ""
                                        return
                                    }
                                    if (pressEdge >= 0 && Math.abs(dy) > 24
                                        && Math.abs(dy) > Math.abs(dx)) {
                                        root.selectDiagramElement("", pressEdge)
                                        root.applyGesture(
                                            readCanvas.moveSelectedMessageSource(
                                                dy > 0 ? 1 : -1),
                                            qsTr("Moved message"))
                                        pressNode = ""
                                        return
                                    }
                                }
                                var nodeId = readCanvas.nodeAt(mouse.x, mouse.y)
                                if (nodeId !== "") {
                                    root.selectDiagramElement(nodeId, -1)
                                } else {
                                    var edge = readCanvas.edgeAt(mouse.x, mouse.y)
                                    if (edge >= 0) {
                                        root.selectDiagramElement("", edge)
                                    } else {
                                        readCanvas.clearSelection()
                                        root.focusAtEnd()
                                    }
                                }
                                pressNode = ""
                            }
                            onCanceled: {
                                if (dragging) {
                                    dragging = false
                                    readCanvas.cancelNodeDrag()
                                }
                                pressNode = ""
                            }
                        }
                    }

                    // §20.1 keyboard path: Tab/arrows cycle nodes, Escape
                    // clears the selection first, Enter/F2 opens the source
                    // at the element.
                    Item {
                        id: selectionKeys
                        objectName: "diagramSelectionKeys"
                        Accessible.role: Accessible.Graphic
                        Accessible.name: readCanvas.hasSelection
                            ? readCanvas.selectionLabel() : readCanvas.summary
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Escape) {
                                if (readCanvas.hasSelection)
                                    readCanvas.clearSelection()
                                else
                                    root.focusSelectionHandler()
                                event.accepted = true
                                return
                            }
                            if ((event.modifiers & Qt.ControlModifier)
                                && readCanvas.supportsSequenceReorder) {
                                if (event.key === Qt.Key_Up
                                    || event.key === Qt.Key_Down) {
                                    root.applyGesture(
                                        readCanvas.moveSelectedMessageSource(
                                            event.key === Qt.Key_Up ? -1 : 1),
                                        qsTr("Moved"))
                                    event.accepted = true
                                    return
                                }
                                if (event.key === Qt.Key_Left
                                    || event.key === Qt.Key_Right) {
                                    root.applyGesture(
                                        readCanvas.moveSelectedParticipantSource(
                                            event.key === Qt.Key_Left ? -1 : 1),
                                        qsTr("Moved"))
                                    event.accepted = true
                                    return
                                }
                            }
                            var fwd = event.key === Qt.Key_Tab
                                || event.key === Qt.Key_Right
                                || event.key === Qt.Key_Down
                            var back = event.key === Qt.Key_Backtab
                                || event.key === Qt.Key_Left
                                || event.key === Qt.Key_Up
                            if (fwd || back) {
                                readCanvas.cycleNode(fwd ? 1 : -1)
                                root.announceSelection()
                                event.accepted = true
                                return
                            }
                            if (event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter
                                || event.key === Qt.Key_F2) {
                                // §20.1: the label editor on a node; the
                                // source editor otherwise.
                                if (readCanvas.selectedNodeId !== "")
                                    labelEditor.openFor(false)
                                else
                                    root.editSelectionSource()
                                event.accepted = true
                                return
                            }
                            if (event.key === Qt.Key_Delete
                                || event.key === Qt.Key_Backspace) {
                                root.applyGesture(
                                    readCanvas.deleteSelectionSource(),
                                    qsTr("Deleted"))
                                event.accepted = true
                                return
                            }
                            if (event.key === Qt.Key_Menu) {
                                contextMenu.popup()
                                event.accepted = true
                            }
                        }
                    }

                    // §20.1 anchor affordances on the selected node's sides:
                    // drag one to draw a ghost edge, click to add a
                    // connected node.
                    Item {
                        id: nodeAffordances
                        visible: readCanvas.selectedNodeId !== ""
                                 && readCanvas.supportsArrangement
                                 && !canvasArea.dragging
                        property rect selRect: Qt.rect(0, 0, 0, 0)
                        function refresh() {
                            selRect = readCanvas.selectionRect()
                        }
                        Connections {
                            target: readCanvas
                            function onSelectionChanged() { nodeAffordances.refresh() }
                            function onSceneChanged() { nodeAffordances.refresh() }
                            function onRenderScaleChanged() { nodeAffordances.refresh() }
                        }
                        Repeater {
                            model: 4
                            delegate: Rectangle {
                                required property int index
                                readonly property rect box: nodeAffordances.selRect
                                width: 12; height: 12; radius: 6
                                color: theme.accent
                                border.color: theme.panelBackground
                                border.width: 2
                                x: (index === 0 ? box.x + box.width / 2
                                  : index === 1 ? box.x + box.width
                                  : index === 2 ? box.x + box.width / 2
                                  : box.x) - 6
                                y: (index === 0 ? box.y
                                  : index === 1 ? box.y + box.height / 2
                                  : index === 2 ? box.y + box.height
                                  : box.y + box.height / 2) - 6
                                visible: nodeAffordances.visible && box.width > 0
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -5
                                    preventStealing: true
                                    cursorShape: Qt.CrossCursor
                                    property bool dragging: false
                                    onPositionChanged: function(mouse) {
                                        if (!pressed) return
                                        var p = mapToItem(readCanvas,
                                                          mouse.x, mouse.y)
                                        if (!dragging)
                                            dragging = readCanvas.startEdgeDrag(p.x, p.y)
                                        else
                                            readCanvas.updateEdgeDrag(p.x, p.y)
                                    }
                                    onReleased: {
                                        if (dragging) {
                                            dragging = false
                                            root.applyGesture(
                                                readCanvas.finishEdgeDragSource(),
                                                qsTr("Connected"))
                                        } else {
                                            root.applyGesture(
                                                readCanvas.quickAddSelectionSource(),
                                                qsTr("Added a connected node"))
                                        }
                                    }
                                    onCanceled: {
                                        if (dragging) {
                                            dragging = false
                                            readCanvas.cancelEdgeDrag()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Empty / unsupported / error placeholder (no valid scene yet).
                Column {
                    width: parent.width
                    visible: !readCanvas.hasScene
                    spacing: 3
                    Text {
                        visible: root.content.trim().length === 0
                        text: qsTr("Empty Mermaid diagram — click to edit")
                        color: theme.textFaint; font.italic: true; font.pixelSize: 13
                        TapHandler { onTapped: root.focusAtEnd() }
                    }
                    Text {
                        visible: root.content.trim().length > 0 && readCanvas.unsupportedFamily
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("Unsupported Mermaid diagram type in this Kvit version. "
                                   + "The source is preserved — click to edit, or treat it as code.")
                        color: theme.textMuted; font.pixelSize: 12
                        TapHandler { onTapped: root.focusAtEnd() }
                    }
                    Text {
                        visible: root.content.trim().length > 0 && readCanvas.hasError
                                 && !readCanvas.unsupportedFamily
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: "⚠ " + readCanvas.errorText
                              + (readCanvas.errorLine > 0
                                 ? " (line " + readCanvas.errorLine + ")" : "")
                        color: theme.danger; font.pixelSize: 12
                        TapHandler { onTapped: root.focusAtEnd() }
                    }
                    Text {
                        visible: root.content.trim().length > 0 && !readCanvas.hasError
                                 && !readCanvas.hasScene
                        text: qsTr("Rendering…")
                        color: theme.textFaint; font.pixelSize: 12
                    }
                }

                // Last-good note when a rendered scene coexists with an error.
                Text {
                    visible: readCanvas.hasScene && readCanvas.hasError
                    text: qsTr("⚠ Preview is from the last valid source")
                    color: theme.warning; font.pixelSize: 11
                }
            }

            // Hover controls.
            Row {
                anchors.right: parent.right; anchors.top: parent.top
                anchors.rightMargin: 6; anchors.topMargin: 4
                spacing: 4
                opacity: root.isHovered ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 120 } }

                component ChipButton: Rectangle {
                    id: chip
                    property string label: ""
                    property bool active: false
                    signal clicked()
                    width: chipText.implicitWidth + 12; height: 18; radius: 4
                    color: chipArea.containsMouse ? theme.hoverTint
                         : (active ? theme.selectionTint : theme.chipBackground)
                    border.color: theme.border; border.width: 1
                    Text { id: chipText; anchors.centerIn: parent; text: chip.label
                        color: theme.textSecondary; font.pixelSize: 10 }
                    MouseArea { id: chipArea; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: chip.clicked() }
                }

                Text { anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Mermaid"); color: theme.textFaint; font.pixelSize: 10 }
                ChipButton { label: qsTr("Fit"); active: root.fitMode
                    onClicked: root.fitMode = true }
                ChipButton { label: "100%"; active: !root.fitMode && root.zoomLevel === 1.0
                    onClicked: { root.fitMode = false; root.zoomLevel = 1.0 } }
                // Zoom steps continue from the scale on screen (which in fit
                // mode is the fit scale, read before fitMode flips the
                // renderScale binding over to zoomLevel).
                ChipButton { label: "−"; onClicked: {
                    root.zoomLevel = Math.max(0.05, readCanvas.renderScale / 1.25)
                    root.fitMode = false } }
                ChipButton { label: "+"; onClicked: {
                    root.zoomLevel = Math.min(3.0, readCanvas.renderScale * 1.25)
                    root.fitMode = false } }
                ChipButton { label: qsTr("Copy"); onClicked: clipboard.text = root.content }
                // "Copy as ASCII diagram" (pre-launch-plan.md §2.3): the
                // rendered scene as Unicode box-drawing text — the same
                // vocabulary the crooked-diagram repair recognizes.
                ChipButton {
                    objectName: "diagramCopyTextChip"
                    label: qsTr("Copy as text")
                    visible: readCanvas.sceneCurrent
                    onClicked: {
                        var text = readCanvas.textDiagram()
                        if (text !== "")
                            clipboard.text = text
                    }
                }
                ChipButton {
                    label: qsTr("Reset layout")
                    visible: readCanvas.hasArrangement
                    onClicked: {
                        var s = readCanvas.resetArrangementSource()
                        if (s !== "" && s !== root.content)
                            blockModel.updateContent(root.index, s)
                    }
                }
                ChipButton { label: qsTr("PNG"); visible: readCanvas.hasScene
                    onClicked: savePngDialog.open() }
                ChipButton { label: qsTr("Edit"); onClicked: root.focusAtEnd() }
                ChipButton { label: qsTr("As code")
                    onClicked: blockModel.convertBlock(root.index, Block.CodeBlock,
                                                       root.content, false, "plain") }
            }

            // Current zoom level (the effective render scale, so it also
            // reflects what Fit chose), bottom-right of the rendered window.
            Rectangle {
                objectName: "diagramZoomIndicator"
                anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.margins: 6
                width: zoomText.implicitWidth + 10; height: 16; radius: 4
                color: theme.chipBackground
                border.color: theme.border; border.width: 1
                visible: readCanvas.hasScene
                Text {
                    id: zoomText
                    objectName: "diagramZoomText"
                    anchors.centerIn: parent
                    text: Math.round(readCanvas.renderScale * 100) + "%"
                    color: theme.textFaint; font.pixelSize: 9
                }
            }
        }

        // ---- Source editor ----
        Flickable {
            id: sourceFlick
            width: parent.width
            visible: root.editing
            height: root.editing ? Math.min(sourceArea.implicitHeight, 360) : 0
            clip: true
            contentWidth: sourceArea.implicitWidth
            contentHeight: sourceArea.implicitHeight
            interactive: contentWidth > width
            boundsBehavior: Flickable.StopAtBounds

            TextArea {
                id: sourceArea
                objectName: "mermaidSourceArea"
                width: Math.max(implicitWidth, sourceFlick.width)
                text: root.content
                font.family: typography.monoFamily
                font.pixelSize: typography.sizeForBlockType(Block.CodeBlock)
                color: theme.textPrimary
                wrapMode: TextEdit.NoWrap
                selectByMouse: true
                background: Rectangle {
                    color: theme.codePanelBackground
                    radius: 4
                    border.color: theme.border; border.width: 1
                }
                onActiveFocusChanged: {
                    if (!activeFocus) {
                        debounce.stop()
                        if (text !== root.content)
                            blockModel.updateContent(root.index, text)
                        text = Qt.binding(function() { return root.content })
                    }
                }
                onTextChanged: { if (activeFocus) debounce.restart() }
                // §20.5 source→preview: the cursor's statement highlights in
                // the live preview once the debounced preview catches up.
                onCursorPositionChanged: {
                    if (activeFocus && text === root.previewSource)
                        previewCanvas.highlightSourceOffset(cursorPosition)
                }
                HoverHandler {
                    onPointChanged: {
                        if (sourceArea.text !== root.previewSource)
                            return
                        previewCanvas.highlightSourceOffset(
                            sourceArea.positionAt(point.position.x,
                                                  point.position.y))
                    }
                }
                Keys.onPressed: function(event) {
                    // Tab inserts two spaces in Mermaid source (§5.1).
                    if (event.key === Qt.Key_Tab
                        && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                        insert(cursorPosition, "  ")
                        event.accepted = true
                        return
                    }
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        && (event.modifiers & Qt.ControlModifier)) {
                        debounce.stop()
                        root.previewSource = text
                        if (text !== root.content)
                            blockModel.updateContent(root.index, text)
                        event.accepted = true
                        return
                    }
                    var arrowModifiers = Qt.ControlModifier | Qt.ShiftModifier
                        | Qt.AltModifier | Qt.MetaModifier
                    if (!(event.modifiers & arrowModifiers) && event.key === Qt.Key_Up
                        && root.isCursorOnFirstLine()) {
                        if (root.focusAdjacentBlock(-1)) event.accepted = true
                        return
                    }
                    if (!(event.modifiers & arrowModifiers) && event.key === Qt.Key_Down
                        && root.isCursorOnLastLine()) {
                        if (root.focusAdjacentBlock(1)) event.accepted = true
                        return
                    }
                    if (event.key === Qt.Key_Escape) {
                        root.focusSelectionHandler(); event.accepted = true
                    }
                }
            }
        }

        // ---- Live preview + diagnostics ----
        Rectangle {
            width: parent.width
            visible: root.editing
            height: root.editing
                ? Math.max(Math.min(previewCanvas.implicitHeight, 320), 32)
                  + diagStrip.height + 14
                : 0
            radius: 4
            color: theme.panelBackground
            border.color: theme.border; border.width: 1
            clip: true

            Text {
                anchors.centerIn: parent
                visible: !previewCanvas.hasScene && !previewCanvas.hasError
                text: previewCanvas.rendering ? qsTr("Rendering…") : qsTr("Preview")
                color: theme.textFaint; font.pixelSize: 12
            }
            Flickable {
                id: previewFlick
                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                anchors.margins: 6
                height: Math.min(previewCanvas.implicitHeight, 320)
                clip: true
                visible: previewCanvas.hasScene
                contentWidth: previewCanvas.implicitWidth
                contentHeight: previewCanvas.implicitHeight
                interactive: contentWidth > width || contentHeight > height
                ThemedCanvas {
                    id: previewCanvas
                    source: root.previewSource
                    renderScale: Math.min(1.0,
                        sceneWidth > 0 ? previewFlick.width / sceneWidth : 1.0,
                        sceneHeight > 0 ? 320 / sceneHeight : 1.0)
                    width: implicitWidth
                    height: implicitHeight

                    // §20.5 preview→source: clicking an element moves the
                    // source cursor to its defining statement; hovering
                    // highlights it. Both are gated on the preview matching
                    // the editor text.
                    readonly property bool linked:
                        sceneCurrent && sourceArea.text === root.previewSource
                    TapHandler {
                        onTapped: function(eventPoint) {
                            if (!previewCanvas.linked)
                                return
                            var off = previewCanvas.sourceOffsetAt(
                                eventPoint.position.x, eventPoint.position.y)
                            if (off >= 0) {
                                sourceArea.forceActiveFocus()
                                sourceArea.cursorPosition =
                                    Math.min(off, sourceArea.length)
                            }
                        }
                    }
                    HoverHandler {
                        onPointChanged: {
                            if (!previewCanvas.linked)
                                return
                            previewCanvas.highlightSourceOffset(
                                previewCanvas.sourceOffsetAt(point.position.x,
                                                             point.position.y))
                        }
                    }
                }
            }
            // Diagnostics strip.
            Column {
                id: diagStrip
                anchors.bottom: parent.bottom
                anchors.left: parent.left; anchors.right: parent.right
                anchors.margins: 6
                spacing: 1
                Text {
                    visible: previewCanvas.hasError
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: (previewCanvas.errorLine > 0
                           ? "line " + previewCanvas.errorLine + ":" + previewCanvas.errorColumn + "  " : "")
                          + "⚠ " + previewCanvas.errorText
                    color: theme.danger; font.pixelSize: 11
                }
                Text {
                    visible: previewCanvas.hasError && previewCanvas.hasScene
                    text: qsTr("Preview is from the last valid source")
                    color: theme.warning; font.pixelSize: 10
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

    // §20.1 context menu over the selected element. Every action has a name
    // and a keyboard path (menu key on a selection).
    Menu {
        id: contextMenu
        objectName: "diagramContextMenu"

        MenuItem {
            text: qsTr("Rename id…")
            visible: readCanvas.supportsArrangement
            height: visible ? implicitHeight : 0
            enabled: readCanvas.selectedNodeId !== ""
            onTriggered: labelEditor.openFor(true)
        }
        MenuItem {
            text: qsTr("Edit label…")
            visible: readCanvas.supportsArrangement
            height: visible ? implicitHeight : 0
            enabled: readCanvas.selectedNodeId !== ""
            onTriggered: labelEditor.openFor(false)
        }
        // Phase 5d: sequence diagrams reorder by statement order (§20.4).
        MenuItem {
            text: qsTr("Move message up")
            visible: readCanvas.supportsSequenceReorder
            height: visible ? implicitHeight : 0
            enabled: readCanvas.selectedEdgeIndex >= 0
            onTriggered: root.applyGesture(
                readCanvas.moveSelectedMessageSource(-1), qsTr("Moved up"))
        }
        MenuItem {
            text: qsTr("Move message down")
            visible: readCanvas.supportsSequenceReorder
            height: visible ? implicitHeight : 0
            enabled: readCanvas.selectedEdgeIndex >= 0
            onTriggered: root.applyGesture(
                readCanvas.moveSelectedMessageSource(1), qsTr("Moved down"))
        }
        MenuItem {
            text: qsTr("Move participant left")
            visible: readCanvas.supportsSequenceReorder
            height: visible ? implicitHeight : 0
            enabled: readCanvas.selectedNodeId !== ""
            onTriggered: root.applyGesture(
                readCanvas.moveSelectedParticipantSource(-1), qsTr("Moved left"))
        }
        MenuItem {
            text: qsTr("Move participant right")
            visible: readCanvas.supportsSequenceReorder
            height: visible ? implicitHeight : 0
            enabled: readCanvas.selectedNodeId !== ""
            onTriggered: root.applyGesture(
                readCanvas.moveSelectedParticipantSource(1), qsTr("Moved right"))
        }
        Menu {
            id: shapeMenu
            // Menu.visible controls whether the submenu popup is open; it is
            // not the visibility of the submenu's entry in its parent menu.
            // Binding it to an async render result can therefore try to open
            // this menu while contextMenu is closed (and crashes Qt 6.10's
            // QQuickMenu). Keep unavailable submenus disabled instead.
            title: qsTr("Shape")
            enabled: readCanvas.selectedNodeId !== ""
                     && readCanvas.supportsArrangement
            Repeater {
                model: [
                    { name: "rect", label: qsTr("Rectangle") },
                    { name: "rounded", label: qsTr("Rounded") },
                    { name: "stadium", label: qsTr("Stadium") },
                    { name: "subroutine", label: qsTr("Subroutine") },
                    { name: "cylinder", label: qsTr("Cylinder") },
                    { name: "circle", label: qsTr("Circle") },
                    { name: "rhombus", label: qsTr("Decision") },
                    { name: "hexagon", label: qsTr("Hexagon") },
                    { name: "parallelogram", label: qsTr("Parallelogram") },
                    { name: "trapezoid", label: qsTr("Trapezoid") },
                ]
                MenuItem {
                    required property var modelData
                    text: modelData.label
                    onTriggered: root.applyGesture(
                        readCanvas.shapeSelectionSource(modelData.name),
                        qsTr("Shape changed"))
                }
            }
        }
        Menu {
            id: edgeStyleMenu
            title: qsTr("Edge style")
            enabled: readCanvas.selectedEdgeIndex >= 0
                     && readCanvas.supportsArrangement
            MenuItem { text: qsTr("Solid"); onTriggered: root.applyGesture(
                readCanvas.edgeStyleSelectionSource("solid"), qsTr("Edge restyled")) }
            MenuItem { text: qsTr("Dotted"); onTriggered: root.applyGesture(
                readCanvas.edgeStyleSelectionSource("dotted"), qsTr("Edge restyled")) }
            MenuItem { text: qsTr("Thick"); onTriggered: root.applyGesture(
                readCanvas.edgeStyleSelectionSource("thick"), qsTr("Edge restyled")) }
        }
        Menu {
            id: colorMenu
            title: qsTr("Color")
            enabled: readCanvas.selectedNodeId !== ""
                     && readCanvas.supportsArrangement
            Repeater {
                model: [
                    { label: qsTr("Red"), fill: "#fecaca", stroke: "#dc2626" },
                    { label: qsTr("Orange"), fill: "#fed7aa", stroke: "#ea580c" },
                    { label: qsTr("Yellow"), fill: "#fef08a", stroke: "#ca8a04" },
                    { label: qsTr("Green"), fill: "#bbf7d0", stroke: "#16a34a" },
                    { label: qsTr("Blue"), fill: "#bfdbfe", stroke: "#2563eb" },
                    { label: qsTr("Purple"), fill: "#e9d5ff", stroke: "#9333ea" },
                    { label: qsTr("Gray"), fill: "#e5e7eb", stroke: "#4b5563" },
                ]
                MenuItem {
                    required property var modelData
                    text: modelData.label
                    onTriggered: root.applyGesture(
                        readCanvas.styleSelectionSource(modelData.fill,
                                                        modelData.stroke),
                        qsTr("Color applied"))
                }
            }
        }
        MenuItem {
            text: qsTr("Add connected node")
            visible: readCanvas.supportsArrangement
            height: visible ? implicitHeight : 0
            enabled: readCanvas.selectedNodeId !== ""
            onTriggered: root.applyGesture(
                readCanvas.quickAddSelectionSource(),
                qsTr("Added a connected node"))
        }
        MenuItem {
            text: qsTr("Delete")
            visible: readCanvas.supportsArrangement
            height: visible ? implicitHeight : 0
            enabled: readCanvas.hasSelection
            onTriggered: root.applyGesture(
                readCanvas.deleteSelectionSource(), qsTr("Deleted"))
        }
        MenuSeparator { }
        MenuItem {
            text: qsTr("Reset layout")
            visible: readCanvas.supportsArrangement
            height: visible ? implicitHeight : 0
            enabled: readCanvas.hasArrangement
            onTriggered: {
                var s = readCanvas.resetArrangementSource()
                if (s !== "" && s !== root.content)
                    blockModel.updateContent(root.index, s)
            }
        }
        MenuItem {
            text: qsTr("Edit source")
            onTriggered: root.editSelectionSource()
        }
    }

    // §20.1 inline label editor (double-click / F2 / Enter), doubling as the
    // Rename dialog. Positioned over the selected node.
    Popup {
        id: labelEditor
        objectName: "diagramLabelEditor"
        property bool renameMode: false
        padding: 4
        background: Rectangle {
            color: theme.popupBackground
            border.color: theme.accent
            border.width: 1
            radius: 4
        }
        function openFor(rename) {
            if (readCanvas.selectedNodeId === "")
                return
            renameMode = rename
            var r = readCanvas.selectionRect()
            var topLeft = readCanvas.mapToItem(root, r.x, r.y)
            x = Math.max(0, Math.min(topLeft.x, root.width - 220))
            y = Math.max(0, topLeft.y + r.height / 2 - height / 2)
            labelField.text = rename ? readCanvas.selectedNodeId
                                     : readCanvas.selectedNodeLabel()
            open()
            labelField.forceActiveFocus()
            labelField.selectAll()
        }
        contentItem: Row {
            spacing: 4
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: labelEditor.renameMode ? qsTr("Id:") : qsTr("Label:")
                color: theme.textMuted
                font.pixelSize: 11
            }
            TextField {
                id: labelField
                objectName: "diagramLabelField"
                width: 180
                font.pixelSize: 12
                onAccepted: {
                    var src = labelEditor.renameMode
                        ? readCanvas.renameSelectionSource(text)
                        : readCanvas.labelSelectionSource(text)
                    if (root.applyGesture(src, labelEditor.renameMode
                                              ? qsTr("Renamed")
                                              : qsTr("Label updated")))
                        labelEditor.close()
                }
                Keys.onEscapePressed: labelEditor.close()
            }
        }
    }

    // Save PNG (diagrams-prd.md Phase 4): rasterize the rendered scene at 2x.
    FileDialog {
        id: savePngDialog
        objectName: "diagramSavePngDialog"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [ qsTr("PNG images (*.png)") ]
        onAccepted: {
            var win = Window.window
            if (!win) return
            var path = win.urlToLocalPath(selectedFile)
            var ok = readCanvas.savePng(path, 2.0)
            if (win.showTransientStatus)
                win.showTransientStatus(ok ? qsTr("Diagram saved to ") + path
                                           : qsTr("Could not save the diagram"))
        }
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
        objectName: "mermaidHandle"
        width: 14; height: 18; x: 30; y: 8
        opacity: root.isHovered || mermaidHandle.pressed ? 0.6 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Column { anchors.centerIn: parent; spacing: 2
            Repeater { model: 2; Row { spacing: 2; Repeater { model: 2
                Rectangle { width: 3; height: 3; radius: 1.5; color: theme.textFaint } } } } }
        MouseArea {
            id: mermaidHandle
            objectName: "dragHandle"
            anchors.fill: parent; anchors.margins: -2
            hoverEnabled: true; cursorShape: Qt.OpenHandCursor; preventStealing: true
            property real pressX: 0; property real pressY: 0; property bool dragging: false
            onPressed: function(mouse) { pressX = mouse.x; pressY = mouse.y; dragging = false }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                var win = Window.window
                if (!win || !win.blockDrag) return
                var sp = mermaidHandle.mapToItem(null, mouse.x, mouse.y)
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
