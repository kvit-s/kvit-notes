// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// Divider block (features.md §1.2.9): a horizontal rule with no text
// content. It keeps the focus API of the editable delegates so block
// navigation is uniform: arrowing through it works, clicking selects it,
// and (step 4) Backspace/Delete remove it and Enter adds a paragraph
// below.
Item {
    id: delegate

    required property int index
    required property string blockId
    required property int blockType
    required property string content
    required property int indentLevel
    required property bool checked
    required property int ordinal
    // Per-block presentation attributes (phase12 §1.2.9): the divider's style,
    // thickness, color, and width. Absent = a plain 2px full-width rule.
    required property string attributes

    // ---- Divider style (phase12 §1.2.9) ----
    readonly property string divStyle:
        blockAttributes.str(attributes, "style", "solid")
    readonly property int divThickness:
        Math.max(1, Math.min(12, blockAttributes.num(attributes, "thickness", 2)))
    readonly property string divColorAttr:
        blockAttributes.str(attributes, "color", "")
    readonly property color divColor: divColorAttr !== ""
        ? divColorAttr : (isFocused ? theme.accent : theme.border)
    readonly property string divWidthAttr:
        blockAttributes.str(attributes, "width", "full")
    readonly property real divWidthFraction: {
        if (divWidthAttr === "full" || divWidthAttr === "")
            return 1.0
        var pct = parseInt(divWidthAttr)   // "50%" -> 50
        return isNaN(pct) ? 1.0 : Math.max(0.1, Math.min(1.0, pct / 100))
    }
    // Write new divider attributes as one undo step (used by the style picker).
    function setDividerAttributes(payload) {
        blockModel.setBlockAttributes(delegate.index, payload)
    }

    property int blockIndex: index
    property bool isPooled: false
    property ListView listView: ListView.view
    property bool isFocused: focusTarget.activeFocus
    property bool isHovered: hoverArea.containsMouse

    // Block-selection membership and the focus handoff, matching
    // EditableBlock (features.md §3.1 applies to every block type). A
    // divider inside a cross-block text range shows the same tint —
    // it has no text to highlight.
    readonly property bool blockSelected: {
        var revision = documentSelection.revision // dependency only
        return documentSelection.isBlockSelected(delegate.index)
            || documentSelection.portionForBlock(delegate.index).selected === true
    }

    // Cross-block position helpers, matching EditableBlock's API: a
    // divider has a single position, 0.
    function markdownPositionAt(sceneX, sceneY) { return 0 }
    function pointInText(sceneX, sceneY) { return false }
    function lineStepPosition(mdPos, dir) { return -1 }
    function entryPositionAtX(x, fromTop) { return 0 }
    function xAtMarkdown(mdPos) { return 0 }

    // Dragged-row dim, matching EditableBlock (§21.4 space-holder).
    readonly property bool isDragSource: {
        var win = Window.window
        if (!win || !win.blockDrag || !win.blockDrag.active)
            return false
        return win.blockDrag.isMulti ? delegate.blockSelected
                                     : win.blockDrag.sourceIndex === delegate.index
    }

    function focusSelectionHandler() {
        var win = Window.window
        if (win && win.selectionKeyHandler)
            win.selectionKeyHandler.forceActiveFocus()
    }

    // Gaining focus records this as the current block, like
    // EditableBlock (and like there, never via listView.currentIndex,
    // which would move focus to the delegate root).
    onIsFocusedChanged: {
        if (isFocused) {
            var win = Window.window
            if (win && win.lastFocusedBlock !== undefined)
                win.lastFocusedBlock = index
        }
    }

    implicitHeight: 28

    // Same pooled-ghost guard as EditableBlock: pooled rows stay
    // visible unless something zeroes their opacity, and the remove
    // transition cannot be relied on to finish.
    ListView.onPooled: {
        isPooled = true
        focusTarget.focus = false
        opacity = 0
    }

    ListView.onReused: {
        isPooled = false
        opacity = 1
    }

    // Focus API parity with EditableBlock; a divider has no cursor, so
    // every position focuses the block itself.
    function focusAtStart() { focusTarget.forceActiveFocus() }
    function focusAtEnd() { focusTarget.forceActiveFocus() }
    function focusAtPosition(markdownPos) { focusTarget.forceActiveFocus() }
    function isCursorOnFirstLine() { return true }
    function isCursorOnLastLine() { return true }

    function deleteCurrentBlock() {
        var prevIndex = delegate.index - 1
        blockModel.removeBlock(delegate.index)
        Qt.callLater(function() {
            if (listView && prevIndex >= 0) {
                listView.currentIndex = prevIndex
                var item = listView.itemAtIndex(prevIndex)
                if (item) item.focusAtEnd()
            }
        })
    }

    function createBlockBelow() {
        var newIndex = delegate.index + 1
        blockModel.insertBlock(newIndex, 0, "") // 0 = Paragraph
        Qt.callLater(function() {
            if (listView) {
                listView.currentIndex = newIndex
                var item = listView.itemAtIndex(newIndex)
                if (item) item.focusAtStart()
            }
        })
    }

    // The gutter plus-button works on dividers too (features.md §3.7):
    // insert an empty paragraph below and open the block menu for it.
    // The new block is a text delegate, which owns openBlockMenu.
    function insertBlockBelowAndOpenMenu() {
        var newIndex = delegate.index + 1
        blockModel.insertBlock(newIndex, 0, "")
        var lv = listView
        Qt.callLater(function() {
            if (!lv)
                return
            lv.currentIndex = newIndex
            var item = lv.itemAtIndex(newIndex)
            if (item) {
                item.focusAtStart()
                if (item.openBlockMenu)
                    item.openBlockMenu("insert")
            }
        })
    }

    Item {
        id: focusTarget
        objectName: "dividerFocusItem"
        anchors.fill: parent
        activeFocusOnTab: true

        Keys.onPressed: function(event) {
            // Block-selection entry points match EditableBlock: a divider
            // has no text, so Ctrl+A goes straight to select-all-blocks.
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && (event.modifiers & Qt.ControlModifier)
                && (event.modifiers & Qt.ShiftModifier)) {
                if (delegate.listView)
                    delegate.listView.currentIndex = delegate.index
                documentSelection.selectBlock(delegate.index)
                delegate.focusSelectionHandler()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                documentSelection.selectAllBlocks()
                delegate.focusSelectionHandler()
                event.accepted = true
                return
            }
            // Ctrl+Shift+D deletes, Ctrl+D duplicates — §13.3 applies to
            // dividers like any block (Shift variant checked first)
            if (event.key === Qt.Key_D && (event.modifiers & Qt.ControlModifier)
                && (event.modifiers & Qt.ShiftModifier)) {
                delegate.deleteCurrentBlock()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_D && (event.modifiers & Qt.ControlModifier)) {
                blockModel.duplicateBlocks([delegate.index])
                var lv = delegate.listView
                var cloneIndex = delegate.index + 1
                Qt.callLater(function() {
                    if (!lv)
                        return
                    lv.currentIndex = cloneIndex
                    var item = lv.itemAtIndex(cloneIndex)
                    if (item)
                        item.focusAtStart()
                })
                event.accepted = true
                return
            }
            // Arrow navigation matches the text delegates
            if (event.key === Qt.Key_Up && delegate.index > 0 && delegate.listView) {
                var prevIndex = delegate.index - 1
                delegate.listView.currentIndex = prevIndex
                var prev = delegate.listView.itemAtIndex(prevIndex)
                if (prev) prev.focusAtEnd()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Down && delegate.index < blockModel.count - 1
                && delegate.listView) {
                var nextIndex = delegate.index + 1
                delegate.listView.currentIndex = nextIndex
                var next = delegate.listView.itemAtIndex(nextIndex)
                if (next) next.focusAtStart()
                event.accepted = true
                return
            }
            // A focused divider deletes on Backspace or Delete (§3.5)
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                delegate.deleteCurrentBlock()
                event.accepted = true
                return
            }
            // Enter continues writing below the rule
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                delegate.createBlockBelow()
                event.accepted = true
                return
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 24
        radius: 4
        opacity: delegate.isDragSource ? 0.35 : 1
        color: delegate.blockSelected ? theme.blockSelectionTint
             : delegate.isFocused ? theme.focusTint
             : (delegate.isHovered ? theme.blockHoverTint : "transparent")
        // A visible keyboard-focus ring (§14.1) in addition to the tint.
        border.color: delegate.blockSelected ? theme.accent
                    : delegate.isFocused ? theme.focusRing : "transparent"
        border.width: (delegate.blockSelected || delegate.isFocused) ? 2 : 0
    }

    // The rule itself, drawn per style/thickness/color/width (phase12 §1.2.9).
    Canvas {
        id: dividerCanvas
        objectName: "dividerLine"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 32
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        height: Math.max(delegate.divThickness,
                         delegate.divStyle === "decorative" ? 14 : delegate.divThickness)
        opacity: delegate.isDragSource ? 0.35 : 1

        // Any styling change (or focus recolor) repaints.
        readonly property string repaintKey: delegate.divStyle + "|"
            + delegate.divThickness + "|" + delegate.divColor + "|"
            + delegate.divWidthFraction
        onRepaintKeyChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            var t = delegate.divThickness
            var cy = height / 2
            var w = width * delegate.divWidthFraction
            var x0 = (width - w) / 2
            var x1 = x0 + w
            ctx.strokeStyle = delegate.divColor
            ctx.fillStyle = delegate.divColor
            ctx.lineWidth = t
            if (delegate.divStyle === "dashed") {
                ctx.setLineDash([t * 3, t * 2.2])
                ctx.lineCap = "butt"
                ctx.beginPath(); ctx.moveTo(x0, cy); ctx.lineTo(x1, cy); ctx.stroke()
            } else if (delegate.divStyle === "dotted") {
                ctx.setLineDash([0.1, t * 2])
                ctx.lineCap = "round"
                ctx.beginPath(); ctx.moveTo(x0, cy); ctx.lineTo(x1, cy); ctx.stroke()
            } else if (delegate.divStyle === "decorative") {
                // Two segments flanking a centered diamond motif.
                ctx.setLineDash([])
                ctx.lineCap = "round"
                var cx = width / 2
                var gap = 16
                ctx.beginPath(); ctx.moveTo(x0, cy); ctx.lineTo(cx - gap, cy); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(cx + gap, cy); ctx.lineTo(x1, cy); ctx.stroke()
                var d = 5
                ctx.beginPath()
                ctx.moveTo(cx, cy - d); ctx.lineTo(cx + d, cy)
                ctx.lineTo(cx, cy + d); ctx.lineTo(cx - d, cy)
                ctx.closePath(); ctx.fill()
            } else {
                ctx.setLineDash([])
                ctx.lineCap = "butt"
                ctx.beginPath(); ctx.moveTo(x0, cy); ctx.lineTo(x1, cy); ctx.stroke()
            }
        }
    }

    // Hover affordance to open the style picker (features.md §1.2.9).
    Rectangle {
        objectName: "dividerStyleButton"
        width: 18; height: 18
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        radius: 4
        color: dividerStyleArea.containsMouse ? theme.hoverTint : "transparent"
        opacity: delegate.isHovered || dividerStylePicker.visible ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Text {
            anchors.centerIn: parent
            text: "╌"
            color: theme.textMuted
            font.pixelSize: 13
        }
        MouseArea {
            id: dividerStyleArea
            anchors.fill: parent
            anchors.margins: -2
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: dividerStylePicker.open()
        }
    }

    DividerStylePicker {
        id: dividerStylePicker
        x: parent.width - width - 10
        y: 26
        currentStyle: delegate.divStyle
        currentThickness: delegate.divThickness
        currentColor: delegate.divColorAttr
        currentWidth: delegate.divWidthAttr
        onApplied: function(payload) { delegate.setDividerAttributes(payload) }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: function(mouse) {
            // The §3.1 modifier gestures work on dividers too; a divider
            // has no text, so there is no link carve-out here.
            if (mouse.modifiers & Qt.ControlModifier) {
                documentSelection.toggleBlock(delegate.index)
                if (documentSelection.hasBlockSelection)
                    delegate.focusSelectionHandler()
                else
                    focusTarget.forceActiveFocus()
                return
            }
            if (mouse.modifiers & Qt.ShiftModifier) {
                var win = Window.window
                var anchor = win && win.lastFocusedBlock !== undefined
                        ? win.lastFocusedBlock : -1
                if (!documentSelection.hasBlockSelection
                    && anchor >= 0 && anchor !== delegate.index)
                    documentSelection.selectBlock(anchor)
                documentSelection.extendBlockSelectionTo(delegate.index)
                delegate.focusSelectionHandler()
                return
            }
            if (documentSelection.hasBlockSelection
                || documentSelection.hasTextSelection)
                documentSelection.clear()
            focusTarget.forceActiveFocus()
        }
    }

    // Gutter plus-button (declared after hoverArea so it wins clicks)
    Rectangle {
        objectName: "plusButton"
        width: 18
        height: 18
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        radius: 4
        color: plusArea.containsMouse ? theme.hoverTint : "transparent"
        opacity: delegate.isHovered ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

        Text {
            anchors.centerIn: parent
            text: "+"
            color: theme.textMuted
            font.pixelSize: 14
            font.bold: true
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
    // Drag handle dots (§3.1/§3.2 apply to dividers too): click selects,
    // dragging past the threshold reorders — same gesture as
    // EditableBlock's handle.
    Item {
        objectName: "dividerHandle"
        width: 14
        height: 18
        x: 30
        anchors.verticalCenter: parent.verticalCenter
        // Pressed keeps it visible: hiding the item would cancel the
        // drag's mouse grab (see EditableBlock's gutter Row).
        opacity: delegate.isHovered || dividerHandleArea.pressed ? 0.6 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

        Column {
            anchors.centerIn: parent
            spacing: 2

            Repeater {
                model: 2

                Row {
                    spacing: 2

                    Repeater {
                        model: 2

                        Rectangle {
                            width: 3
                            height: 3
                            radius: 1.5
                            color: theme.textFaint
                        }
                    }
                }
            }
        }

        MouseArea {
            id: dividerHandleArea
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
                pressX = mouse.x
                pressY = mouse.y
                dragging = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                var win = Window.window
                if (!win || !win.blockDrag)
                    return
                var sp = dividerHandleArea.mapToItem(null, mouse.x, mouse.y)
                if (!dragging) {
                    if (Math.abs(mouse.x - pressX) < 5
                        && Math.abs(mouse.y - pressY) < 5)
                        return
                    dragging = true
                    win.blockDrag.begin(delegate.index, sp.x, sp.y)
                } else {
                    win.blockDrag.update(sp.x, sp.y)
                }
            }
            onReleased: {
                var win = Window.window
                if (dragging) {
                    dragging = false
                    if (win && win.blockDrag)
                        win.blockDrag.drop()
                    return
                }
                if (delegate.listView)
                    delegate.listView.currentIndex = delegate.index
                documentSelection.selectBlock(delegate.index)
                delegate.focusSelectionHandler()
            }
            onCanceled: {
                if (dragging) {
                    dragging = false
                    var win = Window.window
                    if (win && win.blockDrag)
                        win.blockDrag.cancel()
                }
            }
        }
    }
}
