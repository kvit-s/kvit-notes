// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Window
import Kvit 1.0

// Paragraphs and headings.
//
// Default scroll path: one wrapped Text plus a thin press handler. Gutter
// chrome loads on hover. The full EditableBlock is created only after the
// row is committed to an editor need (focus, search hit, drop cap, in-range
// text selection, or non-plain markup).
//
// Important: ListView rebinds model roles one property at a time on reuse.
// `displayText === content` can therefore flicker false for a moment even
// for plain rows. Editor Loader activation is latched through callLater so
// that transient mismatch does not instantiate EditableBlock on every scroll
// reuse (that path was multi-frame-budget expensive).
Item {
    id: root

    required property int index
    required property string blockId
    required property int blockType
    required property string content
    required property string displayText
    required property int indentLevel
    required property bool checked
    required property int ordinal
    required property string language
    required property string calloutTitle
    required property string attributes

    property int blockIndex: index
    property bool isPooled: false
    property bool editorRequested: false
    property bool editorLoaderActive: false
    // Actions requested before the editor Loader finished; drained in
    // order once it loads. A single slot is not enough — callers chain
    // requests (focusAtStart + openBlockMenu) before activation lands.
    property var pendingActions: []
    property real lastShellHeight: 0
    property bool shellHovered: false

    readonly property var appTheme: theme
    readonly property var appTypography: typography
    readonly property int contentFontSize: {
        // sizeForBlockType() is invokable C++; explicitly read baseSize so the
        // QML binding subscribes to typographyChanged and live size changes
        // re-evaluate already-instantiated delegates.
        var baseSize = typography.baseSize
        return typography.sizeForBlockType(root.blockType)
    }
    readonly property int contentFontWeight: {
        switch (root.blockType) {
            case Block.Heading1: return Font.Bold
            case Block.Heading2: return Font.DemiBold
            case Block.Heading3: return Font.Medium
            case Block.Heading4: return Font.Medium
            default: return Font.Normal
        }
    }
    readonly property string contentFontFamily: typography.fontFamily !== ""
        ? typography.fontFamily : Qt.application.font.family
    readonly property color contentColor: theme.textPrimary
    readonly property string blockAlign: {
        if (!root.attributes || root.attributes.length === 0)
            return "left"
        return blockAttributes.str(root.attributes, "align", "left")
    }
    readonly property int alignHAlign:
        root.blockAlign === "center" ? TextEdit.AlignHCenter
      : root.blockAlign === "right"  ? TextEdit.AlignRight
      : TextEdit.AlignLeft

    readonly property bool hasSearchMatches: {
        if (!documentSearch.active || documentSearch.matchCount === 0)
            return false
        var revision = documentSearch.revision
        return documentSearch.matchesForBlock(root.index).length > 0
    }
    readonly property bool hasDropCap:
        root.blockType === Block.Paragraph
        && !!root.attributes
        && root.attributes.indexOf("dropcap") >= 0
        && blockAttributes.num(root.attributes, "dropcap", 0) >= 2
    // Only rows inside the active text range need the editor for portion paint.
    readonly property bool inTextSelectionRange: {
        if (!documentSelection.hasTextSelection)
            return false
        var revision = documentSelection.revision
        var portion = documentSelection.portionForBlock(root.index)
        return !!(portion && portion.selected)
    }
    readonly property bool useReadOnlyShell:
        !root.editorRequested
        && !root.inTextSelectionRange
        && !root.hasSearchMatches
        && !root.hasDropCap
        && root.displayText === root.content

    readonly property var editable: editorLoader.item
    readonly property bool editorActive: editable ? !!editable.editorActive : false
    readonly property bool isFocused: editable ? !!editable.isFocused : false
    // Same hover contract as the other block delegates: the shell tracks
    // its own hover; the latched editor reports its own.
    readonly property bool isHovered: editorLoaderActive
        ? (editable ? !!editable.isHovered : false)
        : shellHovered
    readonly property var editorEngine: editable ? editable.editorEngine : null
    readonly property int cursorFormatFlags: editable ? editable.cursorFormatFlags : 0
    readonly property string currentColor: editable ? editable.currentColor : ""
    readonly property var cursorLineColumn:
        editable && editable.cursorLineColumn !== undefined
        ? editable.cursorLineColumn : { line: 1, column: 1 }
    readonly property int selectionStartDoc: editable ? editable.selectionStartDoc : 0
    readonly property int selectionEndDoc: editable ? editable.selectionEndDoc : 0
    readonly property string selectedDisplayText:
        editable ? editable.selectedDisplayText : ""
    readonly property var inlineMathBoxes:
        editable && editable.inlineMathBoxes !== undefined
        ? editable.inlineMathBoxes : []
    readonly property int inlineMathVerticalPadding:
        editable && editable.inlineMathVerticalPadding !== undefined
        ? editable.inlineMathVerticalPadding : 0
    readonly property int inlineMathPixelSize:
        editable && editable.inlineMathPixelSize !== undefined
        ? editable.inlineMathPixelSize : contentFontSize
    readonly property real typewriterDim: {
        if (editable && editable.typewriterDim !== undefined)
            return editable.typewriterDim
        var win = Window.window
        if (win && win.typewriterMode !== undefined && win.typewriterMode
                && win.caretBlockIndex >= 0 && win.caretBlockIndex !== root.index)
            return 0.32
        return 1.0
    }

    readonly property bool blockSelected: {
        if (!documentSelection.hasBlockSelection)
            return false
        var revision = documentSelection.revision
        return documentSelection.isBlockSelected(root.index)
    }
    readonly property bool isDragSource: {
        var win = Window.window
        if (!win || !win.blockDrag || !win.blockDrag.active)
            return false
        return win.blockDrag.isMulti ? root.blockSelected
                                     : win.blockDrag.sourceIndex === root.index
    }

    // Shell geometry while the editor is not latched; editor height after load.
    implicitHeight: !root.editorLoaderActive
        ? Math.max(readOnlyText.implicitHeight + 28, 28)
        : (editable ? editable.implicitHeight
                    : (root.lastShellHeight > 0 ? root.lastShellHeight
                                               : Math.max(readOnlyText.implicitHeight + 28, 28)))

    onImplicitHeightChanged: {
        if (!root.editorLoaderActive && implicitHeight > 0)
            root.lastShellHeight = implicitHeight
    }

    function syncEditorLoader() {
        if (root.useReadOnlyShell) {
            root.editorLoaderActive = false
            return
        }
        // Defer activation so a one-property-at-a-time ListView rebind that
        // briefly breaks displayText === content does not build EditableBlock.
        Qt.callLater(function() {
            if (!root.useReadOnlyShell)
                root.editorLoaderActive = true
        })
    }

    onUseReadOnlyShellChanged: root.syncEditorLoader()
    Component.onCompleted: root.syncEditorLoader()

    function editableItem() {
        return root.editorLoaderActive ? root.editable : null
    }

    function runPending() {
        var item = editableItem()
        if (!item || root.pendingActions.length === 0)
            return
        var queue = root.pendingActions
        root.pendingActions = []
        for (var i = 0; i < queue.length; ++i) {
            var fn = item[queue[i].action]
            if (typeof fn === "function")
                fn.apply(item, queue[i].args || [])
        }
    }

    function promote(action, args) {
        root.editorRequested = true
        if (action && action.length > 0) {
            var queue = root.pendingActions
            queue.push({ action: action, args: args || [] })
            root.pendingActions = queue
        }
        root.syncEditorLoader()
        // A ready loader can drain immediately. A newly activated loader is
        // handled by onLoaded below. Avoid leaving a callLater callback tied
        // to a delegate that may be replaced by a block-type conversion.
        if (root.editorLoaderActive && root.editable)
            root.runPending()
    }

    function forward(action, args) {
        var item = editableItem()
        if (item && typeof item[action] === "function")
            return item[action].apply(item, args || [])
        promote(action, args)
        return undefined
    }

    function focusSelectionHandler() {
        var win = Window.window
        if (win && win.selectionKeyHandler)
            win.selectionKeyHandler.forceActiveFocus()
    }

    function activateEditor() { promote("", []) }
    function focusAtStart() { forward("focusAtStart", []) }
    function focusAtEnd() { forward("focusAtEnd", []) }
    function focusAtPosition(markdownPos) { forward("focusAtPosition", [markdownPos]) }
    function markdownPositionAt(sceneX, sceneY) {
        var item = editableItem()
        if (item && item.markdownPositionAt)
            return item.markdownPositionAt(sceneX, sceneY)
        var p = readOnlyText.mapFromItem(null, sceneX, sceneY)
        if (typeof readOnlyText.positionAt === "function")
            return Math.max(0, Math.min(root.content.length,
                                        readOnlyText.positionAt(p.x, p.y)))
        return p.x < readOnlyText.width / 2 ? 0 : root.content.length
    }
    function pointInText(sceneX, sceneY) {
        var item = editableItem()
        if (item && item.pointInText)
            return item.pointInText(sceneX, sceneY)
        var p = readOnlyText.mapFromItem(null, sceneX, sceneY)
        return p.x >= 0 && p.x <= readOnlyText.width
            && p.y >= 0 && p.y <= readOnlyText.height
    }
    function lineStepPosition(mdPos, dir) {
        var item = editableItem()
        return item && item.lineStepPosition ? item.lineStepPosition(mdPos, dir) : -1
    }
    function entryPositionAtX(x, fromTop) {
        var item = editableItem()
        return item && item.entryPositionAtX
            ? item.entryPositionAtX(x, fromTop)
            : (fromTop ? 0 : root.content.length)
    }
    function xAtMarkdown(mdPos) {
        var item = editableItem()
        return item && item.xAtMarkdown ? item.xAtMarkdown(mdPos) : 0
    }
    function markdownCursor() {
        var item = editableItem()
        return item && item.markdownCursor ? item.markdownCursor() : 0
    }
    function selectionDisplayText() {
        var item = editableItem()
        return item && item.selectionDisplayText ? item.selectionDisplayText() : ""
    }
    function rectForMarkdownPosition(mdPos) {
        var item = editableItem()
        if (item && item.rectForMarkdownPosition)
            return item.rectForMarkdownPosition(mdPos)
        return Qt.rect(52, 6, Math.max(1, root.width - 72),
                       Math.max(1, root.implicitHeight - 12))
    }
    function selectionRectangle() {
        var item = editableItem()
        return item && item.selectionRectangle
            ? item.selectionRectangle() : Qt.rect(0, 0, 0, 0)
    }
    function cutSelection() { forward("cutSelection", []) }
    function copySelection() { forward("copySelection", []) }
    function pasteClipboard(plain) { forward("pasteClipboard", [plain]) }
    function selectAllText() { forward("selectAllText", []) }
    function toggleSpanType(typeName) { forward("toggleSpanType", [typeName]) }
    function applyColor(value) { forward("applyColor", [value]) }
    function removeColor() { forward("removeColor", []) }
    function openLinkUnderCursor() { forward("openLinkUnderCursor", []) }
    function removeLinkAtCursor() { forward("removeLinkAtCursor", []) }
    function openLinkDialog() { forward("openLinkDialog", []) }
    function openBlockMenu(mode) { forward("openBlockMenu", [mode]) }
    function setBlockAlignment(value) {
        var item = editableItem()
        if (item && item.setBlockAlignment)
            item.setBlockAlignment(value)
        else {
            var next = (value === "left" || value === "")
                ? blockAttributes.without(root.attributes, "align")
                : blockAttributes.withValue(root.attributes, "align", value)
            blockModel.setBlockAttributes(root.index, next)
        }
    }
    function setDropCap(lines) { forward("setDropCap", [lines]) }
    function insertImageBlock(storedPath) {
        forward("insertImageBlock", [storedPath])
    }
    function convertBlockType(newType) {
        var item = editableItem()
        if (item && item.convertBlockType) {
            item.convertBlockType(newType)
            return
        }
        var names = ["Paragraph", "Heading 1", "Heading 2", "Heading 3",
            "Bulleted list", "Numbered list", "To-do", "Quote", "Code block",
            "Divider", "Heading 4", "Image", "Callout", "Math block", "Media",
            "Table"]
        if (typeof a11y !== "undefined" && names[newType])
            a11y.announceConversion(names[newType])
        var lang = newType === Block.Callout ? "info" : ""
        blockModel.convertBlock(root.index, newType, root.content, false, lang)
    }
    function insertBlockBelowAndOpenMenu() {
        var newIndex = root.index + 1
        blockModel.insertBlock(newIndex, 0, "")
        var lv = ListView.view
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

    ListView.onPooled: {
        root.isPooled = true
        root.editorRequested = false
        root.editorLoaderActive = false
        root.pendingActions = []
        root.shellHovered = false
        if (editable && editable.textArea)
            editable.textArea.focus = false
        root.opacity = 0
    }
    ListView.onReused: {
        root.isPooled = false
        root.editorRequested = false
        root.editorLoaderActive = false
        root.pendingActions = []
        root.shellHovered = false
        root.opacity = 1
        root.syncEditorLoader()
        if (root.inTextSelectionRange
            && editable && editable.applyTextPortionLater)
            editable.applyTextPortionLater()
    }

    // ---- Default scroll path: one Text + press routing ----
    Text {
        id: readOnlyText
        objectName: "readOnlyText"
        // Stay painted until the editor is actually latched so rebind flicker
        // does not blank the row or thrash text layout.
        visible: !root.editorLoaderActive
        x: 57 + root.indentLevel * 24
        y: 10
        width: Math.max(1, root.width - x - 14)
        text: root.editorLoaderActive ? "" : root.displayText
        color: root.contentColor
        font.pixelSize: root.contentFontSize
        font.weight: root.contentFontWeight
        font.family: root.contentFontFamily
        font.kerning: false
        font.preferShaping: false
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        horizontalAlignment: root.alignHAlign
        lineHeight: root.appTypography.lineHeight
        lineHeightMode: Text.ProportionalHeight
        opacity: root.isDragSource ? 0.35 : 1
    }

    Rectangle {
        objectName: "selectionBackground"
        visible: !root.editorLoaderActive && root.blockSelected
        anchors.fill: parent
        anchors.leftMargin: 44 + root.indentLevel * 24
        radius: 4
        color: theme.blockSelectionTint
        border.color: theme.accent
        border.width: 1
        z: -1
    }

    Rectangle {
        visible: !root.editorLoaderActive && root.shellHovered && !root.blockSelected
        anchors.fill: parent
        anchors.leftMargin: 44 + root.indentLevel * 24
        radius: 4
        color: theme.blockHoverTint
        z: -1
    }

    Rectangle {
        objectName: "focusIndicator"
        visible: !root.editorLoaderActive
        anchors.left: parent.left
        anchors.leftMargin: 40 + root.indentLevel * 24
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        color: root.isFocused ? theme.focusRing : "transparent"
    }

    HoverHandler {
        enabled: !root.editorLoaderActive
        onHoveredChanged: root.shellHovered = hovered
    }

    Loader {
        id: gutterLoader
        active: !root.editorLoaderActive && root.shellHovered
        x: root.indentLevel * 24
        y: 4
        width: 40
        height: 24
        sourceComponent: gutterComponent
    }

    MouseArea {
        anchors.fill: parent
        enabled: !root.editorLoaderActive
        acceptedButtons: Qt.LeftButton
        propagateComposedEvents: true
        onPressed: function(mouse) {
            var ctrl = mouse.modifiers & Qt.ControlModifier
            var shift = mouse.modifiers & Qt.ShiftModifier
            if (ctrl && !shift) {
                documentSelection.toggleBlock(root.index)
                if (documentSelection.hasBlockSelection)
                    root.focusSelectionHandler()
                else
                    root.focusAtPosition(0)
                mouse.accepted = true
                return
            }
            if (shift && !ctrl) {
                if (!documentSelection.hasBlockSelection) {
                    var win = Window.window
                    var anchor = win && win.lastFocusedBlock !== undefined
                            ? win.lastFocusedBlock : -1
                    if (anchor >= 0 && anchor !== root.index)
                        documentSelection.selectBlock(anchor)
                }
                documentSelection.extendBlockSelectionTo(root.index)
                root.focusSelectionHandler()
                mouse.accepted = true
                return
            }
            var gutterWidth = 44 + root.indentLevel * 24
            if (mouse.x < gutterWidth) {
                if ((documentSelection.hasBlockSelection
                     || documentSelection.hasTextSelection)
                    && !documentSelection.isBlockSelected(root.index))
                    documentSelection.clear()
                mouse.accepted = false
                return
            }
            if ((documentSelection.hasBlockSelection
                 || documentSelection.hasTextSelection)
                && !documentSelection.isBlockSelected(root.index))
                documentSelection.clear()

            var localX = mouse.x - readOnlyText.x
            var localY = mouse.y - readOnlyText.y
            var pos = 0
            if (typeof readOnlyText.positionAt === "function")
                pos = Math.max(0, Math.min(root.content.length,
                    readOnlyText.positionAt(localX, localY)))
            else
                pos = localX < readOnlyText.width / 2 ? 0 : root.content.length
            root.focusAtPosition(pos)
            mouse.accepted = true
        }
    }

    Component {
        id: gutterComponent
        Item {
            width: 40
            height: 24
            Row {
                objectName: "gutterButtons"
                anchors.centerIn: parent
                spacing: 4

                Rectangle {
                    objectName: "plusButton"
                    width: 18
                    height: 18
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 4
                    color: plusArea.containsMouse ? theme.hoverTint : "transparent"
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
                        onClicked: root.insertBlockBelowAndOpenMenu()
                    }
                }

                Item {
                    width: 14
                    height: 18
                    anchors.verticalCenter: parent.verticalCenter
                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        opacity: 0.6
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
                        id: handleArea
                        objectName: "dragHandle"
                        anchors.fill: parent
                        anchors.margins: -2
                        hoverEnabled: true
                        cursorShape: Qt.OpenHandCursor
                        preventStealing: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        property real pressX: 0
                        property real pressY: 0
                        property bool dragging: false
                        onPressed: function(mouse) {
                            if (mouse.button === Qt.RightButton) {
                                var win = Window.window
                                if (win && win.openBlockHandleMenu)
                                    win.openBlockHandleMenu(root)
                                return
                            }
                            pressX = mouse.x
                            pressY = mouse.y
                            dragging = false
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed || (pressedButtons & Qt.RightButton))
                                return
                            var win = Window.window
                            if (!win || !win.blockDrag)
                                return
                            var sp = handleArea.mapToItem(null, mouse.x, mouse.y)
                            if (!dragging) {
                                if (Math.abs(mouse.x - pressX) < 5
                                    && Math.abs(mouse.y - pressY) < 5)
                                    return
                                dragging = true
                                win.blockDrag.begin(root.index, sp.x, sp.y)
                            } else {
                                win.blockDrag.update(sp.x, sp.y)
                            }
                        }
                        onReleased: function(mouse) {
                            if (mouse.button === Qt.RightButton)
                                return
                            var win = Window.window
                            if (dragging) {
                                dragging = false
                                if (win && win.blockDrag)
                                    win.blockDrag.drop()
                                return
                            }
                            if (ListView.view)
                                ListView.view.currentIndex = root.index
                            documentSelection.selectBlock(root.index)
                            root.focusSelectionHandler()
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
        }
    }

    // ---- Full editor only after a committed editor need ----
    Loader {
        id: editorLoader
        width: root.width
        active: root.editorLoaderActive
        visible: active && item
        sourceComponent: editableComponent
        onLoaded: root.runPending()
    }

    // Deliberately no demote-on-blur: destroying the editor the moment
    // focus leaves would invalidate live references (link dialog and
    // toolbar callbacks, Ctrl+Up/Down hops) and churn delegates on every
    // focus move. A promoted row returns to the shell when the ListView
    // pools or reuses it; rows promoted by derived needs (search hits,
    // in-range selection) demote through useReadOnlyShell when the need
    // passes.

    Component {
        id: editableComponent
        EditableBlock {
            width: root.width
            index: root.index
            blockId: root.blockId
            blockType: root.blockType
            content: root.content
            displayText: root.displayText
            indentLevel: root.indentLevel
            checked: root.checked
            ordinal: root.ordinal
            language: root.language || ""
            calloutTitle: root.calloutTitle || ""
            attributes: root.attributes || ""
            isPooled: root.isPooled
            enableLightweightReadOnly: false
            // Inside the Loader the ListView.view attached property no
            // longer resolves (it attaches to the delegate root); pass
            // the view down so refocusBlock and scroll reveals work.
            listView: root.ListView.view

            contentFontSize: root.contentFontSize
            contentFontWeight: root.contentFontWeight
            placeholder: root.blockType === Block.Paragraph ? qsTr("Type something...") : ""
        }
    }
}
