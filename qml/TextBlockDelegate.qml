// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The gutter Loader's Component and the drag handler are separate
// scopes reading the delegate root and the MouseArea by id.
pragma ComponentBehavior: Bound

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
BlockDelegateBase {
    id: root

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
    // See EditableBlock: the kind answers the type scale and whether the
    // alignment buttons apply.
    required property int fontRole
    required property bool isAlignable

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

    readonly property var appTheme: Theme
    readonly property var appTypography: Typography
    readonly property int contentFontSize: {
        // sizeForRole() is invokable C++; explicitly read baseSize so the
        // QML binding subscribes to typographyChanged and live size changes
        // re-evaluate already-instantiated delegates.
        var baseSize = Typography.baseSize
        return Typography.sizeForRole(root.fontRole)
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
    function defaultFontFamily() {
        // Qt.application.font is documented API the type description for
        // QQmlApplication omits, the same gap as Qt.application.screens.
        // qmllint disable missing-property
        return Qt.application.font.family
        // qmllint enable missing-property
    }
    readonly property string contentFontFamily: Typography.fontFamily !== ""
        ? Typography.fontFamily : root.defaultFontFamily()
    readonly property color contentColor: Theme.textPrimary
    readonly property string blockAlign: {
        if (!root.attributes || root.attributes.length === 0)
            return "left"
        return BlockAttributes.str(root.attributes, "align", "left")
    }
    readonly property int alignHAlign:
        root.blockAlign === "center" ? TextEdit.AlignHCenter
      : root.blockAlign === "right"  ? TextEdit.AlignRight
      : TextEdit.AlignLeft

    readonly property bool hasSearchMatches: {
        if (!root.search.active || root.search.matchCount === 0)
            return false
        var revision = root.search.revision
        return root.search.matchesForBlock(root.index).length > 0
    }
    readonly property bool hasDropCap:
        root.blockType === Block.Paragraph
        && !!root.attributes
        && root.attributes.indexOf("dropcap") >= 0
        && BlockAttributes.num(root.attributes, "dropcap", 0) >= 2
    // Only rows inside the active text range need the editor for portion paint.
    readonly property bool inTextSelectionRange: {
        if (!root.selection.hasTextSelection)
            return false
        var revision = root.selection.revision
        var portion = root.selection.portionForBlock(root.index)
        return !!(portion && portion.selected)
    }
    // A block a module has marked promotes to the editor, the same way a
    // block with a search match does: the wash is painted by the editing
    // engine's highlighter, which the lightweight shell below does not have.
    // The gate is one bool in the open build, where nothing is registered.
    readonly property bool hasDecorationSpans: {
        if (!root.decorations.hasSpans)
            return false
        var revision = root.decorations.revision
        return root.decorations.spansForBlock(root.index).length > 0
    }
    readonly property bool useReadOnlyShell:
        !root.editorRequested
        && !root.inTextSelectionRange
        && !root.hasSearchMatches
        && !root.hasDecorationSpans
        && !root.hasDropCap
        && root.displayText === root.content

    readonly property var editable: editorLoader.item
    readonly property bool editorActive: editable ? !!editable.editorActive : false
    isFocused: editable ? !!editable.isFocused : false
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
        if (root.editor && root.editor.typewriterMode !== undefined && root.editor.typewriterMode
                && root.editor.caretBlockIndex >= 0 && root.editor.caretBlockIndex !== root.index)
            return 0.32
        return 1.0
    }

    readonly property bool blockSelected: {
        if (!root.selection.hasBlockSelection)
            return false
        var revision = root.selection.revision
        return root.selection.isBlockSelected(root.index)
    }
    readonly property bool isDragSource: {
        if (!root.editor || !root.editor.blockDrag || !root.editor.blockDrag.active)
            return false
        return root.editor.blockDrag.isMulti ? root.blockSelected
                                     : root.editor.blockDrag.sourceIndex === root.index
    }

    // Shell geometry while the editor is not latched; editor height after load.
    blockContentHeight: !root.editorLoaderActive
        ? Math.max(readOnlyText.implicitHeight + 28, 28)
        : (editable ? editable.blockContentHeight
                    : (root.lastShellHeight > 0 ? root.lastShellHeight
                                               : Math.max(readOnlyText.implicitHeight + 28, 28)))

    // Per-line geometry (see BlockDelegateBase): the lightweight Text shell
    // answers while the row is unlatched, and the editor answers once it is,
    // so a margin glyph stays beside its line across the promotion.
    textLineOrigin: root.editorLoaderActive && root.editable
        ? root.editable.textLineOrigin : readOnlyText.y
    textLineHeight: {
        if (root.editorLoaderActive && root.editable)
            return root.editable.textLineHeight
        return readOnlyText.lineCount > 0
            ? readOnlyText.contentHeight / readOnlyText.lineCount : 0
    }
    textLineCount: root.editorLoaderActive && root.editable
        ? root.editable.textLineCount : readOnlyText.lineCount

    // The remembered height is the row's own content, decorations excluded:
    // it is what the latched editor falls back to before its own document
    // has a height, and a container's height added into it there would be
    // counted twice.
    onBlockContentHeightChanged: {
        if (!root.editorLoaderActive && root.blockContentHeight > 0)
            root.lastShellHeight = root.blockContentHeight
    }

    function syncEditorLoader() {
        if (root.useReadOnlyShell) {
            root.editorLoaderActive = false
            return
        }
        // Defer activation so a one-property-at-a-time ListView rebind that
        // briefly breaks displayText === content does not build EditableBlock.
        editorLatchTimer.restart()
    }
    // A Timer the row owns rather than a queued call, because the row can go
    // before the turn ends and a queued call cannot be cancelled. Selecting
    // text across a document promotes every row it covers and clearing the
    // selection demotes them again, so the rows tear down in bursts; with a
    // queued call each one left behind a callback that ran against a context
    // that no longer existed, and a page of "attempted to evaluate a function
    // in an invalid context" with it. Destroying the row stops the Timer.
    Timer {
        id: editorLatchTimer
        interval: 0
        onTriggered: {
            if (!root.useReadOnlyShell)
                root.editorLoaderActive = true
        }
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

    function activateEditor() { promote("", []) }
    function focusAtStart() { forward("focusAtStart", []) }
    function focusAtEnd() { forward("focusAtEnd", []) }
    function focusAtPosition(markdownPos) { forward("focusAtPosition", [markdownPos]) }
    function focusAtScenePosition(sceneX, sceneY) {
        forward("focusAtScenePosition", [sceneX, sceneY])
    }
    function markdownPositionAt(sceneX, sceneY) {
        var item = editableItem()
        if (item && item.markdownPositionAt)
            return item.markdownPositionAt(sceneX, sceneY)
        var p = readOnlyText.mapFromItem(null, sceneX, sceneY)
        // qmllint disable missing-property
        if (typeof readOnlyText.positionAt === "function")
            return Math.max(0, Math.min(root.content.length,
                                        readOnlyText.positionAt(p.x, p.y)))
        // qmllint enable missing-property
        return p.x < readOnlyText.width / 2 ? 0 : root.content.length
    }
    // The marked runs live in the editor's laid-out text, so this row can
    // only answer once it is latched — which a marked block always is, since
    // carrying a span is what disqualifies it from the shell above.
    function decorationSpanRects(id) {
        var item = editableItem()
        return item ? item.decorationSpanRects(id) : []
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
    // Never promotes: only a row whose editor is already up can have been the
    // block a text drag started in.
    function reapplySelectionPortion() {
        var item = editableItem()
        if (item && item.reapplySelectionPortion)
            item.reapplySelectionPortion()
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
    function typeText(text) { forward("typeText", [text]) }
    function setBlockAlignment(value) {
        var item = editableItem()
        if (item && item.setBlockAlignment)
            item.setBlockAlignment(value)
        else {
            var next = (value === "left" || value === "")
                ? BlockAttributes.without(root.attributes, "align")
                : BlockAttributes.withValue(root.attributes, "align", value)
            root.blocks.setBlockAttributes(root.index, next)
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
        if (typeof A11y !== "undefined" && names[newType])
            A11y.announceConversion(names[newType])
        var lang = newType === Block.Callout ? "info" : ""
        root.blocks.convertBlock(root.index, newType, root.content, false, lang)
    }
    function insertBlockBelowAndOpenMenu() {
        var newIndex = root.index + 1
        root.blocks.insertBlock(newIndex, 0, "")
        var lv = ListView.view
        Qt.callLater(function() {
            if (!lv)
                return
            lv.currentIndex = newIndex
            var item = (lv.itemAtIndex(newIndex) as BlockDelegateBase)
            if (item) {
                item.focusAtStart()
                if (item.openBlockMenu)
                    item.openBlockMenu("insert")
            }
        })
    }

    // Delete this row (the gutter delete-button). Undoable via Ctrl+Z. Mirrors
    // EditableBlock.deleteCurrentBlock: focus falls back to the previous block.
    function deleteCurrentBlock() {
        var prevIndex = root.index - 1
        var lv = ListView.view
        root.blocks.removeBlock(root.index)
        Qt.callLater(function() {
            if (lv && prevIndex >= 0) {
                lv.currentIndex = prevIndex
                var item = (lv.itemAtIndex(prevIndex) as BlockDelegateBase)
                if (item) item.focusAtEnd()
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
        x: root.gutterInset + 13 + root.indentLevel * 24
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
        anchors.leftMargin: root.gutterInset + root.indentLevel * 24
        radius: 4
        color: Theme.blockSelectionTint
        border.color: Theme.accent
        border.width: 1
        z: -1
    }

    Rectangle {
        visible: !root.editorLoaderActive && root.shellHovered && !root.blockSelected
        anchors.fill: parent
        anchors.leftMargin: root.gutterInset + root.indentLevel * 24
        radius: 4
        color: Theme.blockHoverTint
        z: -1
    }

    Rectangle {
        objectName: "focusIndicator"
        visible: !root.editorLoaderActive
        anchors.left: parent.left
        anchors.leftMargin: (root.gutterShown ? 40 : 0) + root.indentLevel * 24
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        color: root.isFocused ? Theme.focusRing : "transparent"
    }

    HoverHandler {
        enabled: !root.editorLoaderActive
        onHoveredChanged: root.shellHovered = hovered
    }

    Loader {
        id: gutterLoader
        active: !root.editorLoaderActive && root.shellHovered && root.gutterShown
        x: root.indentLevel * 24
        y: 4
        width: 40
        height: 44
        sourceComponent: gutterComponent
    }

    MouseArea {
        anchors.fill: parent
        enabled: !root.editorLoaderActive
        acceptedButtons: Qt.LeftButton
        propagateComposedEvents: true
        onPressed: function(mouse) {
            // Ctrl+click and Shift+click build a BLOCK selection, which is a
            // mode with commands in it. A read-only row answers the plain
            // press below instead, which places the caret and lets the
            // pointer sweep text across the document.
            var ctrl = !root.readOnly && (mouse.modifiers & Qt.ControlModifier)
            var shift = !root.readOnly && (mouse.modifiers & Qt.ShiftModifier)
            if (ctrl && !shift) {
                root.selection.toggleBlock(root.index)
                if (root.selection.hasBlockSelection)
                    root.focusSelectionHandler()
                else
                    root.focusAtPosition(0)
                mouse.accepted = true
                return
            }
            if (shift && !ctrl) {
                if (!root.selection.hasBlockSelection) {
                    var anchor = root.editor && root.editor.lastFocusedBlock !== undefined
                            ? root.editor.lastFocusedBlock : -1
                    if (anchor >= 0 && anchor !== root.index)
                        root.selection.selectBlock(anchor)
                }
                root.selection.extendBlockSelectionTo(root.index)
                root.focusSelectionHandler()
                mouse.accepted = true
                return
            }
            var gutterWidth = root.gutterInset + root.indentLevel * 24
            if (mouse.x < gutterWidth) {
                if ((root.selection.hasBlockSelection
                     || root.selection.hasTextSelection)
                    && !root.selection.isBlockSelected(root.index))
                    root.selection.clear()
                mouse.accepted = false
                return
            }
            if ((root.selection.hasBlockSelection
                 || root.selection.hasTextSelection)
                && !root.selection.isBlockSelected(root.index))
                root.selection.clear()

            // QML Text has no positionAt(): the old fallback split the whole
            // row in half, so nearly every click on a short line resolved to
            // position zero. Preserve the actual point while the Loader swaps
            // this lightweight shell for the TextArea; the editor hit-tests
            // it once its document is ready.
            var scenePoint = root.mapToItem(null, mouse.x, mouse.y)
            root.focusAtScenePosition(scenePoint.x, scenePoint.y)
            mouse.accepted = true
        }
    }

    Component {
        id: gutterComponent
        // The same gutter every other delegate uses. The Loader above is
        // already gated on hover, so the strip is only built for the row the
        // pointer is on.
        BlockGutter {
            rowHovered: root.shellHovered
            dragEnabled: root.editor !== null && root.editor.blockDrag !== null

            onInsertRequested: root.insertBlockBelowAndOpenMenu()
            onDeleteRequested: root.deleteCurrentBlock()
            onHandleMenuRequested: AppActions.requestBlockHandleMenu(root)
            onBlockSelectRequested: {
                if (root.ListView.view)
                    root.ListView.view.currentIndex = root.index
                root.selection.selectBlock(root.index)
                root.focusSelectionHandler()
            }
            onDragStarted: function(sceneX, sceneY) {
                root.editor.blockDrag.begin(root.index, sceneX, sceneY)
            }
            onDragMoved: function(sceneX, sceneY) {
                root.editor.blockDrag.update(sceneX, sceneY)
            }
            onDragDropped: {
                if (root.editor && root.editor.blockDrag)
                    root.editor.blockDrag.drop()
            }
            onDragCanceled: {
                if (root.editor && root.editor.blockDrag)
                    root.editor.blockDrag.cancel()
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
            fontRole: root.fontRole
            isAlignable: root.isAlignable
            isPooled: root.isPooled
            enableLightweightReadOnly: false
            // Inside the Loader the ListView.view attached property no
            // longer resolves (it attaches to the delegate root); pass
            // the view down so refocusBlock and scroll reveals work.
            listView: root.ListView.view

            contentFontSize: root.contentFontSize
            contentFontWeight: root.contentFontWeight
            placeholder: root.blockType === Block.Paragraph && root.editor
                         ? root.editor.paragraphPlaceholder : ""
        }
    }
}
