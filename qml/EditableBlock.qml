// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Kvit 1.0

// The shared editing core every text-carrying block delegate wraps: the
// TextArea + hybrid editing engine, key handling, clipboard, focus API,
// hover/handle/focus-indicator chrome, and indentation margin. Per-type
// delegates (TextBlockDelegate, BulletListDelegate, ...) instantiate this
// with their leading chrome (bullet glyph, ordinal label, checkbox, quote
// bar) and styling.
Item {
    id: delegate

    required property int index
    required property string blockId
    required property int blockType
    required property string content
    required property string displayText
    required property int indentLevel
    required property bool checked
    required property int ordinal
    // Code-block language (empty for every other type). The engine consults
    // it only in verbatim mode; the code chrome edits it.
    required property string language
    // Callout title (empty for every other type). A callout reuses `language`
    // for its type and `checked` for its fold state.
    required property string calloutTitle
    // Per-block presentation attributes: the payload of
    // the block's <!--kvit ...--> tag. Read for text alignment (paragraph /
    // heading) and a callout's custom color; edited through setBlockAlignment.
    required property string attributes

    property int blockIndex: index
    property bool isPooled: false

    // ---- Extension points for per-type delegates ----
    // Chrome rendered between the focus indicator and the text (bullet,
    // number, checkbox, quote bar). Instantiated in a Loader spanning the
    // block's height; the component sets its own implicitWidth.
    property Component leadingChrome: null
    // Chrome rendered BELOW the content and contributing to block height: a
    // quote's attribution line, for example. Anchored to the
    // content column; the delegate's component positions itself within it.
    property Component trailingChrome: null
    // The theme/typography context properties, re-exposed so the engine
    // bindings below can reach them past the engine's own property names.
    readonly property var appTheme: theme
    readonly property var appTypography: typography
    property int contentFontSize: typography.baseSize
    property int contentFontWeight: Font.Normal
    property string contentFontFamily: typography.fontFamily !== ""
        ? typography.fontFamily : Qt.application.font.family
    property color contentColor: theme.textPrimary
    property bool contentStrikeout: false
    // Code blocks: the engine maps 1:1 and nothing parses or reveals;
    // formatting/link commands are disabled (markers would be literal).
    property bool verbatimEditing: false
    // Code blocks: Enter inserts a newline into the block; Enter on a
    // trailing empty line exits.
    property bool enterInsertsNewline: false

    // The types whose structure the keyboard operates on (Enter
    // continuation, Tab nesting, the Backspace ladder).
    readonly property bool isListFamily: blockType === Block.BulletList
                                      || blockType === Block.NumberedList
                                      || blockType === Block.Todo
    readonly property bool isStructural: isListFamily || blockType === Block.Quote
    // Code blocks: a light panel behind the text
    property bool showPanel: false
    property string placeholder: ""

    // ---- Trailing chrome ----
    // A suffix of `content` that is rendered as chrome, not editable text: a
    // todo's metadata tail (📅 date / priority emoji) shown as chips, or a
    // quote's attribution line. Set by the per-type delegate. The engine
    // edits only the leading portion; edits re-append the tail. Empty for
    // every other block, so their behavior is unchanged.
    property string metaTail: ""
    readonly property string editableMarkdown: {
        var t = delegate.metaTail
        return (t !== "" && content.length >= t.length
                && content.slice(content.length - t.length) === t)
            ? content.slice(0, content.length - t.length) : content
    }
    // Scroll slice: simple paragraph/heading rows can render from
    // the model's cached display text until they need the editing engine.
    property bool enableLightweightReadOnly: false
    property bool editorRequested: false
    readonly property var blockSearchMatches: {
        var revision = documentSearch.revision // dependency only
        return documentSearch.matchesForBlock(delegate.index)
    }
    readonly property bool hasSearchMatches: blockSearchMatches.length > 0
    readonly property bool useReadOnlyText:
        enableLightweightReadOnly
        && !delegate.editorRequested
        && !textArea.activeFocus
        && !documentSelection.hasTextSelection
        && !delegate.hasSearchMatches
        && !delegate.hasDropCap
        && delegate.displayText === delegate.editableMarkdown
    readonly property bool editorActive: !delegate.useReadOnlyText
    readonly property real textAreaX: delegate.codeChrome
        ? (delegate.codeContentLeft - delegate.codeHScroll)
        : (delegate.calloutMode ? 14 : 0) + delegate.dropCapWidth
    readonly property real textAreaWidth: delegate.codeChrome
        ? Math.max(delegate.codeViewportWidth,
                   Math.ceil(textArea.contentWidth) + 16)
        : (delegate.calloutMode ? contentArea.width - 24
                                : contentArea.width) - delegate.dropCapWidth
    readonly property real textAreaY: delegate.codeChrome ? delegate.codeHeaderHeight
        : (delegate.calloutMode ? delegate.calloutHeaderHeight : 0)

    // ---- Code-block chrome ----
    // Set by CodeBlockDelegate. Turns on the header (language selector +
    // copy button), the optional line-number gutter, and horizontal
    // scrolling for long lines (wrap off). Off for every other block type,
    // so the whole code path is inert elsewhere.
    property bool codeChrome: false
    // Line-number gutter, toggled from the view menu and persisted under
    // view.codeLineNumbers. The revision read re-evaluates
    // this binding when the setting flips.
    readonly property bool codeLineNumbers: {
        var r = appSettings.revision  // dependency only
        return codeChrome
            && appSettings.value("view.codeLineNumbers", false) === true
    }
    readonly property int codeHeaderHeight: codeChrome ? 26 : 0
    // Digits of the largest line number, sized in the mono font.
    readonly property int codeGutterWidth: codeLineNumbers
        ? Math.max(28, String(Math.max(1, textArea.lineCount)).length
                       * Math.ceil(contentFontSize * 0.62) + 16)
        : 0
    // Left inset of the code text: past the gutter, with a small gap.
    readonly property int codeContentLeft: codeGutterWidth + (codeLineNumbers ? 8 : 4)
    readonly property int codeViewportWidth:
        Math.max(1, contentArea.width - codeContentLeft)
    readonly property int codeMaxScroll:
        Math.max(0, Math.ceil(textArea.contentWidth) - codeViewportWidth)
    // Horizontal scroll offset (long lines, wrap off). Clamped to the
    // scrollable range as the text or viewport changes.
    property int codeHScroll: 0
    onCodeMaxScrollChanged: if (codeHScroll > codeMaxScroll) codeHScroll = codeMaxScroll

    // Change the code language as one undo step: a
    // convertBlock to the same type/content keeps the delegate (delegateKind
    // is unchanged) while routing through the undo stack.
    function setCodeLanguage(lang) {
        blockModel.convertBlock(delegate.index, Block.CodeBlock,
                                delegate.content, false, lang)
    }

    // ---- Callout chrome ----
    // Set by CalloutBlock. The type reuses `language`, the fold state reuses
    // `checked`. The header (icon + title + fold chevron) renders above the
    // body; a folded callout shows only the header.
    property bool calloutMode: false
    readonly property string calloutType: delegate.language
    readonly property bool calloutFolded: calloutMode && delegate.checked
    // Per-type icon + accent, from theme tokens (no hardcoded colors). An
    // unrecognized type degrades to note styling with its literal label
    // (the tolerance rule). "toggle" is Kvit's minimal-chrome foldable type.
    readonly property var calloutSpec: ({
        "info":    { icon: "i", accent: theme.accent,    label: qsTr("Info") },
        "warning": { icon: "!", accent: theme.warning,   label: qsTr("Warning") },
        "success": { icon: "✓", accent: theme.success,   label: qsTr("Success") },
        "error":   { icon: "✕", accent: theme.danger,    label: qsTr("Error") },
        "tip":     { icon: "★", accent: theme.calloutTip, label: qsTr("Tip") },
        "note":    { icon: "✎", accent: theme.textMuted, label: qsTr("Note") },
        "toggle":  { icon: "",  accent: theme.textMuted, label: "" }
    })
    readonly property var calloutInfo: {
        var t = (delegate.calloutType || "note").toLowerCase()
        return calloutSpec[t] !== undefined ? calloutSpec[t]
            : { icon: "?", accent: theme.textMuted, label: delegate.calloutType }
    }
    readonly property int calloutHeaderHeight: calloutMode ? 28 : 0
    // A callout's custom color (features.md §1.2.10) overrides the typed
    // accent, recoloring the panel tint, border, bar, and header
    // coherently. Absent
    // (the common case) it falls back to the type's accent, byte-identical.
    readonly property color calloutAccent: blockAttributes.has(delegate.attributes, "color")
        ? blockAttributes.str(delegate.attributes, "color")
        : delegate.calloutInfo.accent

    // ---- Text alignment (features.md §9.2) ----
    // The `align` attribute (left|center|right) maps to the TextArea's
    // horizontal alignment for paragraphs and headings. Absent = left, so an
    // unstyled block is unchanged. Lists/quotes/code never receive it.
    readonly property string blockAlign: blockAttributes.str(delegate.attributes, "align", "left")
    readonly property int alignHAlign:
        delegate.blockAlign === "center" ? TextEdit.AlignHCenter
      : delegate.blockAlign === "right"  ? TextEdit.AlignRight
      : TextEdit.AlignLeft
    // Set/clear this block's alignment as one undo step. Center/right store the
    // value; left removes the attribute so the default carries no tag.
    function setBlockAlignment(value) {
        var next = (value === "left" || value === "")
            ? blockAttributes.without(delegate.attributes, "align")
            : blockAttributes.withValue(delegate.attributes, "align", value)
        blockModel.setBlockAttributes(delegate.index, next)
    }

    // ---- Drop cap (features.md §1.2.16) ----
    // A paragraph attribute, not a stored type: dropcap=<lines>,
    // with an optional letter color and font. The enlarged initial replaces the
    // paragraph's first glyph (masked) while the text hangs indented beside it.
    // Because QQuickTextEdit cannot float text and the reveal engine stays free
    // of presentation, it renders in display (unfocused) mode and
    // reflows to normal text for editing — a documented, delegate-only form.
    readonly property int dropCapLines:
        (blockType === Block.Paragraph && !calloutMode && !verbatimEditing)
            ? blockAttributes.num(attributes, "dropcap", 0) : 0
    readonly property bool hasDropCap: dropCapLines >= 2 && delegate.content.length > 0
    readonly property bool dropCapActive: hasDropCap && !delegate.isFocused
    readonly property string dropCapColorAttr:
        blockAttributes.str(attributes, "dropcapcolor", "")
    readonly property color dropCapColor:
        dropCapColorAttr !== "" ? dropCapColorAttr : delegate.contentColor
    readonly property string dropCapFontAttr:
        blockAttributes.str(attributes, "dropcapfont", "")
    readonly property int dropCapPixelSize:
        Math.round(delegate.contentFontSize * (dropCapLines * 1.15))
    readonly property int dropCapWidth:
        dropCapActive ? Math.round(dropCapPixelSize * 0.72) + 6 : 0
    // Set/clear the drop cap as one undo step (used by the paragraph menu).
    function setDropCap(lines) {
        var next = (lines >= 2)
            ? blockAttributes.withValue(delegate.attributes, "dropcap", String(lines))
            : blockAttributes.without(delegate.attributes, "dropcap")
        blockModel.setBlockAttributes(delegate.index, next)
    }

    // Flip the fold state as one undo step: fold reuses checked.
    function toggleCalloutFold() {
        blockModel.setChecked(delegate.index, !delegate.checked)
    }
    function setCalloutTitleText(t) {
        blockModel.setCalloutTitle(delegate.index, t)
    }
    // A callout's custom color (features.md §1.2.10) as one undo step;
    // reset removes the attribute so it falls back to the typed accent.
    function setCalloutColor(v) {
        blockModel.setBlockAttributes(delegate.index,
            blockAttributes.withValue(delegate.attributes, "color", v))
    }
    function resetCalloutColor() {
        blockModel.setBlockAttributes(delegate.index,
            blockAttributes.without(delegate.attributes, "color"))
    }

    // Exposed for tests
    property alias editorEngine: editorEngine
    property alias textArea: textArea

    implicitHeight: contentArea.implicitHeight
        + (trailingLoader.item ? trailingLoader.implicitHeight : 0) + 16

    property bool isFocused: textArea.activeFocus

    // §16.2 typewriter mode: fade every block that does not hold the caret to
    // a reduced opacity. Off the keystroke path — it re-evaluates only when
    // focus moves between blocks (win.caretBlockIndex) or the mode toggles,
    // not per character. Applied to the content, not the delegate root, so it
    // never fights the pooling opacity guard.
    readonly property real typewriterDim: {
        var win = Window.window
        if (win && win.typewriterMode !== undefined && win.typewriterMode
            && win.caretBlockIndex >= 0 && win.caretBlockIndex !== delegate.index)
            return 0.32
        return 1.0
    }

    // Inline-math overlays (features.md §1.2.15): the hidden $…$
    // spans, whose rendered equation the overlay layer draws over the
    // renderer-width transparent TeX box. Re-read on edit and on
    // reveal/relayout (mathTick); empty in verbatim (code) blocks.
    property int mathTick: 0
    // Optically matched to the prose x-height (engine mathFontPixelSize);
    // the engine reserves width/line-height at this same size.
    readonly property int inlineMathPixelSize:
        editorEngine && editorEngine.mathFontPixelSize > 0
            ? editorEngine.mathFontPixelSize
            : Math.round(delegate.contentFontSize > 0
                             ? delegate.contentFontSize : 15)
    readonly property int inlineMathVerticalPadding:
        Math.max(2, Math.ceil(delegate.inlineMathPixelSize * 0.12))
    readonly property var inlineMathBoxes: {
        if (delegate.verbatimEditing || !delegate.editorActive)
            return []
        // textArea.text changes on BOTH edits and reveal transitions (a reveal
        // inserts the $…$ markers into the document), so it is the reliable
        // signal for "which math spans are hidden right now" — the storage
        // markdown does not change on reveal. mathTick covers relayout.
        var dep = textArea.text
        var dep2 = mathTick
        return editorEngine.inlineMathBoxes()
    }
    function inlineMathSource(tex) {
        if (!tex || tex.trim().length === 0)
            return ""
        function h(x) { return ("0" + Math.round(x * 255).toString(16)).slice(-2) }
        var c = delegate.appTheme ? delegate.appTheme.textPrimary : Qt.rgba(0, 0, 0, 1)
        var fg = h(c.a) + h(c.r) + h(c.g) + h(c.b)
        var sz = delegate.inlineMathPixelSize
        var win = Window.window
        var dpr = 1
        if (win && win.devicePixelRatio !== undefined && win.devicePixelRatio > 0)
            dpr = win.devicePixelRatio
        else if (win && win.screen && win.screen.devicePixelRatio > 0)
            dpr = win.screen.devicePixelRatio
        else if (Screen.devicePixelRatio !== undefined && Screen.devicePixelRatio > 0)
            dpr = Screen.devicePixelRatio
        dpr = Math.round(dpr * 100) / 100
        return "image://math/" + mathRenderer.encode(tex)
             + "?fg=" + fg + "&size=" + sz + "&dpr=" + dpr.toFixed(2)
             + "&vpad=" + delegate.inlineMathVerticalPadding
    }

    // Track hover state. The gutter has its own hover handler
    // because its child MouseAreas occlude blockMouseArea; without this,
    // showing the buttons can make the block look un-hovered and start a
    // hide/show loop under the pointer.
    property bool isHovered: blockMouseArea.containsMouse || gutterHover.hovered

    // Whether this block is in the document-level block selection
    // (features.md §3.1). The revision read makes the binding re-evaluate
    // on every selection change; membership itself is queried, never
    // stored here.
    readonly property bool blockSelected: {
        var revision = documentSelection.revision // dependency only
        return documentSelection.isBlockSelected(delegate.index)
    }

    // Whether this row is being dragged (or is part of the dragged
    // selection): it stays in place as §21.4's space-holder, dimmed,
    // while the floating proxy follows the pointer.
    readonly property bool isDragSource: {
        var win = Window.window
        if (!win || !win.blockDrag || !win.blockDrag.active)
            return false
        return win.blockDrag.isMulti ? delegate.blockSelected
                                     : win.blockDrag.sourceIndex === delegate.index
    }

    // The window's cross-block drag coordinator (Window.window only
    // attaches to Items, so the TextArea's PointHandler routes through
    // this delegate-level helper).
    function dragCoordinator() {
        var win = Window.window
        return win && win.crossBlockDrag ? win.crossBlockDrag : null
    }

    // Focus the window-level handler that owns keys while a block
    // selection is active. The TextArea's focus loss collapses its
    // reveal and dismisses any open menu — both intended in selection
    // mode.
    function focusSelectionHandler() {
        var win = Window.window
        if (win && win.selectionKeyHandler)
            win.selectionKeyHandler.forceActiveFocus()
    }

    function activateEditor() {
        editorRequested = true
    }

    // ---- Cross-block text selection support (features.md §2.5, §21.3).
    // The per-block passive PointHandler feeds the window's drag
    // coordinator through these per-delegate position helpers; the
    // coordinator holds the range; this block renders its portion through
    // its own TextArea selection. ----

    // Markdown position under a scene point (clamped into this block).
    function markdownPositionAt(sceneX, sceneY) {
        var p = textArea.mapFromItem(null, sceneX, sceneY)
        var cx = Math.max(0, Math.min(p.x, textArea.width - 1))
        var cy = Math.max(0, Math.min(p.y, textArea.height - 1))
        return editorEngine.toMarkdownPosition(textArea.positionAt(cx, cy))
    }

    // Whether a scene point is over this block's text (a press in the
    // gutter must not seed a text-selection drag).
    function pointInText(sceneX, sceneY) {
        var p = textArea.mapFromItem(null, sceneX, sceneY)
        return p.x >= 0 && p.x <= textArea.width
            && p.y >= 0 && p.y <= textArea.height
    }

    // Markdown position one visual line up/down from mdPos within this
    // block, or -1 when that would leave the block.
    function lineStepPosition(mdPos, dir) {
        var doc = Math.min(editorEngine.toDocumentPosition(mdPos), textArea.text.length)
        var rect = textArea.positionToRectangle(doc)
        var newY = rect.y + dir * rect.height + rect.height / 2
        if (newY < 0)
            return -1
        var newDoc = textArea.positionAt(rect.x, newY)
        var newRect = textArea.positionToRectangle(newDoc)
        if (Math.abs(newRect.y - rect.y) < 1)
            return -1 // same visual line: the step leaves the block
        return editorEngine.toMarkdownPosition(newDoc)
    }

    // Entry position for a vertical crossing into this block at a given
    // x (the first or last visual line).
    function entryPositionAtX(x, fromTop) {
        var y = fromTop ? 2 : textArea.height - 2
        var cx = Math.max(0, Math.min(x, textArea.width - 1))
        return editorEngine.toMarkdownPosition(textArea.positionAt(cx, y))
    }

    function xAtMarkdown(mdPos) {
        var doc = Math.min(editorEngine.toDocumentPosition(mdPos), textArea.text.length)
        return textArea.positionToRectangle(doc).x
    }

    // ---- Find bar hooks (features.md §7) ----

    // Where the cursor is, in markdown coordinates: the find bar seeds
    // "first match at/after the cursor" from this on open.
    function markdownCursor() {
        return editorEngine.toMarkdownPosition(textArea.cursorPosition)
    }

    // The in-block selection as the user sees it (display text), for
    // prefilling the query field.
    function selectionDisplayText() {
        return textArea.selectedText
    }

    // Rectangle (in delegate coordinates) of a markdown position's line,
    // for the find bar's fine scroll into tall blocks.
    function rectForMarkdownPosition(mdPos) {
        var doc = Math.min(editorEngine.toDocumentPosition(mdPos), textArea.text.length)
        var rect = textArea.positionToRectangle(doc)
        var p = delegate.mapFromItem(textArea, rect.x, rect.y)
        return Qt.rect(p.x, p.y, rect.width, rect.height)
    }

    // The Qt.callLater re-apply sites below can outlive this delegate:
    // a selection clear immediately followed by a document reload
    // (find-and-replace flows do this) tears the delegate down before
    // the queued call fires, and calling into its invalidated context
    // is a TypeError.
    function applyTextPortionLater() {
        Qt.callLater(function() {
            if (delegate && typeof delegate.applyTextPortion === "function")
                delegate.applyTextPortion()
        })
    }

    // Apply this block's portion of the cross-block range to the
    // TextArea (persistentSelection keeps it visible unfocused). The
    // focused anchor block needs no help — its native selection IS its
    // portion while the mouse drags, and the keyboard paths manage it.
    function applyTextPortion() {
        if (isPooled || textArea.activeFocus)
            return
        var p = documentSelection.portionForBlock(delegate.index)
        if (p.selected === true && p.end > p.start) {
            var docStart = editorEngine.toDocumentPosition(p.start)
            var docEnd = editorEngine.toDocumentPosition(p.end)
            // Fixed-point guard: re-select only when the TextArea does
            // not already show the desired range, so the re-apply paths
            // below cannot feed back through the engine indefinitely.
            if (textArea.selectionStart !== docStart
                || textArea.selectionEnd !== docEnd)
                textArea.select(docStart, docEnd)
        } else if (textArea.selectionEnd > textArea.selectionStart) {
            textArea.deselect()
        }
    }

    Connections {
        target: documentSelection
        function onRevisionChanged() {
            // Apply now, and once more on a clean stack: the engine's
            // deferred reveal transitions (the blurred anchor block
            // collapsing its markers) edit the document AFTER this
            // handler and destroy a just-applied selection. A cleared
            // selection needs no delayed pass — the synchronous call
            // already deselected, and nothing re-selects afterwards.
            delegate.applyTextPortion()
            if (documentSelection.hasTextSelection)
                delegate.applyTextPortionLater()
        }
    }

    Component.onCompleted: {
        if (documentSelection.hasTextSelection)
            delegate.applyTextPortionLater()
    }

    // Remove the coordinator's range from the model (one undo step) and
    // return the {index, cursor} landing spot.
    // Copy markdown in every clipboard flavor (§5.1). Shared by the
    // cross-block and in-block copy/cut paths.
    function copyMarkdownToClipboard(md) {
        clipboard.setMarkdown(md, markdownFormatter.toHtml(md))
    }

    function crossBlockDeleteRange() {
        var range = documentSelection.orderedTextRange()
        documentSelection.clearTextSelection()
        textArea.deselect()
        return blockModel.removeTextRange(range.startIndex, range.startPos,
                                          range.endIndex, range.endPos)
    }

    // Move the cross-block head one step (Shift+Arrows, §21.3 keyboard
    // extension). Vertical steps stay within the head block's visual
    // lines until they must cross into the neighbor at the same x.
    function moveCrossBlockHead(key) {
        var headIdx = documentSelection.textHeadIndex()
        var headMd = documentSelection.textHeadPosition()
        if (headIdx < 0 || !listView)
            return
        var content = blockModel.getContent(headIdx)
        var headItem = listView.itemAtIndex(headIdx)
        var newIdx = headIdx
        var newMd = headMd

        if (key === Qt.Key_Right) {
            if (headMd < content.length) {
                newMd = headMd + 1
            } else if (headIdx < blockModel.count - 1) {
                newIdx = headIdx + 1
                newMd = 0
            }
        } else if (key === Qt.Key_Left) {
            if (headMd > 0) {
                newMd = headMd - 1
            } else if (headIdx > 0) {
                newIdx = headIdx - 1
                newMd = blockModel.getContent(newIdx).length
            }
        } else if (key === Qt.Key_Down || key === Qt.Key_Up) {
            var dir = key === Qt.Key_Down ? 1 : -1
            var stepped = headItem && headItem.lineStepPosition
                ? headItem.lineStepPosition(headMd, dir) : -1
            if (stepped >= 0) {
                newMd = stepped
            } else {
                var x = headItem && headItem.xAtMarkdown
                    ? headItem.xAtMarkdown(headMd) : 0
                if (dir > 0 && headIdx < blockModel.count - 1) {
                    newIdx = headIdx + 1
                    var below = listView.itemAtIndex(newIdx)
                    newMd = below && below.entryPositionAtX
                        ? below.entryPositionAtX(x, true) : 0
                } else if (dir < 0 && headIdx > 0) {
                    newIdx = headIdx - 1
                    var above = listView.itemAtIndex(newIdx)
                    newMd = above && above.entryPositionAtX
                        ? above.entryPositionAtX(x, false)
                        : blockModel.getContent(newIdx).length
                }
            }
        }

        if (newIdx === documentSelection.textAnchorIndex()
            && newIdx === delegate.index) {
            // The head returned into the anchor block: collapse back to
            // a native in-block selection
            var anchorMd = documentSelection.textAnchorPosition()
            documentSelection.clearTextSelection()
            textArea.select(editorEngine.toDocumentPosition(anchorMd),
                            editorEngine.toDocumentPosition(newMd))
            return
        }
        documentSelection.updateTextSelectionHead(newIdx, newMd)
    }

    // Keys while this block anchors an active cross-block selection.
    // Returns true when the key was consumed.
    function handleCrossBlockKey(event) {
        var ctrl = event.modifiers & Qt.ControlModifier
        var shift = event.modifiers & Qt.ShiftModifier
        var isArrow = event.key === Qt.Key_Left || event.key === Qt.Key_Right
                   || event.key === Qt.Key_Up || event.key === Qt.Key_Down

        if (event.key === Qt.Key_Escape) {
            documentSelection.clearTextSelection()
            textArea.deselect()
            event.accepted = true
            return true
        }
        if (shift && !ctrl && isArrow) {
            moveCrossBlockHead(event.key)
            event.accepted = true
            return true
        }
        if (!ctrl && !shift && isArrow) {
            // Plain arrows collapse the selection to its edge
            var range = documentSelection.orderedTextRange()
            documentSelection.clearTextSelection()
            textArea.deselect()
            var goStart = event.key === Qt.Key_Left || event.key === Qt.Key_Up
            refocusBlock(goStart ? range.startIndex : range.endIndex,
                         goStart ? range.startPos : range.endPos)
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_C && ctrl) {
            copyMarkdownToClipboard(documentSelection.rangeMarkdown())
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_X && ctrl) {
            copyMarkdownToClipboard(documentSelection.rangeMarkdown())
            var cutResult = crossBlockDeleteRange()
            if (cutResult.index !== undefined)
                refocusBlock(cutResult.index, cutResult.cursor)
            event.accepted = true
            return true
        }
        // Ctrl+V / Ctrl+Shift+V over a cross-block selection: the range goes
        // first, exactly as every sibling operation here does, and the
        // clipboard lands at the collapsed caret. Without this branch the
        // per-block handler would run instead and see only the head block's
        // own selection, leaving the rest of the range in the document.
        if (event.key === Qt.Key_V && ctrl) {
            if (clipboard && clipboard.hasText) {
                var stripPaste = (event.modifiers & Qt.ShiftModifier) ? true : false
                var pasteRes = crossBlockDeleteRange()
                if (pasteRes.index !== undefined)
                    pasteMarkdownAtBlock(pasteRes.index, pasteRes.cursor,
                                         clipboard.text, stripPaste)
            }
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            var delResult = crossBlockDeleteRange()
            if (delResult.index !== undefined)
                refocusBlock(delResult.index, delResult.cursor)
            event.accepted = true
            return true
        }
        // Printable text replaces the range; the deletion and the typed
        // character are layered undo steps
        if (!ctrl && event.text.length > 0 && event.text.charCodeAt(0) >= 32) {
            var repResult = crossBlockDeleteRange()
            if (repResult.index !== undefined) {
                var md = blockModel.getContent(repResult.index)
                blockModel.updateContent(repResult.index,
                    md.substring(0, repResult.cursor) + event.text
                    + md.substring(repResult.cursor))
                refocusBlock(repResult.index, repResult.cursor + event.text.length)
            }
            event.accepted = true
            return true
        }
        return false
    }

    // Get reference to the ListView
    property ListView listView: ListView.view

    // Pooled rows are NOT hidden by the ListView — they stay parented
    // and visible, and ordinarily disappear only because the remove
    // transition finished fading them to opacity 0. Under churn that
    // transition can be cut short, leaving a full-opacity "ghost" row
    // rendering over the live document. Pin the end states explicitly on
    // both sides of the pool boundary instead of relying on transition
    // completion.
    ListView.onPooled: {
        isPooled = true
        textArea.focus = false
        opacity = 0
    }

    ListView.onReused: {
        isPooled = false
        opacity = 1
        // A block scrolled back into view may sit inside an active
        // cross-block range; re-render its portion.
        if (documentSelection.hasTextSelection)
            delegate.applyTextPortionLater()
    }

    // Toolbar / formatting-bar surface: the caret's combined span flags
    // for button states, and the same registry toggle the keyboard
    // shortcuts drive. Inert in verbatim mode, exactly like the
    // shortcuts.
    readonly property int cursorFormatFlags: {
        if (!delegate.editorActive)
            return 0
        var contentDep = delegate.content
        var cursorDep = textArea.cursorPosition
        var focusDep = textArea.activeFocus
        return editorEngine.formatFlagsAtDocumentPosition(
            textArea.cursorPosition)
    }
    function toggleSpanType(typeName) {
        if (delegate.verbatimEditing)
            return
        textArea.toggleSpan(typeName)
    }
    // Text color controls for the toolbar / formatting bar / context menu.
    // applyColor wraps or recolors the selection;
    // removeColor unwraps. currentColor reflects the color span under the
    // caret so the controls can show it and enable "remove".
    function applyColor(value) {
        if (delegate.verbatimEditing) return
        textArea.applyColorValue(value)
    }
    function removeColor() {
        if (delegate.verbatimEditing) return
        textArea.removeColorSpan()
    }
    readonly property string currentColor: {
        if (!delegate.editorActive || delegate.verbatimEditing)
            return ""
        var contentDep = delegate.content
        var cursorDep = textArea.cursorPosition
        var selDep = textArea.selectionStart + "," + textArea.selectionEnd
        var mdStart = editorEngine.toMarkdownPosition(textArea.selectionStart)
        var mdEnd = editorEngine.toMarkdownPosition(textArea.selectionEnd)
        var info = markdownFormatter.colorSpanAt(editorEngine.markdown,
                                                 mdStart, mdEnd)
        return info.found ? info.color : ""
    }

    // Status-bar caret position (features.md §9.7):
    // 1-based line/column within this block's display text.
    readonly property var cursorLineColumn: {
        if (!delegate.editorActive)
            return { line: 1, column: 1 }
        var pos = textArea.cursorPosition
        var before = textArea.text.substring(0, pos)
        var lastNewline = before.lastIndexOf("\n")
        var line = (before.match(/\n/g) || []).length + 1
        return { line: line, column: pos - lastNewline }
    }

    // Formatting-bar surface: the in-block selection in
    // display text and document coordinates, and its bounding rectangle
    // in delegate coordinates.
    readonly property int selectionStartDoc: delegate.editorActive ? textArea.selectionStart : 0
    readonly property int selectionEndDoc: delegate.editorActive ? textArea.selectionEnd : 0
    readonly property string selectedDisplayText: delegate.editorActive ? textArea.selectedText : ""
    function selectionRectangle() {
        if (!delegate.editorActive)
            return { x: 0, y: 0, width: 0, height: 0 }
        var a = textArea.positionToRectangle(textArea.selectionStart)
        var b = textArea.positionToRectangle(textArea.selectionEnd)
        var x0 = Math.min(a.x, b.x)
        var y0 = Math.min(a.y, b.y)
        var x1 = Math.max(a.x + a.width, b.x + b.width)
        var y1 = Math.max(a.y + a.height, b.y + b.height)
        var tl = textArea.mapToItem(delegate, x0, y0)
        return { x: tl.x, y: tl.y, width: x1 - x0, height: y1 - y0 }
    }

    // Context-menu entry points (features.md §9.5): thin wrappers over
    // the same functions the keyboard shortcuts drive.
    function cutSelection() { textArea.cutSelectionAsMarkdown() }
    function copySelection() { textArea.copySelectionAsMarkdown() }
    function pasteClipboard(plain) { textArea.pasteFromClipboard(plain) }
    function selectAllText() { textArea.selectAll() }

    // Asset ingestion context for pasted/dropped images.
    function noteDir() {
        var p = documentManager.currentFilePath
        var idx = p.lastIndexOf("/")
        return idx >= 0 ? p.substring(0, idx) : ""
    }
    function noteSlug() {
        var p = documentManager.currentFilePath
        var fn = p.substring(p.lastIndexOf("/") + 1).replace(/\.[^.]+$/, "")
        var slug = fn.toLowerCase().replace(/[^a-z0-9]+/g, "-")
                     .replace(/^-+|-+$/g, "")
        return slug === "" ? "image" : slug
    }
    function assetRoot() {
        return noteCollection.isOpen ? noteCollection.rootPath : ""
    }
    // §5.3 paste arm: clipboard image data → asset file → image block below
    // (or converting the current empty block). Returns true if it handled an
    // image, so the text-paste path can be skipped.
    function handleImagePaste() {
        if (!imageAssets.clipboardHasImage())
            return false
        var stored = imageAssets.ingestClipboardImage(
            noteSlug(), assetRoot(), noteDir())
        if (stored === "")
            return false
        insertImageBlock(stored)
        return true
    }
    function insertImageBlock(storedPath) {
        var md = imageAssets.build(storedPath, "", "", 0)
        if (delegate.content === "" && !delegate.verbatimEditing) {
            blockModel.convertBlock(delegate.index, Block.Image, md)
            delegate.refocusBlock(delegate.index, 0)
        } else {
            var at = delegate.index + 1
            blockModel.insertBlock(at, Block.Image, md)
            Qt.callLater(function() {
                if (delegate.listView) {
                    delegate.listView.currentIndex = at
                    var item = delegate.listView.itemAtIndex(at)
                    if (item && item.focusAtStart) item.focusAtStart()
                }
            })
        }
    }
    function openLinkUnderCursor() {
        var url = editorEngine.linkAtDocumentPosition(textArea.cursorPosition)
        if (url !== "")
            delegate.openLinkAt(textArea.cursorPosition)
    }
    // §2.4 "remove link formatting while keeping text" without the
    // dialog round-trip.
    function removeLinkAtCursor() {
        if (delegate.verbatimEditing)
            return
        var info = editorEngine.linkSpanAtCursor(textArea.cursorPosition)
        if (!info.found || !info.removable)
            return
        var md = editorEngine.markdown
        blockModel.updateContent(delegate.index,
                                 md.substring(0, info.start) + info.text
                                     + md.substring(info.end))
        focusAtPosition(info.start + info.text.length)
    }

    // Public functions for focus management. Positions are MARKDOWN
    // positions (model coordinates); they are converted to document
    // positions through the engine, which honors the reveal state.
    function focusAtStart() {
        activateEditor()
        textArea.forceActiveFocus()
        textArea.cursorPosition = 0
    }

    function focusAtEnd() {
        activateEditor()
        textArea.forceActiveFocus()
        Qt.callLater(function() {
            textArea.cursorPosition = textArea.text.length
        })
    }

    function focusAtPosition(markdownPos) {
        activateEditor()
        textArea.forceActiveFocus()
        Qt.callLater(function() {
            var docPos = editorEngine.toDocumentPosition(markdownPos)
            textArea.cursorPosition = Math.min(docPos, textArea.text.length)
        })
    }

    // Helper functions for cursor position detection
    function isCursorOnFirstLine() {
        if (textArea.text.indexOf('\n') === -1) return true
        var rect = textArea.positionToRectangle(textArea.cursorPosition)
        var firstLineRect = textArea.positionToRectangle(0)
        return Math.abs(rect.y - firstLineRect.y) < 1
    }

    function isCursorOnLastLine() {
        if (textArea.text.indexOf('\n') === -1) return true
        var rect = textArea.positionToRectangle(textArea.cursorPosition)
        var lastLineRect = textArea.positionToRectangle(textArea.text.length)
        return Math.abs(rect.y - lastLineRect.y) < 1
    }

    // Refocus a block after an operation that may have recreated its
    // delegate (any type change re-resolves the DelegateChooser choice).
    // Insert a structured markdown payload at the caret, parsing it into typed
    // blocks (§5.3). The text before the caret stays in the original block,
    // the payload's blocks follow it, and any text after the caret trails as
    // its own paragraph. When the caret sat in an empty block, that emptied
    // block is dropped so the paste does not leave a blank line behind.
    function pasteStructuredMarkdown(idx, before, pasted, after) {
        blockModel.updateContent(idx, before)
        var inserted = documentSerializer.insertMarkdownAt(blockModel, idx + 1,
                                                           pasted)
        if (inserted === 0) {
            blockModel.updateContent(idx, before + after)
            refocusBlock(idx, before.length)
            return
        }
        var lastIdx = idx + inserted
        if (after.length > 0) {
            blockModel.insertBlock(lastIdx + 1, 0, after)
        }
        var caretIdx = lastIdx
        var caretPos = blockModel.getContent(lastIdx).length
        if (before.length === 0) {
            blockModel.removeBlock(idx)
            caretIdx -= 1
        }
        refocusBlock(caretIdx, caretPos)
    }

    // Insert clipboard markdown at a markdown offset inside block `idx`,
    // splitting into blocks on newlines the way the in-block paste does:
    // the first line joins the text before the caret, the last line joins
    // the text after it. Used by the cross-block paste path, which has
    // already collapsed its range to (idx, mdPos).
    function pasteMarkdownAtBlock(idx, mdPos, raw, stripFormatting) {
        var pasted = raw.replace(/\r\n/g, "\n")
        if (stripFormatting) {
            pasted = pasted.split("\n").map(function(line) {
                return editorEngine.stripFormatting(line)
            }).join("\n")
        }
        if (pasted.length === 0) {
            refocusBlock(idx, mdPos)
            return
        }
        var md = blockModel.getContent(idx)
        var before = md.substring(0, mdPos)
        var after = md.substring(mdPos)
        var lines = pasted.split("\n")
        if (lines.length === 1) {
            blockModel.updateContent(idx, before + pasted + after)
            refocusBlock(idx, mdPos + pasted.length)
            return
        }
        blockModel.updateContent(idx, before + lines[0])
        var insertAt = idx + 1
        for (var i = 1; i < lines.length - 1; i++) {
            blockModel.insertBlock(insertAt, 0, lines[i])
            insertAt++
        }
        var lastLine = lines[lines.length - 1]
        blockModel.insertBlock(insertAt, 0, lastLine + after)
        refocusBlock(insertAt, lastLine.length)
    }

    function refocusBlock(idx, markdownPos) {
        var lv = listView
        Qt.callLater(function() {
            if (lv) {
                lv.currentIndex = idx
                var item = lv.itemAtIndex(idx)
                if (item) item.focusAtPosition(markdownPos)
            }
        })
    }

    // Block operation functions. Enter at the end of a list item or
    // quote continues the type (features.md §1.2.4: "Enter creates new
    // list item at same level"); todos continue unchecked; everything
    // else — headings included (§1.2.2) — starts a paragraph.
    function createBlockBelow() {
        var newIndex = delegate.index + 1
        if (delegate.isStructural) {
            blockModel.insertBlock(newIndex, delegate.blockType, "",
                                   delegate.indentLevel)
        } else {
            blockModel.insertBlock(newIndex, 0, "") // 0 = Paragraph
        }

        Qt.callLater(function() {
            if (listView) {
                listView.currentIndex = newIndex
                var item = listView.itemAtIndex(newIndex)
                if (item) item.focusAtStart()
            }
        })
    }

    function splitBlockAtCursor() {
        var pos = editorEngine.toMarkdownPosition(textArea.cursorPosition)
        var newIndex = delegate.index + 1

        blockModel.splitBlock(delegate.index, pos)

        Qt.callLater(function() {
            if (listView) {
                listView.currentIndex = newIndex
                var item = listView.itemAtIndex(newIndex)
                if (item) item.focusAtStart()
            }
        })
    }

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

    function mergeWithPreviousBlock() {
        var prevIndex = delegate.index - 1
        var prevBlock = blockModel.blockAt(prevIndex)

        if (!prevBlock) return

        var cursorPosInMerged = prevBlock.content.length

        blockModel.mergeBlocks(prevIndex, delegate.index)

        Qt.callLater(function() {
            if (listView) {
                listView.currentIndex = prevIndex
                var item = listView.itemAtIndex(prevIndex)
                if (item) item.focusAtPosition(cursorPosInMerged)
            }
        })
    }

    function mergeWithNextBlock() {
        var nextIndex = delegate.index + 1

        if (nextIndex >= blockModel.count) return

        var mdPos = editorEngine.toMarkdownPosition(textArea.cursorPosition)

        blockModel.mergeBlocks(delegate.index, nextIndex)

        // Cursor stays at the same markdown position
        textArea.cursorPosition = editorEngine.toDocumentPosition(mdPos)
    }

    // Convert block to a different type (features.md §13.3 shortcuts). One
    // undo step through convertBlock, which also drops indentation when
    // leaving the list family. The DelegateChooser may recreate this
    // delegate, so focus is re-established by index.
    function convertBlockType(newType) {
        if (delegate.blockType === newType) return
        var idx = delegate.index
        var mdPos = editorEngine.toMarkdownPosition(textArea.cursorPosition)
        // Turning any block into a callout seeds the default "info" type so
        // it renders as a real callout rather than an unknown one.
        var lang = newType === Block.Callout ? "info" : ""
        // Announce before the model mutation: changing the block type replaces
        // this DelegateChooser branch and can invalidate the current QML
        // execution context before code after convertBlock() gets to run.
        var names = ["Paragraph", "Heading 1", "Heading 2", "Heading 3",
            "Bulleted list", "Numbered list", "To-do", "Quote", "Code block",
            "Divider", "Heading 4", "Image", "Callout", "Math block", "Media",
            "Table"]
        if (typeof a11y !== "undefined" && names[newType])
            a11y.announceConversion(names[newType])
        blockModel.convertBlock(idx, newType, editorEngine.markdown, false, lang)
        refocusBlock(idx, mdPos)
    }

    // Exit a structural block back to a paragraph, keeping the content
    // (the Backspace ladder's last rung and the empty-item Enter exit).
    function unstructureToParagraph() {
        var idx = delegate.index
        var mdPos = editorEngine.toMarkdownPosition(textArea.cursorPosition)
        blockModel.convertBlock(idx, 0, editorEngine.markdown)
        refocusBlock(idx, mdPos)
    }

    // Markdown prefix auto-conversion matchers. Applied to the edited
    // markdown of a paragraph — plus the todo upgrade inside bullets,
    // since typing "- [ ] " passes through the bullet conversion on the
    // way. Returns { type, content, checked?, language? } or null. Order:
    // todo before bullet (its prefix is a superset); exact-content
    // matchers (fence, divider) never collide.
    function matchBlockPrefix(md) {
        var m
        if (delegate.blockType === Block.BulletList) {
            m = md.match(/^\[( |x|X)\] ([\s\S]*)$/)
            if (m) return { type: Block.Todo, content: m[2], checked: m[1] !== " " }
            return null
        }
        // Typing an Obsidian callout header in a quote converts it to a
        // callout: "> " makes the quote, then "[!type]" (as its
        // closing bracket lands) makes the callout. Only a single-line header
        // (no body yet) auto-converts, so an existing multi-paragraph quote
        // is left alone; the body is typed into the callout afterward.
        if (delegate.blockType === Block.Quote) {
            m = md.match(/^\[!([A-Za-z][A-Za-z0-9_-]*)\]([+-]?)\s*([^\n]*)$/)
            if (m)
                return { type: Block.Callout, content: "",
                         checked: m[2] === "-", language: m[1],
                         calloutTitle: m[3] }
            return null
        }
        if (delegate.blockType !== 0)
            return null
        m = md.match(/^[-*] \[( |x|X)\] ([\s\S]*)$/)
        if (m) return { type: Block.Todo, content: m[2], checked: m[1] !== " " }
        m = md.match(/^[-*] ([\s\S]*)$/)
        if (m) return { type: Block.BulletList, content: m[1] }
        m = md.match(/^\d+\. ([\s\S]*)$/)
        if (m) return { type: Block.NumberedList, content: m[1] }
        m = md.match(/^> ([\s\S]*)$/)
        if (m) return { type: Block.Quote, content: m[1] }
        if (md.startsWith("#### "))
            return { type: Block.Heading4, content: md.substring(5) }
        if (md.startsWith("### "))
            return { type: Block.Heading3, content: md.substring(4) }
        if (md.startsWith("## "))
            return { type: Block.Heading2, content: md.substring(3) }
        if (md.startsWith("# "))
            return { type: Block.Heading1, content: md.substring(2) }
        // A fence converts the moment the third backtick lands; the
        // language variant covers pasted fences (typing one is precluded
        // by the immediate conversion)
        m = md.match(/^```([A-Za-z0-9+#-]*)$/)
        if (m) return { type: Block.CodeBlock, content: "", language: m[1] }
        // Immediate divider conversion: auto-save can fire
        // at any moment, and a paragraph reading exactly --- or ***
        // would reload as a divider anyway
        if (md === "---" || md === "***")
            return { type: Block.Divider, content: "" }
        return null
    }

    // The window's block-type menu while it is open FOR THIS BLOCK, else
    // null. Gates the key forwarding and the query updates so a menu
    // targeting another block never affects this one.
    function activeBlockMenu() {
        var win = Window.window
        var menu = win ? win.blockMenu : null
        return (menu && menu.visible && menu.targetIndex === delegate.index)
                ? menu : null
    }

    // Open the block menu anchored at this block's text cursor
    // (features.md §4.3 "position menu near cursor"). The anchor is in
    // window coordinates — the popup's overlay parent fills the window.
    function openBlockMenu(mode) {
        var win = Window.window
        if (!win || !win.blockMenu)
            return
        var rect = textArea.positionToRectangle(textArea.cursorPosition)
        var topLeft = textArea.mapToItem(null, rect.x, rect.y)
        win.blockMenu.openForBlock(delegate.index, mode,
            Qt.rect(topLeft.x, topLeft.y, rect.width, rect.height))
    }

    // ---- Math-entry assistance ----
    // Tab slot-chain: armed by a menu insertion; Tab hops between the
    // empty {} / [] pairs inside the current math span.
    property bool mathSlotChain: false
    // Transient $-pair tracking: document position of the opening $ of a
    // freshly auto-paired $$, else -1. Governs Backspace-deletes-both,
    // Delete-leaves-literal, and $-types-over while the caret stays
    // inside the pair.
    property int dollarPairOpenPos: -1

    // The window's math command menu while it is open FOR THIS EDITOR,
    // else null (the activeBlockMenu() pattern).
    function activeMathMenu() {
        var win = Window.window
        var menu = win ? win.mathCommandMenu : null
        return (menu && menu.visible && menu.targets(textArea)) ? menu : null
    }

    function openMathMenu() {
        var win = Window.window
        if (!win || !win.mathCommandMenu)
            return
        var rect = textArea.positionToRectangle(textArea.cursorPosition)
        var topLeft = textArea.mapToItem(null, rect.x, rect.y)
        win.mathCommandMenu.openForHost(textArea,
            Qt.rect(topLeft.x, topLeft.y, rect.width, rect.height),
            false /* inline context: single-line templates */)
        textArea.syncMathMenuQuery()
    }

    // ---- Wiki-link completion ----
    // The window's wiki-link menu while it is open FOR THIS EDITOR, else
    // null (the activeMathMenu() pattern).
    function activeWikiMenu() {
        var win = Window.window
        var menu = win ? win.wikiLinkMenu : null
        return (menu && menu.visible && menu.targets(textArea)) ? menu : null
    }

    function openWikiMenu() {
        var win = Window.window
        if (!win || !win.wikiLinkMenu)
            return
        var rect = textArea.positionToRectangle(textArea.cursorPosition)
        var topLeft = textArea.mapToItem(null, rect.x, rect.y)
        win.wikiLinkMenu.openForHost(textArea,
            Qt.rect(topLeft.x, topLeft.y, rect.width, rect.height))
        textArea.syncWikiMenuQuery()
    }

    // The pair's closing $ position while tracking is valid for the
    // current caret, else -1: the opening $ still stands, the caret is
    // inside the pair, and the next $ is at or right of the caret.
    function dollarPairClosePos() {
        var open = dollarPairOpenPos
        if (open < 0 || open >= textArea.text.length
            || textArea.text.charAt(open) !== "$")
            return -1
        if (textArea.cursorPosition <= open)
            return -1
        var close = textArea.text.indexOf("$", open + 1)
        if (close < 0 || close < textArea.cursorPosition)
            return -1
        return close
    }

    // The gutter plus-button (features.md §3.7): insert an empty
    // paragraph below this block, focus it, and open the block menu for
    // it with an empty filter. Escape then simply leaves the new
    // paragraph — the block the user asked to add.
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

    // A menu targeting this block follows its focus: clicking or
    // arrowing away dismisses (§4.1 "click outside menu to close" is
    // the popup's own close policy; this covers keyboard focus moves).
    // Gaining focus also records this as the current block (§3.1 "click
    // on block to select it") — the Shift+Click range anchor reads it.
    // Deliberately NOT via listView.currentIndex: setting currentIndex
    // makes the ListView hand focus to the delegate root, which would
    // steal it right back from this TextArea.
    onIsFocusedChanged: {
        if (isFocused) {
            var win = Window.window
            if (win && win.lastFocusedBlock !== undefined)
                win.lastFocusedBlock = index
        }
        if (!isFocused) {
            editorRequested = false
            var menu = activeBlockMenu()
            if (menu)
                menu.dismiss()
            // persistentSelection keeps selections visible unfocused —
            // wanted only while this block renders a cross-block
            // portion; otherwise focus loss clears the highlight. A
            // context menu targeting this block holds the selection: its
            // Cut/Copy/formatting act on it.
            var win2 = Window.window
            var menuHolds = win2 && win2.contextMenuHoldsSelection
                            && win2.contextMenuHoldsSelection(delegate)
            if (!documentSelection.hasTextSelection && !menuHolds)
                textArea.deselect()
        }
    }

    // Open the link at a document position through the window's link
    // opener (features.md §2.4; hookable for tests).
    function openLinkAt(docPos) {
        var url = editorEngine.linkAtDocumentPosition(docPos)
        if (url.length > 0) {
            var win = Window.window
            if (win && win.linkOpener)
                win.linkOpener.activate(url)
        }
    }

    // Ctrl+K (features.md §2.4): inside an existing link the dialog
    // edits it in place; otherwise it inserts a link at the selection,
    // prefilled with the selection's plain text.
    function openLinkDialog() {
        if (delegate.verbatimEditing)
            return
        var win = Window.window
        if (!win || !win.linkDialog)
            return
        var info = editorEngine.linkSpanAtCursor(textArea.cursorPosition)
        if (info.found) {
            win.linkDialog.openForEdit(delegate.index, info.start, info.end,
                                       info.text, info.url, info.removable)
        } else {
            var mdStart = editorEngine.toMarkdownPosition(textArea.selectionStart)
            var mdEnd = editorEngine.toMarkdownPosition(textArea.selectionEnd)
            var initialText = textArea.selectionEnd > textArea.selectionStart
                ? editorEngine.stripFormatting(
                      editorEngine.markdownForRange(textArea.selectionStart,
                                                    textArea.selectionEnd))
                : ""
            win.linkDialog.openForInsert(delegate.index, mdStart, mdEnd, initialText)
        }
    }

    // Move block up
    function moveBlockUp() {
        var fromIndex = delegate.index
        var toIndex = fromIndex - 1
        var cursorPos = editorEngine.toMarkdownPosition(textArea.cursorPosition)

        blockModel.moveBlock(fromIndex, toIndex)

        // Maintain focus on the moved block
        Qt.callLater(function() {
            if (listView) {
                listView.currentIndex = toIndex
                var item = listView.itemAtIndex(toIndex)
                if (item) {
                    item.focusAtPosition(cursorPos)
                }
            }
        })
    }

    // Move block down
    function moveBlockDown() {
        var fromIndex = delegate.index
        var toIndex = fromIndex + 1
        var cursorPos = editorEngine.toMarkdownPosition(textArea.cursorPosition)

        blockModel.moveBlock(fromIndex, toIndex)

        // Maintain focus on the moved block
        Qt.callLater(function() {
            if (listView) {
                listView.currentIndex = toIndex
                var item = listView.itemAtIndex(toIndex)
                if (item) {
                    item.focusAtPosition(cursorPos)
                }
            }
        })
    }

    // Full delegate mouse area for hover detection
    MouseArea {
        id: blockMouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton  // Don't capture clicks
        propagateComposedEvents: true
    }

    Rectangle {
        id: blockContainer

        anchors.fill: parent
        anchors.leftMargin: delegate.indentLevel * 24

        color: "transparent"
        opacity: delegate.isDragSource ? 0.35 : 1

        // Hover background
        Rectangle {
            id: hoverBackground
            anchors.fill: parent
            anchors.leftMargin: 44  // Leave space for the gutter
            color: delegate.isHovered && !delegate.isFocused
                   && !delegate.blockSelected ? theme.blockHoverTint : "transparent"
            radius: 4

            Behavior on color {
                ColorAnimation { duration: 100 }
            }
        }

        // Block-selection indication (features.md §3.1: background and
        // border), covering the same area as the hover background.
        Rectangle {
            id: selectionBackground
            objectName: "selectionBackground"
            anchors.fill: parent
            anchors.leftMargin: 44
            radius: 4
            visible: delegate.blockSelected
            color: theme.blockSelectionTint
            border.color: theme.accent
            border.width: 1
        }

        // Block handle/gutter (widened for the plus-button) - visible
        // on hover
        Item {
            id: blockHandle
            width: 40
            anchors.left: parent.left
            anchors.top: parent.top
            height: 24
            anchors.topMargin: 4

            HoverHandler {
                id: gutterHover
            }

            Row {
                objectName: "gutterButtons"
                anchors.centerIn: parent
                spacing: 4
                // Stays visible while the handle is pressed: hiding an
                // item cancels its MouseArea's grab, which would kill a
                // drag the moment the pointer left this block's hover
                // area (bites multi-drags, whose source row does not
                // follow the pointer).
                opacity: delegate.isHovered || handleArea.pressed ? 1 : 0
                visible: opacity > 0

                Behavior on opacity {
                    NumberAnimation { duration: 150 }
                }

                // Plus-button: add a block below and open the block
                // menu for it (features.md §3.7)
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
                        onClicked: delegate.insertBlockBelowAndOpenMenu()
                    }
                }

                // Drag handle dots. Clicking selects the whole block
                // (features.md §3.1 "click on block handle to select");
                // the reorder drag lives on the same handle.
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

                    // Below the drag threshold a press-release is the
                    // §3.1 select-block click; past it the reorder drag
                    // begins (§3.2). preventStealing keeps the Flickable
                    // from grabbing the vertical movement.
                    MouseArea {
                        id: handleArea
                        objectName: "dragHandle"
                        anchors.fill: parent
                        anchors.margins: -2
                        hoverEnabled: true
                        cursorShape: Qt.OpenHandCursor
                        preventStealing: true
                        // Right-click: the §9.5 block menu.
                        acceptedButtons: Qt.LeftButton | Qt.RightButton

                        property real pressX: 0
                        property real pressY: 0
                        property bool dragging: false

                        onPressed: function(mouse) {
                            if (mouse.button === Qt.RightButton) {
                                var win = Window.window
                                if (win && win.openBlockHandleMenu)
                                    win.openBlockHandleMenu(delegate)
                                return
                            }
                            pressX = mouse.x
                            pressY = mouse.y
                            dragging = false
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed
                                || (pressedButtons & Qt.RightButton))
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
                                win.blockDrag.begin(delegate.index, sp.x, sp.y)
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
        }

        Rectangle {
            id: focusIndicator
            objectName: "focusIndicator"

            anchors.left: blockHandle.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            // A wider bar in the focus-ring color: the visible keyboard-focus
            // indicator for the editor content (§14.1), stronger than the
            // 3px accent bar it replaces.
            width: delegate.isFocused ? 4 : 3

            color: delegate.isFocused ? theme.focusRing : "transparent"

            Behavior on color {
                enabled: theme.motionScale > 0
                ColorAnimation { duration: 150 * theme.motionScale }
            }
        }

        // Per-type leading chrome (bullet glyph, ordinal, checkbox, quote
        // bar). The loader spans the block height; the chrome positions
        // itself and defines its width.
        Loader {
            id: leadingLoader
            objectName: "leadingChrome"
            anchors.left: focusIndicator.right
            anchors.leftMargin: delegate.leadingChrome ? 8 : 0
            anchors.top: parent.top
            anchors.topMargin: 4
            anchors.bottom: parent.bottom
            width: item ? item.implicitWidth : 0
            sourceComponent: delegate.leadingChrome
            opacity: delegate.typewriterDim  // fade the bullet/ordinal too
        }

        // Per-type trailing chrome below the content (quote attribution).
        Loader {
            id: trailingLoader
            objectName: "trailingChrome"
            anchors.left: contentArea.left
            anchors.right: contentArea.right
            anchors.top: contentArea.bottom
            height: item ? item.implicitHeight : 0
            sourceComponent: delegate.trailingChrome
        }

        Item {
            id: contentArea

            anchors.left: leadingLoader.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.topMargin: 4

            // Typewriter-mode fade (§16.2). A binding, not an imperative set,
            // so it composes with the pooling opacity on the delegate root.
            opacity: delegate.typewriterDim
            Behavior on opacity {
                enabled: theme.motionScale > 0
                NumberAnimation { duration: 120 * theme.motionScale }
            }

            // Code blocks scroll horizontally rather than wrap; the clip
            // keeps a long line inside the panel.
            clip: delegate.codeChrome

            Loader {
                active: delegate.hasDropCap
                sourceComponent: dropCapComponent
            }
            Component {
                id: dropCapComponent
                Item {
                    // ---- Drop cap (features.md §1.2.16) ----
                    // Drop-cap chrome is rare; keeping it behind a Loader
                    // avoids building text metrics/masks for ordinary rows.
                    TextMetrics {
                        id: dropCapMetrics
                        font.family: textArea.font.family
                        font.pixelSize: delegate.contentFontSize
                        text: delegate.content.charAt(0)
                    }
                    Rectangle {
                        objectName: "dropCapMask"
                        visible: delegate.dropCapActive
                        x: textArea.x + textArea.leftPadding - 1
                        y: textArea.y + textArea.topPadding
                        width: dropCapMetrics.advanceWidth + 3
                        height: dropCapMetrics.height
                        color: theme.windowBackground
                        z: 4
                    }
                    Text {
                        objectName: "dropCapLetter"
                        visible: delegate.dropCapActive
                        text: delegate.content.charAt(0)
                        color: delegate.dropCapColor
                        font.bold: true
                        font.pixelSize: delegate.dropCapPixelSize
                        font.family: delegate.dropCapFontAttr !== ""
                            ? delegate.dropCapFontAttr : textArea.font.family
                        x: 2
                        y: textArea.y + textArea.topPadding
                        z: 5
                    }
                }
            }

            // textArea.y is 0 for every non-code block, so this is unchanged
            // there; for code it adds the header's height above the text. A
            // folded callout collapses to just its header.
            implicitHeight: delegate.calloutFolded
                ? delegate.calloutHeaderHeight
                : (delegate.useReadOnlyText
                    ? delegate.textAreaY + textArea.topPadding
                        + readOnlyText.implicitHeight + textArea.bottomPadding
                    : delegate.textAreaY + textArea.implicitHeight)

            Loader {
                id: calloutChromeLoader
                active: delegate.calloutMode
                anchors.fill: parent
                sourceComponent: calloutChromeComponent
            }
            Component {
                id: calloutChromeComponent
                Item {
                    anchors.fill: parent

                    // ---- Callout chrome ----
                    // Tinted panel with an accent left bar; the header (icon,
                    // title, fold chevron) sits at the top and the body below.
                    Rectangle {
                        id: calloutPanel
                        objectName: "calloutPanel"
                        anchors.fill: parent
                        radius: 5
                        color: Qt.alpha(delegate.calloutAccent, 0.10)
                        border.width: 1
                        border.color: Qt.alpha(delegate.calloutAccent, 0.35)
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 4
                            radius: 2
                            color: delegate.calloutAccent
                        }
                    }
                    Item {
                        id: calloutHeader
                        objectName: "calloutHeader"
                        x: 10
                        y: 0
                        z: 3
                        width: parent.width - 14
                        height: delegate.calloutHeaderHeight

                        Text {
                            id: calloutChevron
                            objectName: "calloutFoldChevron"
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: delegate.calloutFolded ? "▸" : "▾"
                            color: delegate.calloutAccent
                            font.pixelSize: 12
                            TapHandler { onTapped: delegate.toggleCalloutFold() }
                        }
                        Text {
                            id: calloutIcon
                            anchors.left: calloutChevron.right
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            visible: delegate.calloutInfo.icon !== ""
                            text: delegate.calloutInfo.icon
                            color: delegate.calloutAccent
                            font.pixelSize: 13
                            font.bold: true
                        }
                        TextField {
                            id: calloutTitleField
                            objectName: "calloutTitleField"
                            anchors.left: calloutIcon.visible ? calloutIcon.right : calloutChevron.right
                            anchors.leftMargin: 6
                            anchors.right: calloutColorDot.left
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: delegate.calloutTitle
                            placeholderText: delegate.calloutInfo.label
                            color: delegate.calloutAccent
                            font.pixelSize: 13
                            font.bold: true
                            background: null
                            padding: 0
                            onEditingFinished: {
                                if (text !== delegate.calloutTitle)
                                    delegate.setCalloutTitleText(text)
                            }
                        }
                        Rectangle {
                            id: calloutColorDot
                            objectName: "calloutColorDot"
                            anchors.right: parent.right
                            anchors.rightMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: 14; height: 14; radius: 7
                            color: delegate.calloutAccent
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.4)
                            opacity: delegate.isHovered || calloutColorPicker.visible ? 1 : 0.35
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                            TapHandler { onTapped: calloutColorPicker.open() }

                            CalloutColorPicker {
                                id: calloutColorPicker
                                x: parent ? parent.width - width : 0
                                y: parent ? parent.height + 4 : 0
                                currentColor: blockAttributes.str(delegate.attributes, "color", "")
                                onColorPicked: function(v) { delegate.setCalloutColor(v) }
                                onResetRequested: delegate.resetCalloutColor()
                            }
                        }
                    }
                }
            }

            Loader {
                id: codeChromeLoader
                active: delegate.codeChrome
                anchors.fill: parent
                sourceComponent: codeChromeComponent
            }
            Component {
                id: codeChromeComponent
                Item {
                    anchors.fill: parent

                    // ---- Code-block chrome ----
                    // Plain text rows no longer instantiate this panel,
                    // language picker, gutter, or horizontal scrollbar.
                    Rectangle {
                        id: codePanel
                        objectName: "codePanel"
                        anchors.fill: parent
                        color: theme.codePanelBackground
                        radius: 4
                        border.width: 1
                        border.color: theme.border
                    }

                    Column {
                        id: codeGutter
                        objectName: "codeGutter"
                        visible: delegate.codeLineNumbers
                        width: delegate.codeGutterWidth
                        x: 0
                        y: textArea.y + textArea.topPadding
                        z: 2
                        // On the Repeater below a bare `delegate` is its own
                        // delegate component, not the block (outer ids rank
                        // below scope properties inside this inline
                        // component) — alias here, where Column has no
                        // `delegate` property.
                        readonly property int gutterLineCount:
                            delegate.codeLineNumbers ? textArea.lineCount : 0
                        Repeater {
                            model: codeGutter.gutterLineCount
                            delegate: Text {
                                required property int index
                                width: delegate.codeGutterWidth - 8
                                height: textArea.lineCount > 0
                                    ? textArea.contentHeight / textArea.lineCount : 0
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                                text: index + 1
                                color: theme.textFaint
                                font.family: delegate.contentFontFamily
                                font.pixelSize: delegate.contentFontSize
                            }
                        }
                    }
                    Rectangle {
                        visible: delegate.codeLineNumbers
                        x: 0
                        y: 0
                        width: delegate.codeGutterWidth
                        height: parent.height
                        color: theme.codePanelBackground
                        z: 1
                    }

                    Item {
                        id: codeHeader
                        objectName: "codeHeader"
                        x: 0
                        y: 0
                        z: 3
                        width: parent.width
                        height: delegate.codeHeaderHeight

                        Rectangle {
                            id: langButton
                            objectName: "codeLanguageButton"
                            height: 20
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: langLabel.implicitWidth + 20
                            radius: 3
                            color: langHover.hovered ? theme.hoverTint : "transparent"
                            Text {
                                id: langLabel
                                anchors.left: parent.left
                                anchors.leftMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: (delegate.language && delegate.language.length > 0)
                                      ? delegate.language : "plain text"
                                color: theme.textMuted
                                font.pixelSize: Math.max(10, delegate.contentFontSize - 3)
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: 5
                                anchors.verticalCenter: parent.verticalCenter
                                text: "▾"
                                color: theme.textFaint
                                font.pixelSize: 9
                            }
                            HoverHandler { id: langHover }
                            TapHandler { onTapped: languagePicker.open() }
                        }

                        Rectangle {
                            id: copyButton
                            objectName: "codeCopyButton"
                            height: 20
                            width: copyLabel.implicitWidth + 14
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 3
                            color: copyHover.hovered ? theme.hoverTint : "transparent"
                            Text {
                                id: copyLabel
                                anchors.centerIn: parent
                                text: copyButton.copied ? "Copied" : "Copy"
                                color: theme.textMuted
                                font.pixelSize: Math.max(10, delegate.contentFontSize - 3)
                            }
                            property bool copied: false
                            HoverHandler { id: copyHover }
                            TapHandler {
                                onTapped: {
                                    clipboard.text = delegate.content
                                    copyButton.copied = true
                                    copyResetTimer.restart()
                                }
                            }
                            Timer {
                                id: copyResetTimer
                                interval: 1200
                                onTriggered: copyButton.copied = false
                            }
                        }
                    }

                    // Menu has its own `delegate` property (the item chrome
                    // component), and scope-object attributes shadow document
                    // ids — so inside the Menu's instantiation an unqualified
                    // `delegate` is that Component, not the block root. Bind
                    // and connect from sibling scope, where the id resolves.
                    LanguagePicker {
                        id: languagePicker
                        objectName: "languagePicker"
                    }
                    Binding {
                        target: languagePicker
                        property: "currentLanguage"
                        value: delegate.language || ""
                    }
                    Connections {
                        target: languagePicker
                        function onLanguageChosen(lang) {
                            delegate.setCodeLanguage(lang)
                        }
                    }

                    ScrollBar {
                        id: codeHScrollBar
                        objectName: "codeHScrollBar"
                        visible: delegate.codeMaxScroll > 0
                        orientation: Qt.Horizontal
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: delegate.codeContentLeft
                        height: 8
                        z: 2
                        size: delegate.codeViewportWidth
                              / Math.max(1, textArea.contentWidth)
                        position: delegate.codeMaxScroll > 0
                            ? delegate.codeHScroll / (delegate.codeMaxScroll + delegate.codeViewportWidth)
                            : 0
                        onPositionChanged: {
                            if (pressed) {
                                delegate.codeHScroll = Math.round(
                                    position * (delegate.codeMaxScroll + delegate.codeViewportWidth))
                            }
                        }
                    }
                }
            }

            // The hybrid editing engine. When a row is
            // actively edited, the TextArea's QTextDocument is the only text
            // element: the engine fills it from the model and styles it with a
            // QSyntaxHighlighter, so block height derives from what is on
            // screen. The readOnlyText item below covers eligible unfocused
            // rows so scroll paint/layout does not instantiate the
            // highlighter path.
            // The document holds the display text (markers stripped); the
            // engine reveals a span's markers while the cursor touches it
            // (features.md §2.2) and maps user edits back to storage
            // markdown. Verbatim blocks (code) skip all of that: the
            // document text IS the markdown.
            BlockEditorEngine {
                id: editorEngine
                document: delegate.editorActive ? textArea.textDocument : null
                markdown: delegate.editableMarkdown
                cursorPosition: textArea.cursorPosition
                selectionStart: textArea.selectionStart
                selectionEnd: textArea.selectionEnd
                cursorActive: delegate.editorActive && textArea.activeFocus
                verbatim: delegate.verbatimEditing
                codeLanguage: delegate.language
                // The token source for the highlighter. Bound through the
                // delegate property: inside this object a bare `theme`
                // would resolve to the engine's own property, not the
                // context property.
                theme: delegate.appTheme
                // The internal-link resolver: the
                // DocumentOutline, so a `[text](#slug)` whose slug matches no
                // heading renders muted. Context property, no name clash with
                // the engine's own `linkResolver` property.
                linkResolver: documentOutline
                // The wiki-link resolver: with no collection open every
                // [[wiki-link]] styles as an ordinary link rather than all
                // rendering "unresolved".
                wikiResolver: noteCollection.isOpen ? noteCollection : null
                lineHeight: delegate.appTypography.lineHeight
                monoFontFamily: delegate.appTypography.monoFamily
                contentFontPixelSize: delegate.contentFontSize
                contentFontFamily: delegate.contentFontFamily
                contentFontWeight: delegate.contentFontWeight
                // Search-match tints: the revision read makes the binding
                // re-evaluate on every search change; with the bar closed
                // this is an empty list and costs nothing.
                searchMatches: delegate.editorActive
                    ? delegate.blockSearchMatches : []

                onMarkdownEdited: function(md) {
                    if (delegate.isPooled) return

                    // Captured before the model updates: the slash-menu
                    // trigger below needs the pre-edit emptiness. `md` is the
                    // editable portion (metadata tail excluded).
                    var wasEmpty = delegate.editableMarkdown === ""

                    // Markdown prefix auto-conversion. The typed text goes
                    // into the model first — it merges into the current
                    // typing command, so one Ctrl+Z after a conversion
                    // restores the literal typed prefix. The conversion
                    // itself is a single ConvertBlockCommand; the type
                    // change may recreate the delegate through the
                    // DelegateChooser, so focus is re-established by index.
                    var conv = delegate.matchBlockPrefix(md)
                    if (conv !== null) {
                        // A structural conversion ends any menu session
                        // on this block (the query context is gone)
                        var staleMenu = delegate.activeBlockMenu()
                        if (staleMenu)
                            staleMenu.dismiss()
                        var idx = delegate.index
                        var prefixLength = md.length - conv.content.length
                        var mdCursor = Math.max(0,
                            editorEngine.toMarkdownPosition(textArea.cursorPosition)
                            - prefixLength)
                        blockModel.updateContent(idx, md)
                        blockModel.convertBlock(idx, conv.type, conv.content,
                                                conv.checked === true,
                                                conv.language || "",
                                                conv.calloutTitle || "")
                        delegate.refocusBlock(idx,
                            Math.min(mdCursor, conv.content.length))
                        return
                    }

                    // Re-append the metadata tail so the chrome survives an
                    // edit of the body (decisions 10-11).
                    if (md !== delegate.editableMarkdown) {
                        blockModel.updateContent(delegate.index,
                                                 md + delegate.metaTail)
                    }

                    // The block-type menu (features.md §4.1). The query
                    // lives in the block content: while the menu targets
                    // this block, every edit feeds
                    // it; "/" landing on a previously empty non-verbatim
                    // block opens it. This hook is the user-edit path
                    // only — programmatic changes (undo, load) rebuild
                    // the document without passing through here.
                    var menu = delegate.activeBlockMenu()
                    if (menu) {
                        menu.updateQuery(md)
                    } else if (md === "/" && wasEmpty
                               && !delegate.verbatimEditing) {
                        delegate.openBlockMenu("slash")
                    }
                }
            }

            Text {
                id: readOnlyText
                objectName: "readOnlyText"
                visible: delegate.useReadOnlyText && !delegate.calloutFolded
                x: delegate.textAreaX + textArea.leftPadding
                y: delegate.textAreaY + textArea.topPadding
                width: Math.max(1, delegate.textAreaWidth
                                    - textArea.leftPadding
                                    - textArea.rightPadding)
                text: delegate.displayText
                color: delegate.contentColor
                font.pixelSize: delegate.contentFontSize
                font.weight: delegate.contentFontWeight
                font.family: delegate.contentFontFamily
                font.strikeout: delegate.contentStrikeout
                font.kerning: false
                font.preferShaping: false
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                horizontalAlignment: delegate.alignHAlign
                lineHeight: delegate.appTypography.lineHeight
                lineHeightMode: Text.ProportionalHeight
                clip: false

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: function(eventPoint) {
                        var px = eventPoint.position.x + textArea.leftPadding
                        var py = eventPoint.position.y + textArea.topPadding
                        delegate.activateEditor()
                        textArea.forceActiveFocus()
                        Qt.callLater(function() {
                            var pos = textArea.positionAt(px, py)
                            textArea.cursorPosition = Math.min(pos,
                                                               textArea.text.length)
                        })
                    }
                }
            }

            TextArea {
                id: textArea
                objectName: "blockTextArea"

                // Code blocks drop the right anchor and take an explicit
                // width so a long line (wrap off) can exceed the viewport
                // and scroll under the clip; the left margin carries both
                // the gutter inset and the horizontal scroll offset. For
                // every other block the width equals the two-anchor result,
                // so behavior is unchanged.
                anchors.left: parent.left
                anchors.leftMargin: delegate.textAreaX
                width: delegate.textAreaWidth
                // The code/callout header sits above the text via a y offset —
                // NOT a topPadding override, which would also change every
                // other block's default padding and shift its geometry. y is 0
                // for plain blocks, so their layout is byte-identical.
                y: delegate.textAreaY
                // A folded callout hides its body; the header stays visible.
                visible: !delegate.calloutFolded && delegate.editorActive

                // Text alignment (features.md §9.2). Code scrolls and
                // stays left; every other block honors the `align`
                // attribute (default left).
                horizontalAlignment: delegate.codeChrome
                    ? TextEdit.AlignLeft : delegate.alignHAlign

                // Screen-reader role/name (§14.2): an editable text field whose
                // accessible value is its content; the name labels the kind.
                Accessible.role: Accessible.EditableText
                Accessible.name: {
                    var kinds = ["Paragraph", "Heading 1", "Heading 2",
                        "Heading 3", "Bulleted list item", "Numbered list item",
                        "To-do", "Quote", "Code block", "Divider", "Heading 4"]
                    var k = kinds[delegate.blockType]
                    return (k ? k : qsTr("Text")) + qsTr(" block")
                }

                // Formatting commands (Ctrl+B / Ctrl+I) operate on storage
                // markdown through the model — never on the document text
                // directly. Selection positions are document coordinates
                // and map through the engine; the cursor is restored at
                // the equivalent markdown position afterwards.
                function toggleFormatting(applyToggle, collapsedCursorOffset) {
                    if (delegate.verbatimEditing) return
                    // Formatting across a cross-block selection is inert:
                    // applying to just the anchor block's portion would
                    // silently format a fraction of what the user
                    // selected.
                    if (documentSelection.hasTextSelection) return
                    var md = editorEngine.markdown
                    var mdStart = editorEngine.toMarkdownPosition(selectionStart)
                    var mdEnd = editorEngine.toMarkdownPosition(selectionEnd)
                    var newText = applyToggle(md, mdStart, mdEnd)
                    if (newText === md) return

                    var mdCursor
                    if (mdStart === mdEnd) {
                        // Collapsed: land between the inserted marker pair
                        mdCursor = mdStart + collapsedCursorOffset
                    } else {
                        mdCursor = mdEnd + (newText.length - md.length)
                    }
                    mdCursor = Math.max(0, Math.min(mdCursor, newText.length))

                    blockModel.updateContent(delegate.index, newText)
                    cursorPosition = Math.min(editorEngine.toDocumentPosition(mdCursor),
                                              text.length)
                }

                // One registry-driven toggle for every inline type. The
                // collapsed-cursor offset is the canonical marker length,
                // landing the cursor between the inserted empty marker
                // pair.
                function toggleSpan(typeName) {
                    toggleFormatting(function(md, s, e) {
                        return markdownFormatter.toggleSpanType(md, s, e, typeName)
                    }, markdownFormatter.getMarkerLength(typeName))
                }

                function toggleBold() { toggleSpan("bold") }
                function toggleItalic() { toggleSpan("italic") }

                // Text color (features.md §2.1). Apply
                // wraps or recolors-in-place through the same selection→
                // markdown→model→cursor machinery as every inline toggle, so
                // one Ctrl+Z reverts it. The collapsed-cursor offset is the
                // opening marker's length, landing the caret inside an empty
                // color span for the format-then-type workflow.
                function applyColorValue(value) {
                    if (!value) return
                    var openLen = ("<span style=\"color:" + value + "\">").length
                    toggleFormatting(function(md, s, e) {
                        return markdownFormatter.applyColor(md, s, e, value)
                    }, openLen)
                }
                function removeColorSpan() {
                    toggleFormatting(function(md, s, e) {
                        return markdownFormatter.removeColor(md, s, e)
                    }, 0)
                }

                // Clipboard operations (features.md §5) on the C++
                // clipboard helper. Copy puts MARKDOWN on the clipboard
                // (the engine maps the visual selection, wrapping selected
                // span content in its markers); paste inserts markdown at
                // the cursor (rendering follows from the engine), splitting
                // multi-line text into blocks; paste-plain strips markdown
                // formatting first.
                function copySelectionAsMarkdown() {
                    if (selectionEnd <= selectionStart) return false
                    var md = editorEngine.markdownForRange(selectionStart,
                                                           selectionEnd)
                    // All three flavors (§5.1): text for plain targets, HTML
                    // so rich-text targets get formatting instead of raw
                    // syntax, and the internal marker for pasting back.
                    clipboard.setMarkdown(md, markdownFormatter.toHtml(md))
                    return true
                }

                // Cut removes exactly what the copy captured (whole spans
                // when their content is fully selected), so cut+paste
                // round-trips. Plain Delete/Backspace instead keep empty
                // markers for the format-then-type workflow (§2.2.7).
                function cutSelectionAsMarkdown() {
                    if (!copySelectionAsMarkdown()) return false
                    var result = editorEngine.cutRange(selectionStart, selectionEnd)
                    blockModel.updateContent(delegate.index, result.markdown)
                    cursorPosition = Math.min(editorEngine.toDocumentPosition(result.cursor),
                                              text.length)
                    return true
                }

                function pasteFromClipboard(stripFormatting) {
                    // Image data on the clipboard becomes an image block
                    // (§5.3), even in a code block (an image is not code).
                    if (delegate.handleImagePaste())
                        return
                    if (!clipboard || !clipboard.hasText) return

                    // A lone URL over a text selection links the selection
                    // rather than replacing it (§5.3) — the common "copy a
                    // link, select the words, paste" gesture.
                    if (!stripFormatting && clipboard.hasUrl
                        && selectionEnd > selectionStart
                        && !delegate.verbatimEditing) {
                        var label = editorEngine.markdownForRange(selectionStart,
                                                                  selectionEnd)
                        if (label.length > 0 && label.indexOf("\n") < 0) {
                            var linkPos = selectionStart
                            remove(selectionStart, selectionEnd)
                            var linkMd = "[" + label + "](" + clipboard.url + ")"
                            insert(linkPos, linkMd)
                            cursorPosition = linkPos + linkMd.length
                            return
                        }
                    }

                    // §5.3 format matrix: Kvit's own payload verbatim,
                    // structured HTML converted, everything else as text.
                    // Paste-plain deliberately bypasses it — Ctrl+Shift+V
                    // means "whatever the source's plain text was".
                    var pasted = (stripFormatting ? clipboard.text
                                                  : clipboard.markdown())
                                     .replace(/\r\n/g, "\n")
                    if (stripFormatting) {
                        pasted = pasted.split("\n").map(function(line) {
                            return editorEngine.stripFormatting(line)
                        }).join("\n")
                    }
                    if (pasted.length === 0) return

                    var lines = pasted.split("\n")
                    if (lines.length === 1 || delegate.verbatimEditing) {
                        // Document-level edit; the engine maps it to
                        // markdown. Verbatim blocks keep multi-line pastes
                        // whole — newlines are content there.
                        var pos = selectionStart
                        if (selectionEnd > selectionStart) {
                            remove(selectionStart, selectionEnd)
                        }
                        insert(pos, pasted)
                        cursorPosition = pos + pasted.length
                        return
                    }

                    // Multi-line: split into blocks through the model.
                    // First line joins the text before the cursor, last
                    // line joins the text after it.
                    var mdStart = editorEngine.toMarkdownPosition(selectionStart)
                    var mdEnd = editorEngine.toMarkdownPosition(selectionEnd)
                    var md = editorEngine.markdown
                    var before = md.substring(0, mdStart)
                    var after = md.substring(mdEnd)

                    // A structured payload (Kvit's own blocks, or HTML that
                    // carried headings/lists) must be PARSED into typed
                    // blocks: splicing it in line by line would leave the
                    // markdown literal, so a pasted "## Title" would render as
                    // the characters "## Title". Flat text keeps the literal
                    // splice, which is what pasting plain lines should do.
                    if (!stripFormatting && clipboard.hasStructuredMarkdown) {
                        delegate.pasteStructuredMarkdown(delegate.index, before,
                                                         pasted, after)
                        return
                    }

                    blockModel.updateContent(delegate.index, before + lines[0])
                    var insertAt = delegate.index + 1
                    for (var i = 1; i < lines.length - 1; i++) {
                        blockModel.insertBlock(insertAt, 0, lines[i])
                        insertAt++
                    }
                    var lastLine = lines[lines.length - 1]
                    blockModel.insertBlock(insertAt, 0, lastLine + after)

                    var lastIndex = insertAt
                    var cursorMd = lastLine.length
                    Qt.callLater(function() {
                        if (listView) {
                            listView.currentIndex = lastIndex
                            var item = listView.itemAtIndex(lastIndex)
                            if (item) item.focusAtPosition(cursorMd)
                        }
                    })
                }

                // No text binding: the BlockEditorEngine owns the document
                // content (model -> engine -> document; user edits flow
                // back via editorEngine.markdownEdited).

                // Link opening (features.md §2.4): a plain click opens the
                // link under the pointer only while the block is not
                // focused (the reading state of §2.2.2 — "links show as
                // clickable text"). Once the block is being edited, plain
                // clicks just place the cursor. Ctrl+Click link opening
                // moved to the row-level selectionClickArea, which
                // resolves the §3.1/§2.4 conflict by specificity. The
                // handler observes passively, so focus and cursor
                // placement stay intact.
                // §9.5 right-click: the link menu wins by specificity
                // over the text menu (mirroring Ctrl+Click); a click
                // inside an existing selection keeps it, elsewhere the
                // caret moves to the click first. A CHILD MouseArea:
                // children receive pointer events before the TextArea,
                // which otherwise consumes right presses itself. The
                // window owns the shared Menu instances (one set, not
                // one per pooled delegate).
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onPressed: function(mouse) {
                        var win = Window.window
                        if (!win || !win.openTextContextMenu)
                            return
                        var pos = textArea.positionAt(mouse.x, mouse.y)
                        if (textArea.selectionEnd <= textArea.selectionStart
                            || pos < textArea.selectionStart
                            || pos > textArea.selectionEnd)
                            textArea.cursorPosition = pos
                        textArea.forceActiveFocus()
                        var url = delegate.verbatimEditing ? ""
                            : editorEngine.linkAtDocumentPosition(pos)
                        if (url !== "")
                            win.openLinkContextMenu(delegate)
                        else
                            win.openTextContextMenu(delegate)
                    }
                }

                TapHandler {
                    acceptedModifiers: Qt.NoModifier
                    property bool wasFocusedAtPress: false
                    onPressedChanged: {
                        if (pressed)
                            wasFocusedAtPress = textArea.activeFocus
                    }
                    onTapped: function(eventPoint) {
                        if (wasFocusedAtPress)
                            return
                        var pos = textArea.positionAt(eventPoint.position.x,
                                                      eventPoint.position.y)
                        delegate.openLinkAt(pos)
                    }
                }

                font.pixelSize: delegate.contentFontSize
                font.weight: delegate.contentFontWeight
                font.family: delegate.contentFontFamily
                font.strikeout: delegate.contentStrikeout
                color: delegate.contentColor

                textFormat: TextEdit.PlainText

                background: Rectangle {
                    // Code uses the fixed codePanel behind contentArea so the
                    // panel does not scroll with a long line; other panelled
                    // blocks keep their own background.
                    visible: delegate.showPanel && !delegate.codeChrome
                    color: theme.codePanelBackground
                    radius: 4
                }

                readOnly: delegate.useReadOnlyText
                wrapMode: delegate.codeChrome ? TextEdit.NoWrap : TextEdit.Wrap

                // Keep the caret in view as it moves along a long line, and —
                // in §16.2 typewriter mode — recenter the caret line in the
                // editor viewport (cheap no-op unless the mode is on and this
                // block holds focus).
                onCursorRectangleChanged: {
                    if (delegate.codeChrome) {
                        var cx = cursorRectangle.x
                        if (cx - delegate.codeHScroll > delegate.codeViewportWidth - 12)
                            delegate.codeHScroll = Math.min(delegate.codeMaxScroll,
                                cx - delegate.codeViewportWidth + 24)
                        else if (cx - delegate.codeHScroll < 0)
                            delegate.codeHScroll = Math.max(0, cx - 4)
                    }
                    var win = Window.window
                    if (win && win.typewriterMode !== undefined
                        && win.typewriterMode && textArea.activeFocus
                        && !delegate.isPooled)
                        win.centerCaretLine(delegate)
                    // A caret move can reveal/hide a math span and shifts
                    // rectangles; reposition the inline-math overlays.
                    delegate.mathTick++
                }
                onContentHeightChanged: delegate.mathTick++
                onContentWidthChanged: delegate.mathTick++

                // Cross-block portions must stay visible on unfocused
                // blocks; the focus-loss deselect in
                // onIsFocusedChanged keeps single-block behavior as it
                // was before this property.
                persistentSelection: true

                // Engine-driven document edits (reveal transitions,
                // rebuilds) destroy an applied portion selection;
                // re-assert it once the stack is clean. Safe against
                // feedback: applyTextPortion is a fixed point when the
                // selection already matches.
                onTextChanged: {
                    if (documentSelection.hasTextSelection && !activeFocus)
                        delegate.applyTextPortionLater()
                    if (delegate.activeMathMenu()
                        || delegate.dollarPairOpenPos >= 0)
                        Qt.callLater(settleMathEntryState)
                    if (delegate.activeWikiMenu())
                        Qt.callLater(syncWikiMenuQuery)
                }

                // Math-entry state follows the caret: the menu query is
                // the backslash-word at the caret, and the $-pair
                // tracking ends when the caret leaves the pair. Always
                // DEFERRED: during an edit this TextArea's caret signal
                // arrives before the text property reflects the same
                // edit, so an immediate read sees an inconsistent
                // snapshot and would mis-dismiss.
                onCursorPositionChanged: {
                    if (delegate.activeMathMenu()
                        || delegate.dollarPairOpenPos >= 0)
                        Qt.callLater(settleMathEntryState)
                    if (delegate.activeWikiMenu())
                        Qt.callLater(syncWikiMenuQuery)
                }

                function settleMathEntryState() {
                    syncMathMenuQuery()
                    if (delegate.dollarPairOpenPos >= 0
                        && delegate.dollarPairClosePos() < 0)
                        delegate.dollarPairOpenPos = -1
                }

                onActiveFocusChanged: {
                    if (!activeFocus) {
                        delegate.dollarPairOpenPos = -1
                        delegate.mathSlotChain = false
                        var mathMenu = delegate.activeMathMenu()
                        if (mathMenu)
                            mathMenu.dismiss()
                        var wikiMenu = delegate.activeWikiMenu()
                        if (wikiMenu)
                            wikiMenu.dismiss()
                    }
                }

                // The backslash-word ending at the caret — {trigger,
                // query} or null. Derived from content rather than a
                // stored position: reveal transitions rewrite the
                // document (the span materializing around the fresh
                // "$\a$" bounces it), so any saved document offset goes
                // stale mid-word; the content at the caret never does.
                function mathWordAtCaret() {
                    var pos = cursorPosition
                    // TeX control symbols are one non-letter character.
                    // These five are the symbol-style commands in the menu.
                    if (pos >= 2 && text.charAt(pos - 2) === "\\"
                        && /^[|,;:!]$/.test(text.charAt(pos - 1))) {
                        return { trigger: pos - 2,
                                 query: text.charAt(pos - 1) }
                    }
                    var s = pos
                    while (s > 0 && /[A-Za-z]/.test(text.charAt(s - 1)))
                        s--
                    if (s === 0 || text.charAt(s - 1) !== "\\")
                        return null
                    // A second backslash right after the trigger is the
                    // "\\" (row break) query.
                    if (s === pos && s > 1 && text.charAt(s - 2) === "\\")
                        return { trigger: s - 2, query: "\\" }
                    return { trigger: s - 1, query: text.substring(s, pos) }
                }

                // Recompute the math-menu query; when the caret no longer
                // ends a backslash-word (trigger deleted, caret moved
                // away, non-letter typed) the menu closes with the text
                // kept.
                function syncMathMenuQuery() {
                    var menu = delegate.activeMathMenu()
                    if (!menu)
                        return
                    var word = mathWordAtCaret()
                    if (!word) {
                        menu.dismiss()
                        return
                    }
                    menu.updateQuery(word.query)
                }

                // The "[[…" run ending at the caret — {trigger, query} or
                // null. Content-derived, like mathWordAtCaret: reveal
                // transitions rewrite the document, so stored offsets go
                // stale but the text at the caret never does (§3.5).
                function wikiWordAtCaret() {
                    var pos = cursorPosition
                    var open = text.lastIndexOf("[[", Math.max(0, pos - 2))
                    if (open < 0 || open + 2 > pos)
                        return null
                    var between = text.substring(open + 2, pos)
                    if (between.indexOf("]]") >= 0
                        || between.indexOf("[") >= 0
                        || between.indexOf("\n") >= 0)
                        return null
                    return { trigger: open, query: between }
                }

                // Recompute the wiki-menu query; when the caret no longer
                // sits in an open "[[…" run (trigger deleted, "]]" typed,
                // caret moved away) the menu closes with the text kept.
                function syncWikiMenuQuery() {
                    var menu = delegate.activeWikiMenu()
                    if (!menu)
                        return
                    var word = wikiWordAtCaret()
                    if (!word) {
                        menu.dismiss()
                        return
                    }
                    menu.updateQuery(word.query)
                }

                // Insertion (the wiki menu hands the chosen row here):
                // replace the whole "[[…" run with the completed link,
                // caret after the closing "]]".
                function applyWikiCompletion(row) {
                    var word = wikiWordAtCaret()
                    if (!word)
                        return
                    var insertText
                    if (row.kind === "heading") {
                        var hashIdx = word.query.indexOf("#")
                        var targetPart = hashIdx >= 0
                            ? word.query.substring(0, hashIdx) : ""
                        insertText = "[[" + targetPart + "#"
                                     + row.heading + "]]"
                    } else {
                        insertText = "[[" + row.target + "]]"
                    }
                    var start = word.trigger
                    var end = cursorPosition
                    remove(start, end)
                    insert(start, insertText)
                    cursorPosition = start + insertText.length
                    forceActiveFocus()
                }

                // Insertion (the math menu hands the chosen row here):
                // replace the backslash-word at the caret with the
                // template, caret into the first slot, arm the Tab chain.
                // Inline context always takes the single-line form.
                function applyMathCommand(row) {
                    var insertText = row.insert
                    var offset = row.cursorOffset
                    var word = mathWordAtCaret()
                    var start = word ? word.trigger : cursorPosition
                    var end = cursorPosition
                    // A bare command fuses with a following letter
                    // (\alphax); pad with a space.
                    if (offset < 0 && end < text.length
                        && /[A-Za-z]/.test(text.charAt(end))
                        && /[A-Za-z]$/.test(insertText))
                        insertText += " "
                    remove(start, end)
                    insert(start, insertText)
                    cursorPosition = start
                        + (offset >= 0 ? offset : insertText.length)
                    delegate.mathSlotChain = insertText.indexOf("{}") >= 0
                        || insertText.indexOf("[]") >= 0
                    forceActiveFocus()
                }

                // The next (or previous) empty {} / [] pair inside the
                // math span under the caret; false when none is left or
                // the caret left the span — the chain ends there.
                function jumpToNextMathSlot(backward) {
                    var span = editorEngine.mathSpanRangeAt(cursorPosition)
                    if (!span.found) {
                        delegate.mathSlotChain = false
                        return false
                    }
                    var from = span.docContentStart
                    var to = Math.min(span.docContentEnd, text.length)
                    var positions = []
                    for (var i = from; i + 1 < to; ++i) {
                        var two = text.substring(i, i + 2)
                        if (two === "{}" || two === "[]")
                            positions.push(i + 1)
                    }
                    if (positions.length === 0) {
                        delegate.mathSlotChain = false
                        return false
                    }
                    if (backward) {
                        for (var j = positions.length - 1; j >= 0; --j) {
                            if (positions[j] < cursorPosition) {
                                cursorPosition = positions[j]
                                return true
                            }
                        }
                        return false
                    }
                    for (var k = 0; k < positions.length; ++k) {
                        if (positions[k] > cursorPosition) {
                            cursorPosition = positions[k]
                            return true
                        }
                    }
                    delegate.mathSlotChain = false
                    return false
                }


                // Passive observer feeding the window's cross-block
                // drag coordinator (§21.3). Sits on the TextArea itself
                // because only the pressed item's own handlers see a
                // press it accepts; the passive grab then reports every
                // move — even outside this block — while the TextArea
                // keeps its exclusive grab and native selection.
                PointHandler {
                    acceptedButtons: Qt.LeftButton
                    onActiveChanged: {
                        var drag = delegate.dragCoordinator()
                        if (!drag)
                            return
                        if (active) {
                            var sp = point.scenePosition
                            drag.beginPress(delegate.index,
                                delegate.markdownPositionAt(sp.x, sp.y),
                                sp.x, sp.y)
                        } else {
                            drag.endPress()
                        }
                    }
                    onPointChanged: {
                        if (!active)
                            return
                        var drag = delegate.dragCoordinator()
                        if (drag) {
                            var sp = point.scenePosition
                            drag.update(sp.x, sp.y)
                        }
                    }
                }

                activeFocusOnPress: true

                placeholderText: delegate.placeholder
                placeholderTextColor: theme.textDisabled

                // Key handlers for Milestone 2, 3, 4, 5, and 7 features
                Keys.onPressed: function(event) {
                    // While the math command menu targets this editor it
                    // owns navigation; Enter is claimed in handleReturn.
                    // Everything else keeps typing, which feeds the query.
                    var mathMenu = delegate.activeMathMenu()
                    if (mathMenu) {
                        if (event.key === Qt.Key_Down) {
                            mathMenu.highlightNext()
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Up) {
                            mathMenu.highlightPrevious()
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Left
                            || event.key === Qt.Key_Right) {
                            var consumed = event.key === Qt.Key_Left
                                ? mathMenu.moveLeft() : mathMenu.moveRight()
                            if (consumed) {
                                event.accepted = true
                                return
                            }
                            // Completion mode: the caret moves, menu closes.
                            mathMenu.dismiss()
                            return
                        }
                        if (event.key === Qt.Key_Tab) {
                            mathMenu.applyHighlighted()
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Escape) {
                            // Closes the menu only; the editor keeps focus
                            // and the next Escape behaves as before.
                            mathMenu.dismiss()
                            event.accepted = true
                            return
                        }
                    }

                    // While the wiki-link menu targets this editor it owns
                    // navigation (§3.5); Enter is claimed in handleReturn.
                    // Everything else keeps typing, which feeds the query.
                    var wikiMenu = delegate.activeWikiMenu()
                    if (wikiMenu) {
                        if (event.key === Qt.Key_Down) {
                            wikiMenu.highlightNext()
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Up) {
                            wikiMenu.highlightPrevious()
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Tab) {
                            wikiMenu.applyHighlighted()
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Escape) {
                            wikiMenu.dismiss()
                            event.accepted = true
                            return
                        }
                    }

                    // While the block menu targets this block it owns
                    // Up/Down/Escape (features.md §4.1); Enter is claimed
                    // in handleReturn. Everything else keeps typing into
                    // the block, which is what filters the menu.
                    var blockMenu = delegate.activeBlockMenu()
                    if (blockMenu) {
                        if (event.key === Qt.Key_Down) {
                            blockMenu.highlightNext()
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Up) {
                            blockMenu.highlightPrevious()
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Escape) {
                            blockMenu.dismiss()
                            event.accepted = true
                            return
                        }
                    }

                    // Keys while this block anchors an active cross-block
                    // text selection (features.md §2.5, §21.3): Escape,
                    // arrows, Shift+arrows, Ctrl+C/X, Delete/Backspace,
                    // and typing-replaces all resolve against the range.
                    if (documentSelection.hasTextSelection
                        && documentSelection.textAnchorIndex() === delegate.index) {
                        if (delegate.handleCrossBlockKey(event))
                            return
                    }

                    // ---- Math-entry assistance ----

                    // Backslash: the command-menu trigger — inside a math
                    // context only. The gate pre-checks the span (the
                    // transitional "$\$" state is not a parsed span, so a
                    // post-insertion check would miss the primary flow) and
                    // honors the fresh $-pair, which is not a span until it
                    // has content. A second backslash right after the
                    // trigger becomes the "\\" query instead of a new one.
                    if (event.text === "\\" && !delegate.verbatimEditing) {
                        var bsPos = selectionStart
                        var inMathContext = delegate.dollarPairClosePos() >= 0
                            || editorEngine.mathSpanRangeAt(bsPos).found
                        if (mathMenu || inMathContext) {
                            if (selectionEnd > selectionStart)
                                remove(selectionStart, selectionEnd)
                            insert(bsPos, "\\")
                            cursorPosition = bsPos + 1
                            // With the menu already open the sync derives
                            // the new state (a "\\" query, or a fresh
                            // trigger elsewhere in the formula).
                            if (!mathMenu)
                                delegate.openMathMenu()
                            event.accepted = true
                            return
                        }
                        // Plain prose: a literal backslash, default typing.
                    }

                    // "[[": the wiki-link completion trigger (§3.5) —
                    // prose only (never in verbatim/code blocks), a
                    // collection open, never inside a math span. The
                    // second "[" is inserted by hand so the menu opens on
                    // a settled document.
                    if (event.text === "[" && !delegate.verbatimEditing
                        && noteCollection.isOpen
                        && !delegate.activeWikiMenu()
                        && selectionStart === selectionEnd
                        && cursorPosition > 0
                        && text.charAt(cursorPosition - 1) === "["
                        && !editorEngine.mathSpanRangeAt(
                               cursorPosition).found) {
                        var bracketPos = cursorPosition
                        insert(bracketPos, "[")
                        cursorPosition = bracketPos + 1
                        delegate.openWikiMenu()
                        event.accepted = true
                        return
                    }

                    // Ctrl+Space: re-trigger completion for the
                    // backslash-word at the caret, inside a math span.
                    if (event.key === Qt.Key_Space
                        && (event.modifiers & Qt.ControlModifier)
                        && !delegate.verbatimEditing
                        && editorEngine.mathSpanRangeAt(cursorPosition).found) {
                        var wordStart = cursorPosition
                        while (wordStart > 0
                               && /[A-Za-z]/.test(text.charAt(wordStart - 1)))
                            wordStart--
                        if (wordStart > 0
                            && text.charAt(wordStart - 1) === "\\")
                            delegate.openMathMenu()
                        event.accepted = true
                        return
                    }

                    // Dollar auto-pair for entering inline math: type-over
                    // the tracked closer, wrap a selection, or insert the
                    // pair with the caret between — each gated by the
                    // engine's suppression rules.
                    if (event.text === "$" && !delegate.verbatimEditing) {
                        var closePos = delegate.dollarPairClosePos()
                        if (closePos >= 0 && closePos === cursorPosition
                            && selectionStart === selectionEnd) {
                            // Types over the auto-inserted $ instead of
                            // inserting a third: "$x$" typed in full.
                            delegate.dollarPairOpenPos = -1
                            cursorPosition = closePos + 1
                            event.accepted = true
                            return
                        }
                        if (selectionEnd > selectionStart) {
                            if (editorEngine.shouldAutoPairDollar(
                                    selectionStart, true)) {
                                var selStart = selectionStart
                                var selEnd = selectionEnd
                                var wrapped = "$"
                                    + text.substring(selStart, selEnd) + "$"
                                remove(selStart, selEnd)
                                insert(selStart, wrapped)
                                cursorPosition = selStart + wrapped.length
                                event.accepted = true
                                return
                            }
                            // Suppressed: default replace-selection typing.
                        } else if (editorEngine.shouldAutoPairDollar(
                                       cursorPosition)) {
                            var pairPos = cursorPosition
                            insert(pairPos, "$$")
                            cursorPosition = pairPos + 1
                            delegate.dollarPairOpenPos = pairPos
                            event.accepted = true
                            return
                        }
                        // Suppressed: a literal $, default typing.
                    }

                    // Backspace on the still-empty pair removes both
                    // dollars — as if the keystroke never happened.
                    if (event.key === Qt.Key_Backspace
                        && selectionStart === selectionEnd
                        && delegate.dollarPairOpenPos >= 0
                        && cursorPosition === delegate.dollarPairOpenPos + 1
                        && delegate.dollarPairClosePos()
                           === delegate.dollarPairOpenPos + 1) {
                        var openPos = delegate.dollarPairOpenPos
                        delegate.dollarPairOpenPos = -1
                        remove(openPos, openPos + 2)
                        event.accepted = true
                        return
                    }

                    // Delete just before the tracked closer removes only
                    // the auto-inserted $, leaving a literal dollar sign —
                    // the escape hatch for typing $ as a character.
                    if (event.key === Qt.Key_Delete
                        && selectionStart === selectionEnd
                        && delegate.dollarPairOpenPos >= 0) {
                        var closer = delegate.dollarPairClosePos()
                        if (closer >= 0 && closer === cursorPosition) {
                            delegate.dollarPairOpenPos = -1
                            remove(closer, closer + 1)
                            event.accepted = true
                            return
                        }
                    }

                    // Tab slot-chain: hop between the empty {} / [] pairs
                    // a template insertion left in the math span. Runs
                    // before the list-indent Tab below.
                    if (delegate.mathSlotChain && event.key === Qt.Key_Tab
                        && !(event.modifiers & Qt.ControlModifier)) {
                        if (jumpToNextMathSlot(false)) {
                            event.accepted = true
                            return
                        }
                    }
                    if (delegate.mathSlotChain && event.key === Qt.Key_Backtab) {
                        if (jumpToNextMathSlot(true)) {
                            event.accepted = true
                            return
                        }
                    }

                    // Escape drops an in-block selection (which also
                    // dismisses the formatting bar).
                    if (event.key === Qt.Key_Escape
                        && textArea.selectionEnd > textArea.selectionStart) {
                        textArea.deselect()
                        event.accepted = true
                        return
                    }

                    // Shift+Arrow at a block edge extends the selection
                    // into the neighbor block, engaging the cross-block
                    // coordinator; within the block it stays native.
                    if ((event.modifiers & Qt.ShiftModifier)
                        && !(event.modifiers & Qt.ControlModifier)
                        && !(event.modifiers & Qt.AltModifier)
                        && !documentSelection.hasTextSelection) {
                        var crossIdx = -1
                        var crossMd = 0
                        if (event.key === Qt.Key_Right
                            && cursorPosition >= text.length
                            && delegate.index < blockModel.count - 1) {
                            crossIdx = delegate.index + 1
                            crossMd = 0
                        } else if (event.key === Qt.Key_Left
                                   && cursorPosition === 0 && delegate.index > 0) {
                            crossIdx = delegate.index - 1
                            crossMd = blockModel.getContent(crossIdx).length
                        } else if (event.key === Qt.Key_Down
                                   && isCursorOnLastLine()
                                   && delegate.index < blockModel.count - 1) {
                            crossIdx = delegate.index + 1
                            var below = listView ? listView.itemAtIndex(crossIdx) : null
                            crossMd = below && below.entryPositionAtX
                                ? below.entryPositionAtX(
                                      positionToRectangle(cursorPosition).x, true)
                                : 0
                        } else if (event.key === Qt.Key_Up
                                   && isCursorOnFirstLine() && delegate.index > 0) {
                            crossIdx = delegate.index - 1
                            var above = listView ? listView.itemAtIndex(crossIdx) : null
                            crossMd = above && above.entryPositionAtX
                                ? above.entryPositionAtX(
                                      positionToRectangle(cursorPosition).x, false)
                                : blockModel.getContent(crossIdx).length
                        }
                        if (crossIdx >= 0) {
                            // The anchor is the far end of any native
                            // selection, else the cursor
                            var anchorDoc = selectionEnd > selectionStart
                                ? (cursorPosition === selectionEnd
                                       ? selectionStart : selectionEnd)
                                : cursorPosition
                            documentSelection.beginTextSelection(delegate.index,
                                editorEngine.toMarkdownPosition(anchorDoc), 0)
                            documentSelection.updateTextSelectionHead(crossIdx, crossMd)
                            event.accepted = true
                            return
                        }
                    }

                    // Ctrl+Shift+Up/Down (features.md §3.1): enter block
                    // selection at this block; subsequent presses extend
                    // it from the selectionKeyHandler. Must run before
                    // the Ctrl+Up/Down navigation checks, which do not
                    // exclude Shift.
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

                    // Ctrl+A ladder (features.md §2.5): first select all
                    // text in this block; a second press — or a first
                    // press in an empty block — selects every block.
                    if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                        var wholeBlockSelected = text.length === 0
                            || (selectionStart === 0 && selectionEnd === text.length)
                        if (wholeBlockSelected) {
                            documentSelection.selectAllBlocks()
                            delegate.focusSelectionHandler()
                        } else {
                            selectAll()
                        }
                        event.accepted = true
                        return
                    }

                    // Ctrl+Shift+V: Paste plain text (strip formatting)
                    if (event.key === Qt.Key_V &&
                        (event.modifiers & Qt.ControlModifier) &&
                        (event.modifiers & Qt.ShiftModifier)) {
                        pasteFromClipboard(true)
                        event.accepted = true
                        return
                    }

                    // Ctrl+V: Paste markdown at cursor (multi-line splits
                    // into blocks)
                    if (event.key === Qt.Key_V && (event.modifiers & Qt.ControlModifier)) {
                        pasteFromClipboard(false)
                        event.accepted = true
                        return
                    }

                    // Ctrl+C: Copy selection as markdown
                    if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier)) {
                        if (copySelectionAsMarkdown()) {
                            event.accepted = true
                        }
                        return
                    }

                    // Ctrl+X: Cut selection as markdown
                    if (event.key === Qt.Key_X && (event.modifiers & Qt.ControlModifier)) {
                        if (cutSelectionAsMarkdown()) {
                            event.accepted = true
                        }
                        return
                    }

                    // Undo/Redo shortcuts - intercept before TextArea's built-in undo
                    if (event.key === Qt.Key_Z && (event.modifiers & Qt.ControlModifier)) {
                        if (event.modifiers & Qt.ShiftModifier) {
                            // Ctrl+Shift+Z: Redo
                            if (undoStack && undoStack.canRedo) {
                                undoStack.redo()
                            }
                        } else {
                            // Ctrl+Z: Undo
                            if (undoStack && undoStack.canUndo) {
                                undoStack.undo()
                            }
                        }
                        event.accepted = true
                        return
                    }

                    // Ctrl+Y: Redo
                    if (event.key === Qt.Key_Y && (event.modifiers & Qt.ControlModifier)) {
                        if (undoStack && undoStack.canRedo) {
                            undoStack.redo()
                        }
                        event.accepted = true
                        return
                    }

                    // Block type conversion shortcuts
                    // Ctrl+0: Convert to Paragraph
                    if (event.key === Qt.Key_0 && (event.modifiers & Qt.ControlModifier)) {
                        delegate.convertBlockType(0)
                        event.accepted = true
                        return
                    }

                    // Ctrl+1: Convert to Heading1
                    if (event.key === Qt.Key_1 && (event.modifiers & Qt.ControlModifier)) {
                        delegate.convertBlockType(1)
                        event.accepted = true
                        return
                    }

                    // Ctrl+2: Convert to Heading2
                    if (event.key === Qt.Key_2 && (event.modifiers & Qt.ControlModifier)) {
                        delegate.convertBlockType(2)
                        event.accepted = true
                        return
                    }

                    // Ctrl+3: Convert to Heading3
                    if (event.key === Qt.Key_3 && (event.modifiers & Qt.ControlModifier)) {
                        delegate.convertBlockType(3)
                        event.accepted = true
                        return
                    }

                    // Ctrl+Shift+D: delete this block (§13.3) — checked
                    // before plain Ctrl+D, which must exclude Shift
                    if (event.key === Qt.Key_D &&
                        (event.modifiers & Qt.ControlModifier) &&
                        (event.modifiers & Qt.ShiftModifier)) {
                        var delIdx = delegate.index
                        blockModel.removeBlocks([delIdx])
                        delegate.refocusBlock(
                            Math.min(delIdx, blockModel.count - 1), 0)
                        event.accepted = true
                        return
                    }

                    // Ctrl+D: duplicate this block below, cursor into
                    // the clone at the same position (§3.6, §13.3)
                    if (event.key === Qt.Key_D && (event.modifiers & Qt.ControlModifier)) {
                        var dupPos = editorEngine.toMarkdownPosition(cursorPosition)
                        blockModel.duplicateBlocks([delegate.index])
                        delegate.refocusBlock(delegate.index + 1, dupPos)
                        event.accepted = true
                        return
                    }

                    // Ctrl+B: Toggle bold formatting
                    if (event.key === Qt.Key_B && (event.modifiers & Qt.ControlModifier)) {
                        toggleBold()
                        event.accepted = true
                        return
                    }

                    // Ctrl+I: Toggle italic formatting
                    if (event.key === Qt.Key_I && (event.modifiers & Qt.ControlModifier)) {
                        toggleItalic()
                        event.accepted = true
                        return
                    }

                    // Ctrl+Shift+S: Toggle strikethrough (features.md §13)
                    if (event.key === Qt.Key_S &&
                        (event.modifiers & Qt.ControlModifier) &&
                        (event.modifiers & Qt.ShiftModifier)) {
                        toggleSpan("strike")
                        event.accepted = true
                        return
                    }

                    // Ctrl+Shift+T: Convert to quote (§13.3) — before the
                    // plain Ctrl+T check, which must exclude Shift
                    if (event.key === Qt.Key_T &&
                        (event.modifiers & Qt.ControlModifier) &&
                        (event.modifiers & Qt.ShiftModifier)) {
                        delegate.convertBlockType(Block.Quote)
                        event.accepted = true
                        return
                    }

                    // Ctrl+T: Convert to todo (§13.3)
                    if (event.key === Qt.Key_T && (event.modifiers & Qt.ControlModifier)) {
                        delegate.convertBlockType(Block.Todo)
                        event.accepted = true
                        return
                    }

                    // Tab / Shift+Tab: indent and outdent list items
                    // (§3.3). Clamping (depth 4, one below the previous
                    // list block) lives in the model. Code blocks keep
                    // Tab as literal whitespace input; other types keep
                    // the TextArea default.
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ControlModifier)
                        && (delegate.isListFamily
                            || delegate.blockType === Block.Quote)) {
                        blockModel.changeIndent(delegate.index, 1)
                        event.accepted = true
                        return
                    }
                    if (event.key === Qt.Key_Backtab
                        && (delegate.isListFamily
                            || delegate.blockType === Block.Quote)) {
                        blockModel.changeIndent(delegate.index, -1)
                        event.accepted = true
                        return
                    }

                    // Ctrl+U: Toggle underline
                    if (event.key === Qt.Key_U && (event.modifiers & Qt.ControlModifier)) {
                        toggleSpan("underline")
                        event.accepted = true
                        return
                    }

                    // Ctrl+E: Toggle inline code
                    if (event.key === Qt.Key_E && (event.modifiers & Qt.ControlModifier)) {
                        toggleSpan("code")
                        event.accepted = true
                        return
                    }

                    // Ctrl+K: Insert or edit a link (features.md §2.4)
                    if (event.key === Qt.Key_K && (event.modifiers & Qt.ControlModifier)) {
                        delegate.openLinkDialog()
                        event.accepted = true
                        return
                    }

                    // Ctrl+Home: Move to start of block
                    if (event.key === Qt.Key_Home && (event.modifiers & Qt.ControlModifier)) {
                        cursorPosition = 0
                        event.accepted = true
                        return
                    }

                    // Ctrl+End: Move to end of block
                    if (event.key === Qt.Key_End && (event.modifiers & Qt.ControlModifier)) {
                        cursorPosition = text.length
                        event.accepted = true
                        return
                    }

                    // Ctrl+Up: Jump to previous block
                    if (event.key === Qt.Key_Up && (event.modifiers & Qt.ControlModifier)) {
                        if (delegate.index > 0 && listView) {
                            var prevIndex = delegate.index - 1
                            listView.currentIndex = prevIndex
                            var item = listView.itemAtIndex(prevIndex)
                            if (item) item.focusAtStart()
                            event.accepted = true
                        }
                        return
                    }

                    // Ctrl+Down: Jump to next block
                    if (event.key === Qt.Key_Down && (event.modifiers & Qt.ControlModifier)) {
                        if (delegate.index < blockModel.count - 1 && listView) {
                            var nextIndex = delegate.index + 1
                            listView.currentIndex = nextIndex
                            var item = listView.itemAtIndex(nextIndex)
                            if (item) item.focusAtStart()
                            event.accepted = true
                        }
                        return
                    }

                    // Alt+Up: Move block up
                    if (event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier)) {
                        if (delegate.index > 0) {
                            moveBlockUp()
                        }
                        event.accepted = true
                        return
                    }

                    // Alt+Down: Move block down
                    if (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)) {
                        if (delegate.index < blockModel.count - 1) {
                            moveBlockDown()
                        }
                        event.accepted = true
                        return
                    }

                    // Up arrow: Move to previous block if on first line
                    if (event.key === Qt.Key_Up && !(event.modifiers & Qt.ControlModifier)) {
                        if (isCursorOnFirstLine() && delegate.index > 0 && listView) {
                            var prevIndex = delegate.index - 1
                            listView.currentIndex = prevIndex
                            var item = listView.itemAtIndex(prevIndex)
                            if (item) item.focusAtEnd()
                            event.accepted = true
                        }
                        return
                    }

                    // Down arrow: Move to next block if on last line
                    if (event.key === Qt.Key_Down && !(event.modifiers & Qt.ControlModifier)) {
                        if (isCursorOnLastLine() && delegate.index < blockModel.count - 1 && listView) {
                            var nextIndex = delegate.index + 1
                            listView.currentIndex = nextIndex
                            var item = listView.itemAtIndex(nextIndex)
                            if (item) item.focusAtStart()
                            event.accepted = true
                        }
                        return
                    }

                    // Backspace at the start of a block. Structural
                    // blocks un-structure first: outdent one level if
                    // indented, else become a paragraph keeping the
                    // content. Paragraphs and headings keep the old
                    // behavior: delete when empty, merge into the previous
                    // block otherwise.
                    if (event.key === Qt.Key_Backspace) {
                        if (cursorPosition === 0 && selectionStart === selectionEnd
                            && delegate.isStructural) {
                            if (delegate.indentLevel > 0) {
                                blockModel.changeIndent(delegate.index, -1)
                            } else {
                                delegate.unstructureToParagraph()
                            }
                            event.accepted = true
                            return
                        }
                        if (cursorPosition === 0 && delegate.index > 0) {
                            if (text === "") {
                                deleteCurrentBlock()
                            } else {
                                mergeWithPreviousBlock()
                            }
                            event.accepted = true
                            return
                        }
                    }

                    // Delete: Merge with next block at end of text
                    if (event.key === Qt.Key_Delete) {
                        if (cursorPosition >= text.length && delegate.index < blockModel.count - 1) {
                            mergeWithNextBlock()
                            event.accepted = true
                            return
                        }
                    }
                }

                // Enter/Return semantics per type:
                //  - Ctrl+Enter toggles a todo (features.md §1.2.3)
                //  - code blocks: newline into the block; on a trailing
                //    empty line, exit to a new paragraph
                //  - empty list items and quotes exit to a paragraph
                //    (§1.2.4 "exits list mode")
                //  - otherwise: continue/split (split inherits type and
                //    indent; continuation type comes from createBlockBelow)
                function handleReturn(event) {
                    // Enter selects the highlighted entry while the math
                    // command menu targets this editor.
                    var mathMenu = delegate.activeMathMenu()
                    if (mathMenu) {
                        mathMenu.applyHighlighted()
                        event.accepted = true
                        return
                    }

                    // Enter completes the highlighted note/heading while
                    // the wiki-link menu targets this editor (§3.5). With
                    // no match the menu just closes and Enter splits as
                    // usual — the typed link is already valid text.
                    var wikiMenu = delegate.activeWikiMenu()
                    if (wikiMenu) {
                        if (wikiMenu.highlightIndex >= 0) {
                            wikiMenu.applyHighlighted()
                        } else {
                            wikiMenu.dismiss()
                        }
                        event.accepted = true
                        return
                    }

                    // Enter selects the highlighted menu entry while the
                    // block menu targets this block (features.md §4.1)
                    var blockMenu = delegate.activeBlockMenu()
                    if (blockMenu) {
                        blockMenu.applyHighlighted()
                        event.accepted = true
                        return
                    }

                    // Enter replaces a cross-block selection: the range
                    // collapses (one undo step), then the block splits
                    // at the landing cursor
                    if (documentSelection.hasTextSelection
                        && documentSelection.textAnchorIndex() === delegate.index) {
                        var repl = delegate.crossBlockDeleteRange()
                        if (repl.index !== undefined) {
                            var splitIdx = repl.index
                            blockModel.splitBlock(splitIdx, repl.cursor)
                            delegate.refocusBlock(splitIdx + 1, 0)
                        }
                        event.accepted = true
                        return
                    }

                    if ((event.modifiers & Qt.ControlModifier)
                        && delegate.blockType === Block.Todo) {
                        blockModel.setChecked(delegate.index, !delegate.checked)
                        event.accepted = true
                        return
                    }

                    // Ctrl+Enter flips a callout's fold state,
                    // mirroring the todo toggle.
                    if ((event.modifiers & Qt.ControlModifier)
                        && delegate.calloutMode) {
                        delegate.toggleCalloutFold()
                        event.accepted = true
                        return
                    }

                    if (delegate.enterInsertsNewline) {
                        var atEnd = cursorPosition >= text.length
                        var onTrailingEmptyLine = atEnd && text.length > 0
                            && text.charAt(text.length - 1) === "\n"
                        if (onTrailingEmptyLine) {
                            remove(text.length - 1, text.length)
                            createBlockBelow()
                        } else {
                            if (selectionEnd > selectionStart)
                                remove(selectionStart, selectionEnd)
                            var pos = cursorPosition
                            insert(pos, "\n")
                            cursorPosition = pos + 1
                        }
                        event.accepted = true
                        return
                    }

                    if (delegate.isStructural && text.length === 0) {
                        delegate.unstructureToParagraph()
                        event.accepted = true
                        return
                    }

                    if (cursorPosition >= text.length) {
                        createBlockBelow()
                    } else {
                        splitBlockAtCursor()
                    }
                    event.accepted = true
                }

                Keys.onReturnPressed: function(event) { handleReturn(event) }
                Keys.onEnterPressed: function(event) { handleReturn(event) }
            }

            Loader {
                id: mathOverlayLoader
                active: delegate.inlineMathBoxes.length > 0
                anchors.fill: parent
                sourceComponent: inlineMathOverlayComponent
            }
            Component {
                id: inlineMathOverlayComponent
                Item {
                    // The inline-math overlay layer: one Image
                    // per hidden $…$ span, created only for rows that actually
                    // contain active inline math.
                    id: mathOverlayLayer
                    objectName: "mathOverlayLayer"
                    anchors.fill: parent
                    z: 3

                    // Inside this inline component the outer id `delegate`
                    // reaches bindings only through the creation context,
                    // AFTER the scope object's own properties — so on the
                    // Repeater below, a bare `delegate` is the Repeater's
                    // delegate component, not the block. Alias the boxes here
                    // (Item has no `delegate` property) and bind to the alias.
                    readonly property var overlayBoxes: delegate.inlineMathBoxes

                    FontMetrics {
                        id: inlineMathFontMetrics
                        font: textArea.font
                    }

                    Repeater {
                        model: mathOverlayLayer.overlayBoxes
                        Image {
                            id: mathImg
                            objectName: "inlineMathImage"
                            required property var modelData
                            function boxFor(entry) {
                                var r1 = textArea.positionToRectangle(entry.docStart)
                                var r2 = textArea.positionToRectangle(entry.docEnd)
                                var sameLine = Math.abs(r2.y - r1.y) < 2
                                var w = sameLine ? Math.max(2, r2.x - r1.x)
                                                 : Math.max(2, r1.width)
                                return Qt.rect(r1.x, r1.y, w, r1.height)
                            }
                            function reservationAscentFor(entry) {
                                return entry.reservationValid
                                    && entry.reservationAscent > 0
                                    && entry.reservationHeight > 0
                                    ? entry.reservationAscent
                                    : inlineMathFontMetrics.ascent
                            }
                            function lineBaselineY() {
                                var t = delegate.mathTick  // reposition on change
                                var lineStart = modelData.lineStart !== undefined
                                    ? modelData.lineStart : modelData.docStart
                                var baseline = box.y + reservationAscentFor(modelData)
                                for (var i = 0; i < delegate.inlineMathBoxes.length; ++i) {
                                    var entry = delegate.inlineMathBoxes[i]
                                    var entryLineStart = entry.lineStart !== undefined
                                        ? entry.lineStart : entry.docStart
                                    if (entryLineStart !== lineStart)
                                        continue
                                    var r = boxFor(entry)
                                    baseline = Math.max(baseline,
                                                        r.y + reservationAscentFor(entry))
                                }
                                return baseline
                            }
                            property rect box: {
                                var t = delegate.mathTick  // reposition on change
                                return boxFor(modelData)
                            }
                            property bool metricsValid: modelData.valid
                                && modelData.width > 0
                                && modelData.height > 0
                                && modelData.baseline > 0
                            property bool reservationValid: modelData.reservationValid
                                && modelData.reservationAscent > 0
                                && modelData.reservationHeight > 0
                            property real mathVerticalPadding:
                                modelData.inlineVerticalPadding !== undefined
                                ? modelData.inlineVerticalPadding
                                : delegate.inlineMathVerticalPadding
                            property real measuredWidth: metricsValid
                                ? modelData.width : box.width
                            property real measuredHeight: metricsValid
                                ? modelData.height
                                  + 2 * mathVerticalPadding
                                : box.height
                            property real mathBaseline: metricsValid
                                ? modelData.baseline
                                  + mathVerticalPadding
                                : measuredHeight
                            property real lineBaseline: textArea.y + lineBaselineY()
                            x: textArea.x + box.x
                            y: lineBaseline - mathBaseline
                            width: measuredWidth
                            height: measuredHeight
                            fillMode: Image.PreserveAspectFit
                            horizontalAlignment: Image.AlignLeft
                            verticalAlignment: Image.AlignTop
                            smooth: true
                            source: delegate.inlineMathSource(modelData.tex)
                        }
                    }
                }
            }
        }
    }

    // Row-level press handling for the block-selection gestures
    // (features.md §3.1). Sits above the whole row and accepts ONLY the
    // presses it owns:
    //  - Ctrl+Click over a link opens it (§2.4 wins by specificity);
    //    Ctrl+Click anywhere else toggles this block in the selection.
    //  - Shift+Click while another block is being edited selects the
    //    block range from the current block to this one; Shift+Click
    //    inside the focused block stays native text extension.
    // Every other press is rejected (after clearing any document-level
    // selection) so the TextArea, plus-button, and handle paths are
    // untouched.
    MouseArea {
        id: selectionClickArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: function(mouse) {
            var ctrl = mouse.modifiers & Qt.ControlModifier
            var shift = mouse.modifiers & Qt.ShiftModifier
            if (ctrl && !shift) {
                var p = mapToItem(textArea, mouse.x, mouse.y)
                if (p.x >= 0 && p.x <= textArea.width
                    && p.y >= 0 && p.y <= textArea.height) {
                    var url = editorEngine.linkAtDocumentPosition(
                                  textArea.positionAt(p.x, p.y))
                    if (url.length > 0) {
                        var win = Window.window
                        if (win && win.linkOpener)
                            win.linkOpener.activate(url)
                        mouse.accepted = true
                        return
                    }
                }
                documentSelection.toggleBlock(delegate.index)
                if (documentSelection.hasBlockSelection)
                    delegate.focusSelectionHandler()
                else {
                    delegate.activateEditor()
                    textArea.forceActiveFocus()
                }
                mouse.accepted = true
                return
            }
            if (shift && !ctrl && !textArea.activeFocus) {
                if (!documentSelection.hasBlockSelection) {
                    var win = Window.window
                    var anchor = win && win.lastFocusedBlock !== undefined
                            ? win.lastFocusedBlock : -1
                    if (anchor >= 0 && anchor !== delegate.index)
                        documentSelection.selectBlock(anchor)
                }
                documentSelection.extendBlockSelectionTo(delegate.index)
                delegate.focusSelectionHandler()
                mouse.accepted = true
                return
            }
            // Plain press: any document-level selection ends here; the
            // press falls through to the normal editing path. Exception:
            // the gutter of a SELECTED block keeps the selection — its
            // handle press must be able to drag the whole selection.
            var inGutter = mouse.x < 44 + delegate.indentLevel * 24
            if ((documentSelection.hasBlockSelection
                 || documentSelection.hasTextSelection)
                && !(inGutter && documentSelection.isBlockSelected(delegate.index)))
                documentSelection.clear()
            mouse.accepted = false
        }
    }
}
