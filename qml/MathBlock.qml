// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Kvit 1.0

// Display-math block (features.md §1.2.15). The block is a `$$ … $$` fence;
// its content is the verbatim TeX. Unfocused, it shows only the rendered
// equation, centered, through the MicroTeX image provider and themed to the
// text color, with an equation number at the right when that setting is on.
// Focused, it shows the LaTeX source in monospace
// with a debounced live-render preview beneath, and — when the expression does
// not parse — the source plus a named error rather than nothing. It keeps the
// non-text focus API of the other wave-2 blocks so navigation, selection, and
// drag stay uniform.
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
    property bool isHovered: hoverArea.hovered
    // The drag proxy grabs this instead of the whole row: a formula
    // occupies a fraction of the full-width delegate, and fitting the row
    // into the proxy's 300px shrinks it to illegibility. Falls back to the
    // row while the rendered image is not showing (editing/error states).
    readonly property Item dragGrabItem: renderedImage

    // Editing is simply "the source has focus". The debounced previewTex drives
    // the live preview so keystrokes do not re-render on every character.
    readonly property bool editing: sourceArea.activeFocus
    property string previewTex: content
    // What the rendered view/preview shows: the live (debounced) source while
    // editing, else the committed content.
    readonly property string renderTex: editing ? previewTex : content
    readonly property string errorText: mathRenderer.errorFor(renderTex)

    // previewTex starts as a binding to content, but the debounce assigns
    // it imperatively, which destroys that binding for the delegate's whole
    // pooled lifetime. Resync at every boundary — committed content changed,
    // editing ended — so a stale preview (e.g. an invalid mid-completion
    // string whose final debounce was swallowed) can never outlive the edit
    // session. Without this the stale string survived note switches via
    // delegate reuse and only a restart healed it (seen on Windows,
    // 2026-07-19).
    onContentChanged: if (!editing) previewTex = content
    onEditingChanged: if (!editing) previewTex = content
    // Display math renders at the paragraph text size, optically matched to
    // the text font's x-height — the displaystyle layout supplies the large
    // operators; the letters themselves stay at prose size like LaTeX.
    readonly property int mathPixelSize: mathRenderer.opticalMathPixelSize(
        typography.fontFamily, typography.sizeForBlockType(Block.Paragraph))
    readonly property int pngMathVerticalPadding:
        Math.max(2, Math.ceil(root.mathPixelSize * 0.12))
    readonly property bool numbered: {
        var r = appSettings.revision // re-evaluate when a setting changes
        return appSettings.value("view.equationNumbers", false) === true
    }
    readonly property int equationNumber: blockModel.mathNumber(root.index)

    // aarrggbb hex of a color, for the image://math/ query.
    function argbHex(c) {
        function h(x) { return ("0" + Math.round(x * 255).toString(16)).slice(-2) }
        return h(c.a) + h(c.r) + h(c.g) + h(c.b)
    }
    function currentDpr() {
        var win = Window.window
        if (win && win.devicePixelRatio !== undefined && win.devicePixelRatio > 0)
            return Math.round(win.devicePixelRatio * 100) / 100
        if (win && win.screen && win.screen.devicePixelRatio > 0)
            return Math.round(win.screen.devicePixelRatio * 100) / 100
        if (Screen.devicePixelRatio !== undefined && Screen.devicePixelRatio > 0)
            return Math.round(Screen.devicePixelRatio * 100) / 100
        return 1
    }
    function mathSource(tex) {
        if (tex.trim().length === 0)
            return ""
        return "image://math/" + mathRenderer.encode(tex)
             + "?fg=" + argbHex(theme.textPrimary)
             + "&size=" + root.mathPixelSize
             + "&dpr=" + root.currentDpr().toFixed(2)
             + "&vpad=" + root.pngMathVerticalPadding
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
            previewTex = content
        }
    }

    implicitHeight: contentColumn.implicitHeight + 16

    ListView.onPooled: { isPooled = true; opacity = 0 }
    ListView.onReused: {
        isPooled = false
        opacity = 1
        // A reused delegate carries the previous block's imperative
        // previewTex; re-anchor it to this block's committed content.
        previewTex = content
    }

    function focusAtStart() { sourceArea.forceActiveFocus(); sourceArea.cursorPosition = 0 }
    function focusAtEnd() { sourceArea.forceActiveFocus(); sourceArea.cursorPosition = sourceArea.length }
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

    // Leave the source editor at its visual boundary just like a prose
    // block does. Focusing the neighbor explicitly is important here: losing
    // sourceArea focus collapses this delegate from source + preview to the
    // rendered equation, so relying on Qt's implicit arrow handling can leave
    // the window with no text caret after that relayout.
    function focusAdjacentBlock(direction) {
        var targetIndex = root.index + direction
        if (!root.listView || targetIndex < 0
            || targetIndex >= blockModel.count)
            return false
        root.listView.currentIndex = targetIndex
        var target = root.listView.itemAtIndex(targetIndex)
        if (!target)
            return false
        if (direction < 0)
            target.focusAtEnd()
        else
            target.focusAtStart()
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
    // The shared block context menu's Turn-into submenu calls this on its
    // target; same fallback path as TextBlockDelegate's.
    function convertBlockType(newType) {
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
        var lv = listView
        Qt.callLater(function() {
            if (!lv) return
            lv.currentIndex = newIndex
            var item = lv.itemAtIndex(newIndex)
            if (item) { item.focusAtStart(); if (item.openBlockMenu) item.openBlockMenu("insert") }
        })
    }

    // Commit the edited source into the model, addressed by the block's stable
    // id rather than by row: this runs 250 ms after typing stopped, by which
    // time rows may have shifted or this delegate may have been pooled onto a
    // different block, making a previously captured index unsafe.
    function commitPendingSource() {
            debounce.stop()
            root.previewTex = sourceArea.text
            if (sourceArea.text !== root.content) {
                var caret = sourceArea.cursorPosition
                var keepMenu = sourceArea.activeMathMenu() !== null
                blockModel.updateContentById(root.blockId, sourceArea.text)
                // Re-applying the model-backed source can move the TextArea
                // caret before the command trigger. Restore it before query
                // synchronization so a slow typist does not lose the popup.
                sourceArea.cursorPosition = Math.min(caret, sourceArea.length)
                if (keepMenu) {
                    Qt.callLater(function() {
                        sourceArea.cursorPosition = Math.min(caret,
                                                             sourceArea.length)
                        var word = sourceArea.mathWordAtCaret()
                        if (!word)
                            return
                        if (!sourceArea.activeMathMenu())
                            sourceArea.openMathMenu(word.trigger)
                        else
                            sourceArea.syncMathMenuQuery()
                    })
                }
            }
    }

    // Debounce source edits into one preview refresh and one model write.
    Timer {
        id: debounce
        interval: 250
        onTriggered: root.commitPendingSource()
    }

    // A save, export, note switch or shutdown must see the text the user has
    // just typed, not the text as of the last time the timer happened to fire.
    Connections {
        target: documentManager
        function onPendingEditsRequested() {
            if (debounce.running)
                root.commitPendingSource()
        }
    }

    // A pooled delegate is about to become a different block; anything still
    // pending belongs to the old one and must not follow it.
    Component.onDestruction: debounce.stop()

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

        // ---- Rendered (read) view: shown when not editing ----
        Item {
            width: parent.width
            height: root.editing ? 0
                : Math.max(renderedImage.implicitHeight,
                           readError.implicitHeight, 24)
            visible: !root.editing
            clip: true

            // The equation, centered.
            Image {
                id: renderedImage
                objectName: "mathRenderedImage"
                anchors.centerIn: parent
                visible: root.errorText === "" && renderTex.trim().length > 0
                source: root.mathSource(root.renderTex)
                width: implicitWidth
                height: implicitHeight
                fillMode: Image.PreserveAspectFit
                smooth: true
                cache: false
            }
            // Empty block placeholder.
            Text {
                anchors.centerIn: parent
                visible: renderTex.trim().length === 0
                text: qsTr("Empty equation — click to edit")
                color: theme.textFaint
                font.italic: true
                font.pixelSize: 13
            }
            // Error state: source + named message, never blank.
            Column {
                id: readError
                anchors.centerIn: parent
                spacing: 2
                visible: root.errorText !== "" && renderTex.trim().length > 0
                Text {
                    text: root.renderTex
                    font.family: "monospace"; font.pixelSize: 13
                    color: theme.textPrimary; horizontalAlignment: Text.AlignHCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: "⚠ " + root.errorText
                    font.pixelSize: 11; color: theme.danger
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
            // Equation number at the right when numbering is on.
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: root.numbered && root.equationNumber > 0
                text: "(" + root.equationNumber + ")"
                color: theme.textMuted; font.pixelSize: 14
            }
            TapHandler { onTapped: root.focusAtEnd() }
        }

        // ---- Source editor: shown when editing ----
        TextArea {
            id: sourceArea
            objectName: "mathSourceArea"
            width: parent.width
            opacity: root.editing ? 1 : 0
            height: root.editing ? implicitHeight : 0
            clip: true
            text: root.content
            font.family: "monospace"
            font.pixelSize: 14
            color: theme.textPrimary
            wrapMode: TextEdit.WrapAnywhere
            selectByMouse: true
            background: Rectangle {
                color: theme.codePanelBackground
                radius: 4
                border.color: theme.border; border.width: 1
            }

            // ---- Math command menu wiring ----
            // Last resolved position of the trigger backslash. Query sync
            // re-derives it from the caret so model writes cannot stale it.
            property int mathTriggerPos: -1
            // Tab slot-chain: armed by a template insertion, Tab hops
            // to the next empty {} / [] pair until none remain or focus
            // leaves.
            property bool slotChainActive: false

            function activeMathMenu() {
                var win = Window.window
                var menu = win ? win.mathCommandMenu : null
                return (menu && menu.visible && menu.targets(sourceArea))
                        ? menu : null
            }

            function openMathMenu(triggerPos) {
                var win = Window.window
                if (!win || !win.mathCommandMenu)
                    return
                mathTriggerPos = triggerPos
                var rect = positionToRectangle(cursorPosition)
                var topLeft = sourceArea.mapToItem(null, rect.x, rect.y)
                win.mathCommandMenu.openForHost(sourceArea,
                    Qt.rect(topLeft.x, topLeft.y, rect.width, rect.height),
                    true /* display-math context */)
                syncMathMenuQuery()
            }

            // The TeX control word/symbol ending at the caret. Display math
            // used to trust mathTriggerPos, but the debounced model write can
            // re-apply the TextArea binding and invalidate that saved offset.
            function mathWordAtCaret() {
                var pos = cursorPosition
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
                if (s === pos && s > 1 && text.charAt(s - 2) === "\\")
                    return { trigger: s - 2, query: "\\" }
                return { trigger: s - 1, query: text.substring(s, pos) }
            }

            // Recompute the query from content at the caret; anything that
            // is no longer a backslash-word closes the menu with its text
            // kept (keyboard contract).
            function syncMathMenuQuery() {
                var menu = activeMathMenu()
                if (!menu)
                    return
                var word = mathWordAtCaret()
                if (!word) {
                    mathTriggerPos = -1
                    menu.dismiss()
                    return
                }
                mathTriggerPos = word.trigger
                menu.updateQuery(word.query)
            }

            // Insertion (the menu hands the chosen row here): replace the
            // trigger region with the template, caret into the first slot,
            // arm the Tab chain. Display blocks take the multi-line form.
            function applyMathCommand(row) {
                var insertText = row.insert
                var offset = row.cursorOffset
                if (row.insertDisplay && row.insertDisplay.length > 0) {
                    insertText = row.insertDisplay
                    offset = row.cursorOffsetDisplay
                }
                var word = mathWordAtCaret()
                var start = word ? word.trigger
                                 : (mathTriggerPos >= 0 ? mathTriggerPos
                                                        : cursorPosition)
                var end = Math.max(cursorPosition, start)
                mathTriggerPos = -1
                // A bare command fuses with a following letter (\alphax);
                // pad with a space.
                if (offset < 0 && end < text.length
                    && /[A-Za-z]/.test(text.charAt(end))
                    && /[A-Za-z]$/.test(insertText))
                    insertText += " "
                remove(start, end)
                insert(start, insertText)
                cursorPosition = start
                    + (offset >= 0 ? offset : insertText.length)
                slotChainActive = insertText.indexOf("{}") >= 0
                    || insertText.indexOf("[]") >= 0
                forceActiveFocus()
            }

            // The next (or previous) empty {} / [] pair in the source;
            // false when none is left — the chain ends there.
            function jumpToNextSlot(backward) {
                var positions = []
                for (var i = 0; i + 1 < text.length; ++i) {
                    var two = text.substring(i, i + 2)
                    if (two === "{}" || two === "[]")
                        positions.push(i + 1)
                }
                if (positions.length === 0) {
                    slotChainActive = false
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
                slotChainActive = false
                return false
            }

            // Re-bind to the model value when focus leaves, so an external
            // change (undo, reload) shows once we are no longer editing.
            onActiveFocusChanged: {
                if (!activeFocus) {
                    debounce.stop()
                    slotChainActive = false
                    var menu = activeMathMenu()
                    if (menu)
                        menu.dismiss()
                    mathTriggerPos = -1
                    if (text !== root.content)
                        blockModel.updateContent(root.index, text)
                    text = Qt.binding(function() { return root.content })
                }
            }
            onTextChanged: {
                if (activeFocus) debounce.restart()
                // Deferred: the caret position settles after textChanged.
                Qt.callLater(syncMathMenuQuery)
            }
            // During typing the caret signal can precede the matching text
            // update; read both from a clean event-loop turn.
            onCursorPositionChanged: Qt.callLater(syncMathMenuQuery)

            Keys.onPressed: function(event) {
                // While the menu targets this editor it owns navigation;
                // everything else keeps typing into the source, which
                // feeds the query.
                var menu = activeMathMenu()
                if (menu) {
                    if (event.key === Qt.Key_Down) {
                        menu.highlightNext(); event.accepted = true; return
                    }
                    if (event.key === Qt.Key_Up) {
                        menu.highlightPrevious(); event.accepted = true; return
                    }
                    if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                        var consumed = event.key === Qt.Key_Left
                            ? menu.moveLeft() : menu.moveRight()
                        if (consumed) { event.accepted = true; return }
                        menu.dismiss()  // completion mode: caret moves, menu closes
                        return
                    }
                    if (event.key === Qt.Key_Tab || event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter) {
                        menu.applyHighlighted(); event.accepted = true; return
                    }
                    if (event.key === Qt.Key_Escape) {
                        // First Escape closes the menu only; the next one
                        // falls through to the editor-exit binding below.
                        sourceArea.mathTriggerPos = -1
                        menu.dismiss(); event.accepted = true; return
                    }
                }

                // Backslash: the menu trigger. A second backslash right
                // after the trigger becomes the "\\" query (row break)
                // instead of a new trigger.
                if (event.text === "\\") {
                    var pos = selectionStart
                    var word = menu ? mathWordAtCaret() : null
                    if (selectionEnd > selectionStart)
                        remove(selectionStart, selectionEnd)
                    if (menu && word && word.query === ""
                        && pos === word.trigger + 1) {
                        insert(pos, "\\")
                        cursorPosition = pos + 1
                    } else {
                        insert(pos, "\\")
                        cursorPosition = pos + 1
                        openMathMenu(pos)
                    }
                    event.accepted = true
                    return
                }

                // Ctrl+Space: re-trigger completion for the backslash-word
                // at the caret.
                if (event.key === Qt.Key_Space
                    && (event.modifiers & Qt.ControlModifier)) {
                    var s = cursorPosition
                    while (s > 0 && /[A-Za-z]/.test(text.charAt(s - 1)))
                        s--
                    if (s > 0 && text.charAt(s - 1) === "\\")
                        openMathMenu(s - 1)
                    event.accepted = true
                    return
                }

                // Tab slot-chain: hop between the empty {} / [] pairs a
                // template insertion left behind.
                if (slotChainActive && event.key === Qt.Key_Tab) {
                    if (jumpToNextSlot(false)) { event.accepted = true; return }
                }
                if (slotChainActive && event.key === Qt.Key_Backtab) {
                    if (jumpToNextSlot(true)) { event.accepted = true; return }
                }

                // A display equation participates in the same continuous
                // Up/Down caret traversal as prose. Preserve modified arrows
                // for native selection/navigation inside the TeX source.
                var arrowModifiers = Qt.ControlModifier | Qt.ShiftModifier
                    | Qt.AltModifier | Qt.MetaModifier
                if (!(event.modifiers & arrowModifiers)
                    && event.key === Qt.Key_Up
                    && root.isCursorOnFirstLine()) {
                    if (root.focusAdjacentBlock(-1))
                        event.accepted = true
                    return
                }
                if (!(event.modifiers & arrowModifiers)
                    && event.key === Qt.Key_Down
                    && root.isCursorOnLastLine()) {
                    if (root.focusAdjacentBlock(1))
                        event.accepted = true
                    return
                }

                if (event.key === Qt.Key_Escape) {
                    root.focusSelectionHandler(); event.accepted = true
                }
            }
        }

        // ---- Live preview: shown while editing ----
        Rectangle {
            width: parent.width
            height: root.editing
                ? Math.max(previewImage.implicitHeight,
                           previewError.implicitHeight, 28) + 12
                : 0
            visible: root.editing
            radius: 4
            color: theme.panelBackground
            border.color: theme.border; border.width: 1
            Image {
                id: previewImage
                objectName: "mathPreviewImage"
                anchors.centerIn: parent
                visible: root.errorText === "" && root.previewTex.trim().length > 0
                source: root.mathSource(root.previewTex)
                width: implicitWidth
                height: implicitHeight
                fillMode: Image.PreserveAspectFit
                smooth: true
                cache: false
            }
            Text {
                anchors.centerIn: parent
                visible: root.previewTex.trim().length === 0
                text: qsTr("Preview"); color: theme.textFaint; font.pixelSize: 12
            }
            Text {
                id: previewError
                anchors.centerIn: parent
                visible: root.errorText !== "" && root.previewTex.trim().length > 0
                text: "⚠ " + root.errorText
                color: theme.danger; font.pixelSize: 12
                wrapMode: Text.Wrap; width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // HoverHandler, not a hover MouseArea: pointer handlers keep reporting
    // hovered while the cursor is over the gutter chrome's own MouseAreas,
    // whereas MouseArea hover gets stolen by them — the chrome then hides
    // under the cursor, hover returns, it re-shows, and the plus button
    // blinks unclickably (TextBlockDelegate already uses this pattern).
    HoverHandler { id: hoverArea }

    // Right-click on the rendered block opens the shared block menu
    // (turn into / duplicate / delete / move) — without this the only
    // route to deleting a math block was keyboard selection. While
    // editing, the source TextArea owns the context menu instead.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        enabled: !root.editing
        onClicked: {
            var win = Window.window
            if (win && win.openBlockHandleMenu)
                win.openBlockHandleMenu(root)
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
        objectName: "mathHandle"
        width: 14; height: 18; x: 30; y: 8
        opacity: root.isHovered || mathHandle.pressed ? 0.6 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Column { anchors.centerIn: parent; spacing: 2
            Repeater { model: 2; Row { spacing: 2; Repeater { model: 2
                Rectangle { width: 3; height: 3; radius: 1.5; color: theme.textFaint } } } } }
        MouseArea {
            id: mathHandle
            objectName: "dragHandle"
            anchors.fill: parent; anchors.margins: -2
            hoverEnabled: true; cursorShape: Qt.OpenHandCursor; preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            property real pressX: 0; property real pressY: 0; property bool dragging: false
            onPressed: function(mouse) {
                if (mouse.button === Qt.RightButton) {
                    var win = Window.window
                    if (win && win.openBlockHandleMenu)
                        win.openBlockHandleMenu(root)
                    return
                }
                pressX = mouse.x; pressY = mouse.y; dragging = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                var win = Window.window
                if (!win || !win.blockDrag) return
                var sp = mathHandle.mapToItem(null, mouse.x, mouse.y)
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
