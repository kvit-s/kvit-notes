// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The leading-chrome Loader's Components are separate scopes that read this
// row's own properties, so the ids they use are bound rather than injected.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Kvit 1.0

// One block of a drawn document (selection.md "A document drawn read-only").
//
// This is the read-only counterpart of qml/EditableBlock.qml, and it is a
// separate file rather than a mode of that one because almost nothing they
// share survives the change: there is no caret, no block menu, no gutter, no
// drag handle, no undo stack and no writing back to the model. What is left
// is the part worth keeping — the block's running text goes through
// BlockEditorEngine with `cursorActive` false, so the inline markers are
// hidden, the spans are styled from the theme's tokens, a wiki-link resolves
// against the open collection and a `$…$` span reserves a box that
// InlineMathOverlay paints the equation into.
//
// The row addresses no singleton: its model row arrives as properties and the
// selection it paints its share of arrives as an object, which is what lets a
// window hold several drawn documents at once, each with a selection of its
// own.
//
// The text sits in a TextArea with `enabled: false`, which does more than
// `readOnly` would. A TextArea accepts the left button whatever it intends to
// do with it, so an accepted press would never reach the sweep coordinator
// over the surface; disabling takes the item out of event delivery altogether
// and changes nothing about how it draws. That is the same reason
// qml/SelectableText.qml gives for the same decision.
Item {
    id: block

    // ---- what the surface hands the row ----

    // This row's position in the surface's document.
    required property int blockIndex
    // The surface's own selection. Every block paints the part of the range
    // that falls inside its own text, exactly as the editor's blocks do
    // through qml/CrossBlockTextSelection.qml, and the range itself is held
    // in markdown coordinates by the DocumentSelection this points at.
    required property DocumentBlockSelection selection

    // ---- the row's data, as values ----
    required property int blockType
    required property string content
    required property string language
    required property string calloutTitle
    required property int indentLevel
    required property bool checked
    required property int ordinal
    required property int fontRole

    // ---- what the row is ----

    // A divider holds no text at all, so there is nothing to lay out and
    // nothing to select inside it; it is drawn as a rule and takes part in a
    // range that crosses it as a whole block.
    readonly property bool isDivider: block.blockType === Block.Divider
    // Verbatim blocks are the ones whose content IS their text: a code fence
    // (including every fence kind built on one — kanban, toc, mermaid, query,
    // and whatever a linked module registered), a `$$` math fence, and a
    // pipe table. The engine parses no inline markup in them and every
    // position maps to itself, which is what the editor does with them too.
    readonly property bool verbatim: block.blockType === Block.CodeBlock
        || block.blockType === Block.MathBlock
        || block.blockType === Block.Table
    // A block drawn on a panel: the code-style background behind the text.
    readonly property bool panelled: block.verbatim

    readonly property int contentFontSize: {
        // sizeForRole() is invokable C++; read baseSize too so the binding
        // subscribes to typographyChanged and a live size change re-lays out.
        var baseSize = Typography.baseSize
        return Typography.sizeForRole(block.fontRole)
    }
    readonly property int contentFontWeight: {
        switch (block.blockType) {
            case Block.Heading1: return Font.Bold
            case Block.Heading2: return Font.DemiBold
            case Block.Heading3: return Font.Medium
            case Block.Heading4: return Font.Medium
            default: return Font.Normal
        }
    }
    function defaultFontFamily() {
        // Qt.application.font is documented API the type description for
        // QQmlApplication omits.
        // qmllint disable missing-property
        return Qt.application.font.family
        // qmllint enable missing-property
    }
    readonly property string contentFontFamily: block.verbatim
        ? Typography.monoFamily
        : (Typography.fontFamily !== "" ? Typography.fontFamily
                                        : block.defaultFontFamily())
    readonly property color contentColor: block.blockType === Block.Quote
        ? Theme.textSecondary : Theme.textPrimary

    // ---- geometry ----

    // Nesting inset plus the width the leading glyph occupies. Both are
    // smaller than the editor's, which reserves a gutter this surface has no
    // use for.
    readonly property int indentStep: 20
    readonly property int leadingWidth: {
        switch (block.blockType) {
            case Block.BulletList: return 16
            case Block.NumberedList: return 22
            case Block.Todo: return 22
            case Block.Quote: return (block.indentLevel + 1) * 6 + 6
            case Block.Callout: return 12
            default: return 0
        }
    }
    readonly property int textLeft: block.indentLevel * block.indentStep
                                    + block.leadingWidth

    implicitHeight: block.isDivider ? 17 : body.implicitHeight

    // ---- position mapping, the one question a sweep asks a row ----

    // The markdown offset under a scene point, clamped into this block. A
    // divider holds no text, so every point in one is offset zero and the
    // range covers it whole.
    function markdownPositionAt(sceneX, sceneY) {
        if (block.isDivider)
            return 0
        var p = body.mapFromItem(null, sceneX, sceneY)
        var cx = Math.max(0, Math.min(p.x, Math.max(1, body.width) - 1))
        var cy = Math.max(0, Math.min(p.y, Math.max(1, body.height) - 1))
        return engine.toMarkdownPosition(body.positionAt(cx, cy))
    }

    // ---- painting this block's share of the range ----

    // What the surface's selection says this row shows. Read as a binding so
    // the row re-paints on every revision bump without a Connections object
    // per block; `full` is what a divider (zero-length content) inside the
    // range answers.
    readonly property var portion: {
        if (!block.selection)
            return null
        var revision = block.selection.revision
        return block.selection.portionForBlock(block.blockIndex)
    }
    readonly property bool wholeBlockSelected:
        !!block.portion && block.portion.selected === true

    onPortionChanged: block.applyTextPortion()

    function applyTextPortion() {
        if (block.isDivider)
            return
        var p = block.portion
        if (p && p.selected === true && p.end > p.start) {
            var docStart = engine.toDocumentPosition(p.start)
            var docEnd = engine.toDocumentPosition(p.end)
            // Fixed-point guard, as in CrossBlockTextSelection: re-select
            // only when the editor does not already show the wanted range,
            // so the re-apply paths below cannot feed back indefinitely.
            if (body.selectionStart !== docStart || body.selectionEnd !== docEnd)
                body.select(docStart, docEnd)
        } else if (body.selectionEnd > body.selectionStart) {
            body.deselect()
        }
    }

    // ---- the leading glyph ----

    Loader {
        id: leading
        x: block.indentLevel * block.indentStep
        y: 0
        width: block.leadingWidth
        height: block.height
        active: block.leadingWidth > 0 && !block.isDivider
        sourceComponent: {
            switch (block.blockType) {
                case Block.BulletList: return bulletGlyph
                case Block.NumberedList: return ordinalGlyph
                case Block.Todo: return todoGlyph
                case Block.Quote: return quoteBars
                case Block.Callout: return calloutBar
                default: return null
            }
        }
    }

    Component {
        id: bulletGlyph
        Text {
            objectName: "readOnlyBullet"
            anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
            font.pixelSize: block.contentFontSize
            font.family: block.contentFontFamily
            lineHeight: Typography.lineHeight
            lineHeightMode: Text.ProportionalHeight
            color: Theme.textSecondary
            text: {
                switch (block.indentLevel % 3) {
                    case 1: return "◦"
                    case 2: return "▪"
                    default: return "•"
                }
            }
            // A bullet is not a control and has no name of its own; the
            // paragraph beside it carries the text a reader is after.
            Accessible.ignored: true
        }
    }

    Component {
        id: ordinalGlyph
        Text {
            objectName: "readOnlyOrdinal"
            anchors.right: parent ? parent.right : undefined
            anchors.rightMargin: 4
            font.pixelSize: block.contentFontSize
            font.family: block.contentFontFamily
            lineHeight: Typography.lineHeight
            lineHeightMode: Text.ProportionalHeight
            color: Theme.textSecondary
            text: block.ordinal + "."
            Accessible.ignored: true
        }
    }

    Component {
        id: todoGlyph
        Item {
            Rectangle {
                objectName: "readOnlyTodoBox"
                width: Math.round(block.contentFontSize * 0.85)
                height: width
                y: Math.round(block.contentFontSize * 0.25)
                radius: 3
                border.width: 1
                border.color: block.checked ? Theme.accent : Theme.borderStrong
                color: block.checked ? Theme.accent : "transparent"
                Text {
                    anchors.centerIn: parent
                    visible: block.checked
                    text: "✓"
                    color: Theme.onAccent
                    font.pixelSize: Math.round(parent.height * 0.8)
                    Accessible.ignored: true
                }
            }
            // The check state is spoken with the block's text, on the body
            // below, rather than by the box beside it.
            Accessible.ignored: true
        }
    }

    Component {
        id: quoteBars
        Row {
            spacing: 3
            Repeater {
                model: block.indentLevel + 1
                delegate: Rectangle {
                    objectName: "readOnlyQuoteBar"
                    width: 3
                    radius: 1.5
                    height: block.height - 4
                    color: Theme.quoteBar
                }
            }
        }
    }

    Component {
        id: calloutBar
        Rectangle {
            objectName: "readOnlyCalloutBar"
            width: 3
            radius: 1.5
            height: block.height - 4
            color: Theme.accent
        }
    }

    // ---- a divider's rule ----
    //
    // Plain, at the two pixels an unstyled divider is drawn at in the editor.
    // The stored style attributes — dashed, dotted, decorative, a width
    // fraction, a colour — are not honoured: the editor paints those on a
    // Canvas, and a preview pane is not where a divider's styling is being
    // judged.
    Rectangle {
        objectName: "readOnlyDividerRule"
        visible: block.isDivider
        x: block.indentLevel * block.indentStep
        width: Math.max(1, block.width - x)
        height: 2
        y: 8
        color: Theme.border
    }

    // ---- the selection band behind a block with no text of its own ----
    //
    // A divider inside a range has no characters to highlight, so the row
    // itself is tinted; DocumentSelection reports it as a full portion for
    // exactly this reason.
    Rectangle {
        objectName: "readOnlySelectionBand"
        visible: block.isDivider && block.wholeBlockSelected
        anchors.fill: parent
        color: Theme.selectionActiveTint
        z: -1
    }

    // ---- the running text ----

    Rectangle {
        objectName: "readOnlyBlockPanel"
        visible: block.panelled && !block.isDivider
        x: body.x
        y: body.y
        width: body.width
        height: body.height
        radius: 4
        color: Theme.codePanelBackground
        z: -1
    }

    TextArea {
        id: body
        objectName: "readOnlyBlockText"

        visible: !block.isDivider
        x: block.textLeft
        width: Math.max(1, block.width - block.textLeft)

        // Switched off, for the reason the note at the top of the file gives.
        enabled: false
        readOnly: true
        selectByMouse: false
        selectByKeyboard: false
        activeFocusOnPress: false
        // The sweep coordinator owns the selection and the keyboard is on the
        // surface, so a painted portion has to survive having no focus.
        persistentSelection: true

        padding: 0
        leftPadding: block.panelled ? 8 : 0
        rightPadding: block.panelled ? 8 : 0
        topPadding: block.panelled ? 6 : 0
        bottomPadding: block.panelled ? 6 : 0
        background: null

        color: block.contentColor
        font.pixelSize: block.contentFontSize
        font.weight: block.contentFontWeight
        font.family: block.contentFontFamily
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        // Explicit rather than palette-derived: a switched-off Control draws
        // from the palette's disabled group, where a selection is close
        // enough to the background to be invisible.
        selectionColor: Theme.selectionActiveTint
        selectedTextColor: Theme.textPrimary

        // A screen reader reads this as static text, which is what it is. The
        // editor's blocks report themselves as editable text fields, and a
        // switched-off editor would otherwise be announced as a disabled one.
        Accessible.role: Accessible.StaticText
        Accessible.name: {
            if (block.blockType === Block.Todo) {
                return (block.checked ? qsTr("Done: ") : qsTr("To do: "))
                       + body.text
            }
            if (block.blockType === Block.Callout && block.calloutTitle !== "")
                return block.calloutTitle + ". " + body.text
            return body.text
        }

        // No text binding: the engine owns the document's content.

        // Engine-driven rebuilds destroy an applied portion; re-assert it
        // once the stack is clean. Safe against feedback, because
        // applyTextPortion is a fixed point when the selection already
        // matches what the editor shows.
        onTextChanged: block.reapplyPortionLater()
        onContentHeightChanged: if (block.hasInlineMath) block.mathTick++
        onContentWidthChanged: if (block.hasInlineMath) block.mathTick++
    }

    // The deferred re-apply, as a timer this row owns rather than as a
    // Qt.callLater.
    //
    // The queued call outlives the row that made it whenever the surface is
    // reloaded from a different document or a Loader takes the whole surface
    // down: the rows go, the engine detaches its document on the way out,
    // that last text change queues one more call, and the call then runs with
    // its scope destroyed — which QML reports as "attempted to evaluate a
    // function in an invalid context" and no guard inside the callback can
    // prevent, because the callback itself is what cannot be evaluated. A
    // timer is a child of the row and stops when the row does.
    Timer {
        id: reapplyTimer
        interval: 0
        repeat: false
        onTriggered: block.applyTextPortion()
    }
    function reapplyPortionLater() { reapplyTimer.restart() }

    BlockEditorEngine {
        id: engine
        document: body.textDocument
        markdown: block.content
        // Never active: nothing here has a caret, so no span ever reveals its
        // markers and the display text is what the reader gets.
        cursorActive: false
        verbatim: block.verbatim
        codeLanguage: block.language
        theme: Theme
        // No link resolver: `[text](#slug)` slugs are headings of the OPEN
        // note, and this document is not it. With none set every internal
        // link renders as an ordinary link rather than as a wrong answer
        // about whether its target exists.
        linkResolver: null
        wikiResolver: NoteCollection.isOpen ? NoteCollection : null
        lineHeight: Typography.lineHeight
        monoFontFamily: Typography.monoFamily
        contentFontPixelSize: block.contentFontSize
        contentFontFamily: block.contentFontFamily
        contentFontWeight: block.contentFontWeight
    }

    // ---- inline equations ----

    // Optically matched to the prose x-height, at the size the engine
    // reserved the box at, exactly as the editor's blocks do.
    readonly property int inlineMathPixelSize: engine.mathFontPixelSize > 0
        ? engine.mathFontPixelSize
        : Math.max(1, block.contentFontSize)
    readonly property int inlineMathVerticalPadding:
        Math.max(2, Math.ceil(block.inlineMathPixelSize * 0.12))
    readonly property var inlineMathBoxes: {
        if (block.verbatim)
            return []
        var dep = body.text         // re-ask when the laid-out text changes
        var dep2 = block.mathTick   // and when it relayouts under the same text
        return engine.inlineMathBoxes()
    }
    readonly property bool hasInlineMath: block.inlineMathBoxes.length > 0
    // Bumped whenever the text relayouts, which moves every box without
    // changing the list.
    property int mathTick: 0
    readonly property real screenDevicePixelRatio:
        Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1

    Loader {
        active: block.hasInlineMath
        anchors.fill: parent
        sourceComponent: InlineMathOverlay {
            editor: body
            editorFont: body.font
            boxes: block.inlineMathBoxes
            tick: block.mathTick
            textColor: Theme.textPrimary
            pixelSize: block.inlineMathPixelSize
            verticalPadding: block.inlineMathVerticalPadding
            devicePixelRatio: block.screenDevicePixelRatio
        }
    }

    Component.onCompleted: block.applyTextPortion()
}
