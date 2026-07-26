// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Columns, cards and label chips are nested delegates whose content and
// handlers are separate scopes reading the ids around them.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// Kanban board (features.md §1.2.12). The block is a `kanban`-tagged code
// fence; its content is human-readable markdown that KanbanTools maps to a
// board and back. Every mutation — dragging a card within or between columns,
// dragging a column, adding, removing, renaming, editing a card, toggling
// done — rewrites the fence content through the model as one undo step.
// Column collapse and the filter row are session-scoped chrome that no file
// records.
//
// A card is edited where it sits, the way the prose blocks are edited rather
// than through a dialog. Clicking a card's text opens an editor on the card's
// own line — the title with its `#label` and `📅 date` written as the file
// holds them — and clicking under it opens one on the description, which
// takes as many lines as it needs and renders formulas and [[wiki-links]]
// like the rest of the note. Both carry the same hybrid-editing engine, math
// entry and wiki-link completion the prose blocks use. The card-details
// popover is the structured way to the same labels and due date, plus the
// pointer-free way to move or delete a card; it is no longer the way in.
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
    required property string language
    required property string calloutTitle

    property int blockIndex: index
    property bool isPooled: false
    property ListView listView: ListView.view
    property bool isFocused: focusTarget.activeFocus || root.editCol >= 0
    // The gutter's MouseAreas sit over hoverArea and steal its hover; fold
    // the gutter's own hover back in so the buttons do not vanish the moment
    // the pointer reaches them (as EditableBlock does).
    property bool isHovered: hoverArea.containsMouse || blockHandle.hovered

    readonly property var board: KanbanTools.parse(content)
    readonly property var columns: board.columns
    // What a delegate reads while the board is between two shapes. A rewrite
    // reaches the delegates' bindings before the Repeater has dropped the
    // delegates the new board has no row for, so for that one pass a card or
    // column index can point past the end; these stand in for it rather than
    // letting every binding on the way down fail on an undefined.
    readonly property var emptyColumn: ({ name: "", cards: [] })
    readonly property var emptyCard: ({ title: "", done: false, labels: [],
                                        due: "", description: "", line: "" })

    // ---- Session chrome: none of this is written to the file ----
    property var collapsed: ({})
    // The label the filter row is narrowing to, and whether finished cards
    // are being kept out of the way. Both hide cards rather than dimming
    // them: a filter that only fades what it excludes reads as a rendering
    // artefact, and the column counts said one thing while the column showed
    // another.
    property string labelFilter: ""
    property bool hideDone: false
    property int renamingCol: -1

    // How many cards a column renders before the rest need asking for, and
    // the per-column overrides a "show all" writes. A board is laid out
    // inline, so every rendered card is a live card.
    readonly property int cardWindowStep: 100
    property var cardReveal: ({})
    function revealedCardsIn(columnName) {
        var n = cardReveal[columnName]
        return n !== undefined ? n : cardWindowStep
    }
    function revealAllCardsIn(columnName, total) {
        var r = Object.assign({}, cardReveal)
        r[columnName] = total
        cardReveal = r
    }

    // ---- Geometry ----
    readonly property int columnWidth: 240
    readonly property int columnGap: 10
    readonly property int cardFontSize: 12
    // Inline math on a card (§1.2.15), sized the way the table sizes it in a
    // cell: optically matched to the card font's x-height, which is what the
    // editing engine reserves its boxes at, so a formula does not move when a
    // card goes live and back. The cards set no family, so the empty one here
    // asks about the same default font they are drawn in.
    readonly property int cardMathPixelSize:
        MathRenderer.opticalMathPixelSize("", root.cardFontSize)
    readonly property int cardMathVerticalPadding:
        Math.max(2, Math.ceil(root.cardMathPixelSize * 0.12))
    readonly property real screenDevicePixelRatio:
        (Screen.devicePixelRatio !== undefined && Screen.devicePixelRatio > 0)
            ? Screen.devicePixelRatio : 1
    // The token source for the editing engine (a bare `theme` inside the
    // engine resolves to the engine's own property).
    readonly property var appThemeRef: Theme

    // ---- Filtering ----
    function cardVisible(card) {
        if (root.hideDone && card.done)
            return false
        return root.labelFilter === ""
            || card.labels.indexOf(root.labelFilter) !== -1
    }
    // The indices, in the column's own card order, of the cards the filter
    // lets through. Everything downstream — what a column renders, what a
    // drop lands next to, what the header counts — is this list, so a
    // filtered board cannot mutate the wrong card.
    function visibleIndicesIn(colIndex) {
        var out = []
        if (colIndex < 0 || colIndex >= root.columns.length)
            return out
        var cards = root.columns[colIndex].cards
        for (var i = 0; i < cards.length; ++i)
            if (root.cardVisible(cards[i]))
                out.push(i)
        return out
    }
    readonly property bool filtering: root.labelFilter !== "" || root.hideDone
    readonly property int cardCount: {
        var n = 0
        for (var c = 0; c < root.columns.length; ++c)
            n += root.columns[c].cards.length
        return n
    }
    readonly property int visibleCardCount: {
        var n = 0
        for (var c = 0; c < root.columns.length; ++c)
            n += root.visibleIndicesIn(c).length
        return n
    }
    readonly property int doneCardCount: {
        var n = 0
        for (var c = 0; c < root.columns.length; ++c) {
            var cards = root.columns[c].cards
            for (var k = 0; k < cards.length; ++k)
                if (cards[k].done) ++n
        }
        return n
    }

    // All labels across the board (for the filter row and palette coloring).
    readonly property var allLabels: {
        var seen = []
        for (var c = 0; c < columns.length; ++c)
            for (var k = 0; k < columns[c].cards.length; ++k) {
                var ls = columns[c].cards[k].labels
                for (var l = 0; l < ls.length; ++l)
                    if (seen.indexOf(ls[l]) === -1) seen.push(ls[l])
            }
        return seen
    }
    // A column's name, empty for an index the board no longer has — the
    // "move to column" lists are rebuilt one pass behind a column removal.
    function columnName(col) {
        return (col >= 0 && col < root.columns.length) ? root.columns[col].name : ""
    }
    function labelColor(label) {
        var pal = Theme.colorPalette
        var h = 0
        for (var i = 0; i < label.length; ++i) h = (h * 31 + label.charCodeAt(i)) % pal.length
        return pal[h]
    }

    // ---- Inline editing ----
    // The card and field the one live editor is on: column, card index in
    // that column's own order, and "title" (the card's line) or
    // "description". -1 is "nothing is being edited".
    property int editCol: -1
    property int editIdx: -1
    property string editField: ""
    // The field rectangle the editor parents itself into, set by whichever
    // field is live. One editor exists per board, not one Loader per card.
    property Item activeFieldItem: null
    // Height of the live editor's text, so the field grows to fit what is
    // being typed instead of clipping it.
    property real activeEditorHeight: 0

    function editingField(col, idx, field) {
        return root.editCol === col && root.editIdx === idx
            && root.editField === field
    }
    function beginEdit(col, idx, field) {
        if (col < 0 || col >= root.columns.length)
            return
        var cards = root.columns[col].cards
        if (idx < 0 || idx >= cards.length)
            return
        root.editCol = col
        root.editIdx = idx
        root.editField = field
    }
    function clearEdit() {
        root.editCol = -1
        root.editIdx = -1
        root.editField = ""
    }
    function endEdit() {
        root.clearEdit()
        focusTarget.forceActiveFocus()   // keep the block "focused" for the shell
    }
    // Tab walks the card: its line, then its description, then out.
    function editNextField() {
        if (root.editField === "title")
            root.beginEdit(root.editCol, root.editIdx, "description")
        else
            root.endEdit()
    }
    function editPreviousField() {
        if (root.editField === "description")
            root.beginEdit(root.editCol, root.editIdx, "title")
        else
            root.endEdit()
    }
    function endEditIfFocusLeft() {
        var p = Window.activeFocusItem
        while (p) {
            if (p === root)
                return
            p = p.parent
        }
        root.clearEdit()
    }

    // The day a change is being made on, in the form the storage grammar
    // wants. Asked for at the moment of the change rather than held in a
    // property, so a window left open overnight stamps the day the edit
    // actually happened on.
    function today() { return Qt.formatDate(new Date(), "yyyy-MM-dd") }

    // A card, or null for an index the board no longer has.
    function cardAt(col, idx) {
        if (col < 0 || col >= root.columns.length)
            return null
        var cards = root.columns[col].cards
        return (idx >= 0 && idx < cards.length) ? cards[idx] : null
    }

    function cardLineText(col, idx) {
        if (col < 0 || col >= root.columns.length)
            return ""
        var cards = root.columns[col].cards
        return (idx >= 0 && idx < cards.length) ? cards[idx].line : ""
    }
    function cardDescriptionText(col, idx) {
        if (col < 0 || col >= root.columns.length)
            return ""
        var cards = root.columns[col].cards
        return (idx >= 0 && idx < cards.length) ? cards[idx].description : ""
    }
    function commitCardLine(col, idx, text) {
        var md = KanbanTools.setCardLine(root.content, col, idx, text, root.today())
        if (md !== root.content)
            root.writeBoard(md)
    }
    function commitCardDescription(col, idx, text) {
        var md = KanbanTools.setCardDescription(root.content, col, idx, text,
                                                root.today())
        if (md !== root.content)
            root.writeBoard(md)
    }

    // ---- Drag state ----
    // The card a drag is carrying, read by the drop targets. Taken from this
    // board rather than from the dragged item, so a card dragged out of
    // another board on the same page cannot land here with indices that mean
    // nothing in this one.
    property int dragCol: -1
    property int dragIdx: -1
    readonly property bool cardDragging: root.dragCol >= 0
    property int dragColumn: -1
    readonly property bool columnDragging: root.dragColumn >= 0

    function beginCardDrag(col, idx) {
        root.clearEdit()
        root.dragCol = col
        root.dragIdx = idx
    }
    function endCardDrag() {
        root.dragCol = -1
        root.dragIdx = -1
    }
    // `slot` is an insert-before position in the destination column's own
    // card order, which is what KanbanTools::moveCard takes.
    function dropCardAt(toCol, slot) {
        if (root.dragCol < 0)
            return
        root.writeBoard(KanbanTools.moveCard(root.content, root.dragCol,
                                             root.dragIdx, toCol, slot,
                                             root.today()))
    }
    // The same move without a pointer gesture: to the end of another column.
    function moveCardToColumn(col, idx, targetCol) {
        if (targetCol < 0 || targetCol >= root.columns.length)
            return
        root.writeBoard(KanbanTools.moveCard(root.content, col, idx, targetCol,
                                             root.columns[targetCol].cards.length,
                                             root.today()))
    }
    // `slot` names the gap the column is dropped into, counted in the board's
    // current column order; QList::move takes the index the column ends up
    // at, which is one lower for every column it passed on the way right.
    function dropColumnAt(slot) {
        if (root.dragColumn < 0)
            return
        var to = slot > root.dragColumn ? slot - 1 : slot
        if (to === root.dragColumn)
            return
        root.writeBoard(KanbanTools.moveColumn(root.content, root.dragColumn, to))
    }

    // ---- Board mutations, each one model content update (one undo step) ----
    function writeBoard(md) { BlockModel.updateContent(root.index, md) }

    function toggleCardDone(col, idx) {
        root.writeBoard(KanbanTools.toggleCardDone(root.content, col, idx,
                                                   root.today()))
    }
    function removeCardAt(col, idx) {
        root.clearEdit()
        root.writeBoard(KanbanTools.removeCard(root.content, col, idx))
    }

    // ---- The card's labels and due date, as fields ----
    // A label typed on the card's own line is still a label, but that is the
    // syntax; this is the other way in, from the chips under the title, and it
    // is the way a date is set at all — the picker writes a day the storage
    // grammar can hold rather than leaving the reader to remember the shape.
    function setCardFields(col, idx, labels, due) {
        var card = root.cardAt(col, idx)
        if (!card)
            return
        root.writeBoard(KanbanTools.setCard(root.content, col, idx, card.title,
                                            card.done, labels, due,
                                            card.description, root.today()))
    }
    function addLabel(col, idx, name) {
        var card = root.cardAt(col, idx)
        if (!card)
            return
        // A leading hash is how the reader says "label"; the stored form does
        // not carry it, so it comes off here rather than becoming part of one.
        var clean = String(name).replace(/^#+/, "").trim()
        if (clean.length === 0 || card.labels.indexOf(clean) !== -1)
            return
        var labels = card.labels.slice()
        labels.push(clean)
        root.setCardFields(col, idx, labels, card.due)
    }
    function removeLabel(col, idx, name) {
        var card = root.cardAt(col, idx)
        if (!card)
            return
        var labels = []
        for (var i = 0; i < card.labels.length; ++i)
            if (card.labels[i] !== name)
                labels.push(card.labels[i])
        root.setCardFields(col, idx, labels, card.due)
    }
    function setCardDue(col, idx, day) {
        var card = root.cardAt(col, idx)
        if (card)
            root.setCardFields(col, idx, card.labels, day)
    }

    // ---- Typing a label into the chip row ----
    // The card whose chip row is taking a label, what has been typed into it,
    // and which of the board's existing labels that highlights. -1 highlights
    // nothing, so Enter takes the typed text: the list is there to reuse a
    // label, not to stop a new one being made.
    property int tagEditCol: -1
    property int tagEditIdx: -1
    property string tagQuery: ""
    property int tagHighlight: -1
    property Item activeTagField: null
    readonly property bool taggingCard: root.tagEditCol >= 0

    // The board's own labels, minus the ones this card already carries,
    // narrowed by what has been typed.
    readonly property var tagChoices: {
        var card = root.cardAt(root.tagEditCol, root.tagEditIdx)
        if (!card)
            return []
        var q = root.tagQuery.replace(/^#+/, "").toLowerCase()
        var out = []
        for (var i = 0; i < root.allLabels.length; ++i) {
            var label = root.allLabels[i]
            if (card.labels.indexOf(label) !== -1)
                continue
            if (q.length > 0 && label.toLowerCase().indexOf(q) === -1)
                continue
            out.push(label)
        }
        return out
    }

    function beginTagEdit(col, idx) {
        root.clearEdit()
        root.tagQuery = ""
        root.tagHighlight = -1
        root.tagEditCol = col
        root.tagEditIdx = idx
    }
    function endTagEdit() {
        root.tagEditCol = -1
        root.tagEditIdx = -1
        root.tagQuery = ""
        root.tagHighlight = -1
    }
    function commitTag(name) {
        var col = root.tagEditCol
        var idx = root.tagEditIdx
        root.endTagEdit()
        root.addLabel(col, idx, name)
    }
    // Enter takes the highlighted label when the reader moved onto one, and
    // what they typed otherwise.
    function acceptTag() {
        var choices = root.tagChoices
        if (root.tagHighlight >= 0 && root.tagHighlight < choices.length)
            root.commitTag(choices[root.tagHighlight])
        else if (root.tagQuery.replace(/^#+/, "").trim().length > 0)
            root.commitTag(root.tagQuery)
        else
            root.endTagEdit()
    }
    function moveTagHighlight(delta) {
        var n = root.tagChoices.length
        if (n === 0)
            return
        var next = root.tagHighlight + delta
        root.tagHighlight = next < -1 ? n - 1 : (next >= n ? -1 : next)
    }
    // A new card is empty and goes straight into its editor: a card seeded
    // with the words "New card" has to be selected and overtyped before it
    // can be named, which is a worse first step than an empty line and a
    // caret.
    function addCardAndEdit(col) {
        root.writeBoard(KanbanTools.addCard(root.content, col, "", root.today()))
        Qt.callLater(function() {
            if (col >= 0 && col < root.columns.length)
                root.beginEdit(col, root.columns[col].cards.length - 1, "title")
        })
    }
    function addColumnAndRename() {
        root.writeBoard(KanbanTools.addColumn(root.content, qsTr("New column")))
        Qt.callLater(function() { root.renamingCol = root.columns.length - 1 })
    }
    function moveColumnBy(col, delta) {
        var to = col + delta
        if (to < 0 || to >= root.columns.length)
            return
        root.writeBoard(KanbanTools.moveColumn(root.content, col, to))
    }
    // Removing a column takes its cards with it. No confirmation, for the
    // reason the block's own delete button asks for none: Ctrl+Z brings the
    // whole column back, and the red hover fill is the destructive cue.
    function removeColumnAt(col) {
        root.clearEdit()
        root.renamingCol = -1
        root.writeBoard(KanbanTools.removeColumn(root.content, col))
    }
    function commitRename(col, name) {
        root.renamingCol = -1
        var trimmed = name.trim()
        if (col < 0 || col >= root.columns.length || trimmed.length === 0)
            return
        var old = root.columns[col].name
        if (trimmed === old)
            return
        // The session chrome is keyed by column name, so carry it across the
        // rename rather than letting a renamed column forget that it was
        // collapsed or expanded.
        if (root.collapsed[old] !== undefined) {
            var c = Object.assign({}, root.collapsed)
            c[trimmed] = c[old]
            delete c[old]
            root.collapsed = c
        }
        if (root.cardReveal[old] !== undefined) {
            var r = Object.assign({}, root.cardReveal)
            r[trimmed] = r[old]
            delete r[old]
            root.cardReveal = r
        }
        root.writeBoard(KanbanTools.renameColumn(root.content, col, trimmed))
    }
    function toggleCollapsed(col) {
        var name = root.columnName(col)
        var c = Object.assign({}, root.collapsed)
        c[name] = !(root.collapsed[name] === true)
        root.collapsed = c
    }

    // Where a drag was grabbed, inside the item it is carrying. Used as that
    // drag's hot spot, which is the point the drop targets are asked about:
    // with it, what a drop lands on is what the pointer is over, rather than
    // wherever the middle of the carried card or column has ended up.
    // Answered while the item is still in place, before the drag reparents it.
    function grabPointIn(item: Item, scenePress: point): point {
        return item.mapFromItem(null, scenePress.x, scenePress.y)
    }

    // Whether a point lies inside an item, in that item's own coordinates.
    // The board routes one tap handler per card rather than putting a mouse
    // area over every part of it: an item that accepts presses on top of a
    // card would stop the card's drag handler from ever seeing them.
    function pointInside(item: Item, p: point): bool {
        return p.x >= 0 && p.y >= 0 && p.x < item.width && p.y < item.height
    }

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
    function focusSelectionHandler() {
        AppActions.requestSelectionFocus()
    }
    onIsFocusedChanged: {
        if (isFocused) {
            if (root.shell && root.shell.lastFocusedBlock !== undefined) root.shell.lastFocusedBlock = index
        }
    }

    implicitHeight: boardColumn.implicitHeight + 16

    ListView.onPooled: { isPooled = true; opacity = 0; root.clearEdit() }
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

    // A square glyph button for the column header: transparent until the
    // pointer reaches it, and it says what it does while the pointer is
    // there. The board's controls used to be bare glyphs with a tap handler,
    // which gave no sign that they were controls at all.
    component KanbanIconButton: Rectangle {
        id: iconButton
        property string glyph: ""
        property string tip: ""
        property bool destructive: false
        property bool actionEnabled: true
        signal activated()

        width: 20
        height: 20
        radius: 4
        color: !iconButton.actionEnabled || !iconHover.hovered ? "transparent"
             : (iconButton.destructive ? Theme.danger : Theme.hoverTint)

        Text {
            anchors.centerIn: parent
            text: iconButton.glyph
            font.pixelSize: 13
            color: !iconButton.actionEnabled ? Theme.textFaint
                 : (iconButton.destructive && iconHover.hovered ? Theme.onAccent
                                                                : Theme.textMuted)
        }
        HoverHandler {
            id: iconHover
            enabled: iconButton.actionEnabled
            cursorShape: Qt.PointingHandCursor
        }
        // ReleaseWithinBounds rather than the default: this takes the press
        // outright. A handler that only watches leaves the press to carry on
        // down the stack, and Qt offers a press to the handlers of every item
        // under it before any item accepts it — so a button drawn over
        // something else lets the click through to whatever is behind.
        TapHandler {
            enabled: iconButton.actionEnabled
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: iconButton.activated()
        }
        ToolTip.visible: iconHover.hovered && iconButton.tip !== ""
        ToolTip.text: iconButton.tip
        ToolTip.delay: 400
    }

    // A pill the size of a card's label chips, for the controls that sit
    // among them: the due date, and the two that add one.
    component KanbanChip: Rectangle {
        id: chipButton
        property string label: ""
        property string tip: ""
        // A chip carrying a value reads as filled; one offering to add a
        // value reads as an outline.
        property bool filled: false
        signal activated()

        height: 16
        radius: 8
        width: chipButtonLabel.implicitWidth + 12
        color: chipButton.filled
            ? Qt.alpha(Theme.accent, chipButtonHover.hovered ? 0.35 : 0.18)
            : (chipButtonHover.hovered ? Theme.hoverTint : "transparent")
        border.width: 1
        border.color: chipButtonHover.hovered ? Theme.accent
                    : (chipButton.filled ? "transparent" : Theme.border)

        Text {
            id: chipButtonLabel
            anchors.centerIn: parent
            text: chipButton.label
            font.pixelSize: 9
            color: chipButtonHover.hovered ? Theme.textPrimary : Theme.textMuted
        }
        HoverHandler { id: chipButtonHover; cursorShape: Qt.PointingHandCursor }
        // ReleaseWithinBounds rather than the default: this handler takes the
        // press outright, so the card's own tap handler underneath does not
        // also fire and open the card's text editor behind the chip.
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: chipButton.activated()
        }
        ToolTip.visible: chipButtonHover.hovered && chipButton.tip !== ""
        ToolTip.text: chipButton.tip
        ToolTip.delay: 400
    }

    // A flat labelled button, matching the app's in-block affordances
    // (transparent until hovered) rather than the OS-styled control.
    component KanbanTextButton: Rectangle {
        id: textButton
        property string label: ""
        property string tip: ""
        property bool outlined: true
        signal activated()

        implicitWidth: textButtonLabel.implicitWidth + 18
        implicitHeight: 22
        radius: 4
        color: textHover.hovered ? Theme.hoverTint : "transparent"
        border.width: textButton.outlined ? 1 : 0
        border.color: Theme.border

        Text {
            id: textButtonLabel
            anchors.centerIn: parent
            text: textButton.label
            color: textHover.hovered ? Theme.textPrimary : Theme.textMuted
            font.pixelSize: 11
        }
        HoverHandler { id: textHover; cursorShape: Qt.PointingHandCursor }
        // Takes the press, for the reason KanbanIconButton does.
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: textButton.activated()
        }
        ToolTip.visible: textHover.hovered && textButton.tip !== ""
        ToolTip.text: textButton.tip
        ToolTip.delay: 400
    }

    // Hover for the block as a whole. First in the file, so it sits under the
    // board: an item that accepts hover on top of the columns would take it
    // from the cards and the buttons that need it.
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    Item {
        id: focusTarget
        objectName: "kanbanFocusItem"
        anchors.fill: parent
        activeFocusOnTab: true
        Keys.onPressed: function(event) {
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)) {
                if (root.listView) root.listView.currentIndex = root.index
                DocumentSelection.selectBlock(root.index)
                root.focusSelectionHandler(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Up && root.index > 0 && root.listView) {
                var pi = root.index - 1
                root.listView.currentIndex = pi
                var prev = (root.listView.itemAtIndex(pi) as BlockDelegateBase)
                if (prev) prev.focusAtEnd(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Down && root.index < BlockModel.count - 1 && root.listView) {
                var ni = root.index + 1
                root.listView.currentIndex = ni
                var next = (root.listView.itemAtIndex(ni) as BlockDelegateBase)
                if (next) next.focusAtStart(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                root.deleteCurrentBlock(); event.accepted = true; return
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.createBlockBelow(); event.accepted = true; return
            }
        }
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

    Column {
        id: boardColumn
        // Past the gutter and onto the text column: the same left edge a
        // code panel or a callout card gets from EditableBlock's content
        // area, so a document's blocks share one left margin.
        x: 52; y: 8
        width: root.width - 64
        spacing: 6
        opacity: root.isDragSource ? 0.35 : 1

        // The filter row. A chip narrows the board to one label and says so:
        // the cards that do not carry it leave the columns, the counts follow,
        // and the line beside the chips reports how much of the board is
        // showing, with the way back next to it.
        Flow {
            id: filterRow
            width: parent.width
            spacing: 6
            visible: root.allLabels.length > 0 || root.doneCardCount > 0

            Text {
                height: 20
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Filter")
                font.pixelSize: 11
                color: Theme.textFaint
            }

            Repeater {
                model: root.allLabels
                delegate: Rectangle {
                    id: filterChip
                    required property var modelData
                    objectName: "kanbanFilterChip"
                    readonly property bool active: root.labelFilter === filterChip.modelData
                    height: 20
                    radius: 10
                    width: filterChipLabel.implicitWidth + 16
                    color: filterChip.active ? root.labelColor(filterChip.modelData)
                         : (filterChipHover.hovered
                            ? Qt.alpha(root.labelColor(filterChip.modelData), 0.25)
                            : Theme.chipBackground)
                    border.width: 1
                    border.color: root.labelColor(filterChip.modelData)
                    Text {
                        id: filterChipLabel
                        anchors.centerIn: parent
                        text: "#" + filterChip.modelData
                        font.pixelSize: 11
                        color: filterChip.active ? Theme.onAccent : Theme.textMuted
                    }
                    HoverHandler { id: filterChipHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: root.labelFilter = filterChip.active
                            ? "" : filterChip.modelData
                    }
                    ToolTip.visible: filterChipHover.hovered
                    ToolTip.text: filterChip.active
                        ? qsTr("Show cards with every label again")
                        : qsTr("Show only cards labelled #%1").arg(filterChip.modelData)
                    ToolTip.delay: 400
                }
            }

            Rectangle {
                id: hideDoneChip
                objectName: "kanbanFilterHideDone"
                visible: root.doneCardCount > 0
                height: 20
                radius: 10
                width: hideDoneLabel.implicitWidth + 16
                color: root.hideDone ? Theme.accent
                     : (hideDoneHover.hovered ? Theme.hoverTint : Theme.chipBackground)
                border.width: 1
                border.color: root.hideDone ? Theme.accent : Theme.border
                Text {
                    id: hideDoneLabel
                    anchors.centerIn: parent
                    text: qsTr("Hide done")
                    font.pixelSize: 11
                    color: root.hideDone ? Theme.onAccent : Theme.textMuted
                }
                HoverHandler { id: hideDoneHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.hideDone = !root.hideDone }
                ToolTip.visible: hideDoneHover.hovered
                ToolTip.text: root.hideDone ? qsTr("Show finished cards again")
                                            : qsTr("Keep finished cards out of the columns")
                ToolTip.delay: 400
            }

            Text {
                objectName: "kanbanFilterStatus"
                visible: root.filtering
                height: 20
                verticalAlignment: Text.AlignVCenter
                text: qsTr("%1 of %2 cards").arg(root.visibleCardCount).arg(root.cardCount)
                font.pixelSize: 11
                color: Theme.textMuted
            }

            KanbanTextButton {
                objectName: "kanbanFilterClear"
                visible: root.filtering
                label: qsTr("Clear filter")
                tip: qsTr("Show the whole board again")
                outlined: false
                onActivated: { root.labelFilter = ""; root.hideDone = false }
            }
        }

        // The columns, laid out in a horizontal Flickable.
        Flickable {
            id: columnsFlick
            width: parent.width
            height: columnsRow.implicitHeight
            contentWidth: columnsRow.implicitWidth
            clip: true
            // A drag inside the board is not a pan of it: while a card or a
            // column is being carried, this Flickable stays still rather than
            // sliding out from under the drop it is aiming at.
            interactive: !root.cardDragging && !root.columnDragging

            Row {
                id: columnsRow
                spacing: root.columnGap

                Repeater {
                    id: columnsRepeater
                    model: root.columns.length
                    delegate: Rectangle {
                        id: columnItem
                        required property int index
                        readonly property int colIndex: index
                        readonly property var colData: colIndex < root.columns.length
                            ? root.columns[colIndex] : root.emptyColumn
                        readonly property bool isCollapsed: root.collapsed[colData.name] === true
                        readonly property bool isRenaming: root.renamingCol === colIndex
                        readonly property var visibleIndices: root.visibleIndicesIn(colIndex)
                        readonly property int renderedCards:
                            Math.min(visibleIndices.length,
                                     root.revealedCardsIn(colData.name))
                        readonly property int hiddenCards:
                            visibleIndices.length - renderedCards
                        readonly property bool isColumnDragSource:
                            root.dragColumn === colIndex
                        // Where a dropped column would land: the gap before
                        // this column or the gap after it, whichever side of
                        // it the pointer is on.
                        readonly property int columnDropSlot:
                            columnDrop.containsDrag
                                ? (columnDrop.drag.x < columnItem.width / 2
                                   ? colIndex : colIndex + 1)
                                : -1

                        width: root.columnWidth
                        implicitHeight: colHeader.height
                            + (isCollapsed ? 8 : colCards.implicitHeight + 16)
                        height: implicitHeight
                        radius: 6
                        color: Theme.panelBackground
                        border.width: 1
                        border.color: cardDrop.containsDrag ? Theme.accent : Theme.border
                        opacity: columnItem.carrying ? 0.9 : 1

                        // Dragging a column to reorder it (§1.2.12). The card
                        // drag below explains the shape; the one difference is
                        // that a column is carried out of a Row, which would
                        // otherwise keep putting it back where the model says
                        // it goes.
                        property bool carrying: false

                        Drag.dragType: Drag.Internal
                        Drag.keys: [ "kvit-kanban-column" ]
                        // Set to the point the drag was started from when it
                        // starts; see the card's own hot spot below.
                        Drag.hotSpot.x: root.columnWidth / 2
                        Drag.hotSpot.y: 15

                        states: State {
                            when: columnItem.carrying
                            ParentChange { target: columnItem; parent: root }
                            PropertyChanges { columnItem.z: 90 }
                        }

                        // Where a column dropped on this one lands. Disabled
                        // while this column is the one being carried: it
                        // travels under the pointer, and a live drop target
                        // there would sit in front of every column the drag
                        // is actually aiming at.
                        DropArea {
                            id: columnDrop
                            anchors.fill: parent
                            enabled: !columnItem.carrying
                            keys: [ "kvit-kanban-column" ]
                            onDropped: function(drop) {
                                root.dropColumnAt(columnItem.columnDropSlot)
                                drop.accept()
                            }
                        }

                        // The insertion mark for that drop, on whichever edge
                        // the column would go in at.
                        Rectangle {
                            visible: columnItem.columnDropSlot >= 0
                                     && !columnItem.isColumnDragSource
                            width: 3
                            height: parent.height
                            radius: 1.5
                            color: Theme.accent
                            x: columnItem.columnDropSlot === columnItem.colIndex
                               ? -6 : columnItem.width + 3
                            z: 50
                        }

                        // Column header: the collapse triangle, the name
                        // (which is also the grip the column is dragged by and
                        // the field it is renamed in), the card count, and the
                        // controls.
                        Item {
                            id: colHeader
                            width: parent.width
                            height: 30

                            KanbanIconButton {
                                id: collapseButton
                                objectName: "kanbanColCollapse"
                                x: 4
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: columnItem.isCollapsed ? "▸" : "▾"
                                tip: columnItem.isCollapsed ? qsTr("Expand column")
                                                            : qsTr("Collapse column")
                                onActivated: root.toggleCollapsed(columnItem.colIndex)
                            }

                            Item {
                                id: colNameArea
                                objectName: "kanbanColName"
                                anchors.left: collapseButton.right
                                anchors.leftMargin: 4
                                anchors.right: colControls.left
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                height: 22

                                Text {
                                    id: colNameText
                                    visible: !columnItem.isRenaming
                                    anchors.left: parent.left
                                    anchors.right: colCountText.left
                                    anchors.rightMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: columnItem.colData.name
                                    elide: Text.ElideRight
                                    font.bold: true
                                    font.pixelSize: 12
                                    color: colNameHover.hovered ? Theme.accent
                                                                : Theme.textPrimary
                                }
                                Text {
                                    id: colCountText
                                    visible: !columnItem.isRenaming
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    // Under a filter the count says both
                                    // numbers, so a short column is read as
                                    // filtered rather than as emptied.
                                    text: root.filtering
                                        ? qsTr("%1 of %2").arg(columnItem.visibleIndices.length)
                                                          .arg(columnItem.colData.cards.length)
                                        : String(columnItem.colData.cards.length)
                                    font.pixelSize: 11
                                    color: Theme.textFaint
                                }

                                HoverHandler {
                                    id: colNameHover
                                    enabled: !columnItem.isRenaming
                                    cursorShape: Qt.IBeamCursor
                                }
                                TapHandler {
                                    enabled: !columnItem.isRenaming
                                    onTapped: root.renamingCol = columnItem.colIndex
                                }
                                // The grip: past the drag threshold the press
                                // that would have opened the rename field
                                // carries the column instead.
                                DragHandler {
                                    id: columnDrag
                                    target: columnItem
                                    enabled: !columnItem.isRenaming
                                             && root.columns.length > 1
                                    // Neither the board's Flickable nor the
                                    // document's list may take this grab: with
                                    // the default permissions a sideways drag
                                    // became a pan of the board and the column
                                    // never moved.
                                    grabPermissions: PointerHandler.CanTakeOverFromItems
                                        | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                                    onActiveChanged: {
                                        if (active) {
                                            root.clearEdit()
                                            columnItem.Drag.hotSpot =
                                                root.grabPointIn(columnItem,
                                                    columnDrag.centroid.scenePressPosition)
                                            columnItem.carrying = true
                                            columnItem.Drag.active = true
                                            root.dragColumn = columnItem.colIndex
                                        } else {
                                            columnItem.Drag.drop()
                                            root.dragColumn = -1
                                            columnItem.carrying = false
                                        }
                                    }
                                }
                                ToolTip.visible: colNameHover.hovered
                                ToolTip.text: qsTr("Click to rename, drag to reorder")
                                ToolTip.delay: 600

                                TextField {
                                    id: colRenameField
                                    objectName: "kanbanColRename"
                                    visible: columnItem.isRenaming
                                    anchors.fill: parent
                                    padding: 2
                                    font.bold: true
                                    font.pixelSize: 12
                                    color: Theme.textPrimary
                                    selectionColor: Theme.selectionActiveTint
                                    selectedTextColor: Theme.textPrimary
                                    selectByMouse: true
                                    background: Rectangle {
                                        color: Theme.windowBackground
                                        border.color: Theme.accent
                                        border.width: 1
                                        radius: 3
                                    }
                                    onVisibleChanged: {
                                        if (visible) {
                                            text = columnItem.colData.name
                                            forceActiveFocus()
                                            selectAll()
                                        }
                                    }
                                    onAccepted: root.commitRename(columnItem.colIndex, text)
                                    onActiveFocusChanged: {
                                        if (!activeFocus && columnItem.isRenaming)
                                            root.commitRename(columnItem.colIndex, text)
                                    }
                                    Keys.onEscapePressed: function(event) {
                                        root.renamingCol = -1
                                        event.accepted = true
                                    }
                                }
                            }

                            Row {
                                id: colControls
                                anchors.right: parent.right
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 0

                                KanbanIconButton {
                                    objectName: "kanbanColLeft"
                                    glyph: "‹"
                                    tip: qsTr("Move column left")
                                    actionEnabled: columnItem.colIndex > 0
                                    onActivated: root.moveColumnBy(columnItem.colIndex, -1)
                                }
                                KanbanIconButton {
                                    objectName: "kanbanColRight"
                                    glyph: "›"
                                    tip: qsTr("Move column right")
                                    actionEnabled: columnItem.colIndex < root.columns.length - 1
                                    onActivated: root.moveColumnBy(columnItem.colIndex, 1)
                                }
                                KanbanIconButton {
                                    objectName: "kanbanAddCard"
                                    glyph: "+"
                                    tip: qsTr("Add a card")
                                    onActivated: root.addCardAndEdit(columnItem.colIndex)
                                }
                                KanbanIconButton {
                                    objectName: "kanbanColDelete"
                                    glyph: "×"
                                    tip: qsTr("Delete this column and its cards (Ctrl+Z undoes it)")
                                    destructive: true
                                    onActivated: root.removeColumnAt(columnItem.colIndex)
                                }
                            }
                        }

                        // Where a card dropped on the column's own background
                        // lands: after everything already in it.
                        DropArea {
                            id: cardDrop
                            anchors.top: colHeader.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            keys: [ "kvit-kanban-card" ]
                            onDropped: function(drop) {
                                root.dropCardAt(columnItem.colIndex,
                                                columnItem.colData.cards.length)
                                drop.accept()
                            }
                        }

                        Column {
                            id: colCards
                            visible: !columnItem.isCollapsed
                            anchors.top: colHeader.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 8
                            spacing: 6

                            Repeater {
                                // Collapsing a column used to hide the Column
                                // and leave every card built underneath it:
                                // invisible is not the same as absent, and the
                                // cards kept their items, their text layouts
                                // and their drag handlers. An empty model is
                                // what actually releases them.
                                //
                                // The window on the rest is for the same
                                // reason the table has one — a board is drawn
                                // inline, so nothing virtualises its cards.
                                model: columnItem.isCollapsed
                                    ? 0 : columnItem.renderedCards
                                delegate: Rectangle {
                                    id: cardItem
                                    required property int index
                                    // The card's index in the column's own
                                    // order, which is what every mutation
                                    // takes; the delegate index counts only
                                    // the cards the filter let through.
                                    readonly property int cardIndex:
                                        index < columnItem.visibleIndices.length
                                            ? columnItem.visibleIndices[index] : -1
                                    readonly property int cardColIndex: columnItem.colIndex
                                    readonly property var cardData:
                                        cardIndex >= 0
                                        && cardIndex < columnItem.colData.cards.length
                                            ? columnItem.colData.cards[cardIndex]
                                            : root.emptyCard
                                    readonly property bool editingTitle:
                                        root.editingField(cardColIndex, cardIndex, "title")
                                    readonly property bool editingDescription:
                                        root.editingField(cardColIndex, cardIndex, "description")
                                    readonly property bool editing:
                                        cardItem.editingTitle || cardItem.editingDescription
                                    readonly property bool isDragged:
                                        root.dragCol === cardColIndex
                                        && root.dragIdx === cardIndex
                                    // The slot a card dropped on this one
                                    // takes: before it from the top half,
                                    // after it from the bottom.
                                    readonly property int cardDropSlot:
                                        cardItemDrop.containsDrag
                                            ? (cardItemDrop.drag.y > cardItem.height / 2
                                               ? cardIndex + 1 : cardIndex)
                                            : -1
                                    // Where the pointer is, for the parts of
                                    // the card that answer to it. One handler
                                    // reads it rather than each part carrying
                                    // a mouse area, which would take the
                                    // presses the drag handler needs.
                                    readonly property point hoverPoint:
                                        cardHover.point.scenePosition
                                    readonly property bool doneHovered:
                                        cardHover.hovered
                                        && root.pointInside(doneBox,
                                            doneBox.mapFromItem(null, cardItem.hoverPoint.x,
                                                                cardItem.hoverPoint.y))

                                    objectName: "kanbanCard"
                                    width: colCards.width
                                    implicitHeight: cardCol.implicitHeight + 12
                                    height: implicitHeight
                                    radius: 5
                                    color: Theme.windowBackground
                                    border.width: 1
                                    border.color: cardItem.editing ? Theme.accent
                                        : (cardHover.hovered ? Theme.borderStrong : Theme.border)

                                    // Card drag (features.md §1.2.12). Drag
                                    // carries this card as the drop payload;
                                    // while it is being carried a State
                                    // reparents it out of its Column into
                                    // `root` (a coordinate-preserving
                                    // ParentChange, so it escapes the column
                                    // clip and follows the pointer) and
                                    // reverts cleanly if the drop misses. The
                                    // drop is fired by hand because an
                                    // internal drag does not auto-drop.
                                    //
                                    // `carrying` is the board's own flag
                                    // rather than the handler's active state,
                                    // and both Drag.active and the State read
                                    // it, because the order matters at the
                                    // end of a drag: binding them to the
                                    // handler let the attached Drag cancel
                                    // itself and the ParentChange put the card
                                    // back before the drop was ever delivered.
                                    property bool carrying: false

                                    Drag.dragType: Drag.Internal
                                    Drag.keys: [ "kvit-kanban-card" ]
                                    // Replaced, when the drag starts, by the
                                    // point it started from: the drop targets
                                    // are asked about the drag's hot spot, and
                                    // a fixed one means the card lands
                                    // wherever the middle of a 240-wide box
                                    // happens to be rather than where the
                                    // pointer is.
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: 16

                                    DragHandler {
                                        id: cardDrag
                                        enabled: !cardItem.editing
                                        // The board scrolls sideways and the
                                        // document scrolls down, and both
                                        // would otherwise take this grab the
                                        // moment the pointer moved: the card
                                        // would stay where it was and the view
                                        // would slide instead.
                                        grabPermissions: PointerHandler.CanTakeOverFromItems
                                            | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                                        onActiveChanged: {
                                            if (active) {
                                                cardItem.Drag.hotSpot =
                                                    root.grabPointIn(cardItem,
                                                        cardDrag.centroid.scenePressPosition)
                                                cardItem.carrying = true
                                                cardItem.Drag.active = true
                                                root.beginCardDrag(cardItem.cardColIndex,
                                                                   cardItem.cardIndex)
                                            } else {
                                                // Delivers the drop, then ends
                                                // the drag; only then does the
                                                // card go back into its column.
                                                cardItem.Drag.drop()
                                                root.endCardDrag()
                                                cardItem.carrying = false
                                            }
                                        }
                                    }

                                    states: State {
                                        when: cardItem.carrying
                                        ParentChange { target: cardItem; parent: root }
                                        PropertyChanges { cardItem.z: 100 }
                                    }

                                    // Dropping onto this card puts the dragged
                                    // one next to it. Disabled while this card
                                    // is itself the drag source so it never
                                    // targets its own slot.
                                    DropArea {
                                        id: cardItemDrop
                                        anchors.fill: parent
                                        enabled: !cardItem.carrying
                                        keys: [ "kvit-kanban-card" ]
                                        onDropped: function(drop) {
                                            root.dropCardAt(cardItem.cardColIndex,
                                                            cardItem.cardDropSlot)
                                            drop.accept()
                                        }
                                    }

                                    // The insertion mark for that drop, in the
                                    // gap the card would go into. Drawn
                                    // outside the card's own box so it does
                                    // not move the cards around it.
                                    Rectangle {
                                        visible: cardItem.cardDropSlot === cardItem.cardIndex
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        y: -4
                                        height: 2
                                        radius: 1
                                        color: Theme.accent
                                    }
                                    Rectangle {
                                        visible: cardItem.cardDropSlot === cardItem.cardIndex + 1
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        y: parent.height + 2
                                        height: 2
                                        radius: 1
                                        color: Theme.accent
                                    }

                                    HoverHandler { id: cardHover }
                                    // One tap handler for the whole card,
                                    // routed by where the tap landed: the
                                    // checkbox toggles, the description opens
                                    // its editor, anything else opens the
                                    // card's line. A link inside either text
                                    // never reaches here — the Text takes that
                                    // press itself and opens the link.
                                    TapHandler {
                                        acceptedButtons: Qt.LeftButton
                                        onTapped: function(point) {
                                            var sx = point.scenePosition.x
                                            var sy = point.scenePosition.y
                                            if (root.pointInside(doneBox,
                                                    doneBox.mapFromItem(null, sx, sy))) {
                                                root.toggleCardDone(cardItem.cardColIndex,
                                                                    cardItem.cardIndex)
                                                return
                                            }
                                            // The chip row answers for itself,
                                            // chip by chip; a tap that lands
                                            // in it is never the card's.
                                            if (metaRow.visible
                                                && root.pointInside(metaRow,
                                                       metaRow.mapFromItem(null, sx, sy)))
                                                return
                                            if (descArea.visible
                                                && root.pointInside(descArea,
                                                       descArea.mapFromItem(null, sx, sy))) {
                                                root.beginEdit(cardItem.cardColIndex,
                                                               cardItem.cardIndex, "description")
                                                return
                                            }
                                            root.beginEdit(cardItem.cardColIndex,
                                                           cardItem.cardIndex, "title")
                                        }
                                    }
                                    TapHandler {
                                        acceptedButtons: Qt.RightButton
                                        onTapped: cardMenu.openFor(cardItem.cardColIndex,
                                                                   cardItem.cardIndex)
                                    }

                                    Column {
                                        id: cardCol
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 6
                                        spacing: 3

                                        Row {
                                            width: parent.width
                                            spacing: 6

                                            Rectangle {
                                                id: doneBox
                                                width: 14
                                                height: 14
                                                radius: 3
                                                anchors.top: parent.top
                                                anchors.topMargin: 1
                                                color: cardItem.cardData.done ? Theme.accent
                                                     : (cardItem.doneHovered ? Theme.hoverTint
                                                                             : "transparent")
                                                border.color: cardItem.cardData.done || cardItem.doneHovered
                                                    ? Theme.accent : Theme.borderStrong
                                                border.width: 1.5
                                                Text {
                                                    anchors.centerIn: parent
                                                    visible: cardItem.cardData.done
                                                    text: "✓"
                                                    color: Theme.onAccent
                                                    font.pixelSize: 9
                                                }
                                            }

                                            // The card's line: its title as
                                            // rendered text, and the source of
                                            // that line — labels and due date
                                            // included — while it is edited.
                                            Item {
                                                id: titleArea
                                                width: parent.width - 20
                                                implicitHeight: cardItem.editingTitle
                                                    ? Math.max(16, root.activeEditorHeight)
                                                    : Math.max(14, titleText.contentHeight)
                                                height: implicitHeight

                                                readonly property bool isActiveField:
                                                    cardItem.editingTitle
                                                onIsActiveFieldChanged: {
                                                    if (isActiveField)
                                                        root.activeFieldItem = titleArea
                                                    else if (root.activeFieldItem === titleArea)
                                                        root.activeFieldItem = null
                                                }
                                                Component.onDestruction: {
                                                    // A whole-board teardown
                                                    // destroys the delegate
                                                    // root first, so this has
                                                    // nothing left to clear.
                                                    if (root && root.activeFieldItem === titleArea)
                                                        root.activeFieldItem = null
                                                }

                                                Text {
                                                    id: titleText
                                                    visible: !cardItem.editingTitle
                                                    width: parent.width
                                                    readonly property bool untitled:
                                                        cardItem.cardData.title.length === 0
                                                    text: untitled ? qsTr("Untitled")
                                                        : MarkdownFormatter.toRichText(
                                                              cardItem.cardData.title,
                                                              root.cardMathPixelSize,
                                                              cardItem.cardData.done
                                                                  ? Theme.textFaint
                                                                  : Theme.textPrimary,
                                                              root.screenDevicePixelRatio,
                                                              Theme.link)
                                                    textFormat: untitled ? Text.PlainText
                                                                         : Text.RichText
                                                    wrapMode: Text.Wrap
                                                    font.pixelSize: root.cardFontSize
                                                    font.italic: untitled
                                                    font.strikeout: cardItem.cardData.done
                                                    color: untitled || cardItem.cardData.done
                                                        ? Theme.textFaint : Theme.textPrimary
                                                    onLinkActivated: function(link) {
                                                        AppActions.requestOpenLink(link)
                                                    }
                                                }
                                            }
                                        }

                                        // The card's labels and due date, in
                                        // their own strip under the title:
                                        // the place to read them, and the
                                        // place to set them. A label can be
                                        // typed on the card's line as well —
                                        // that is the syntax the file holds —
                                        // but nothing about a line of text
                                        // says so, and there is no syntax a
                                        // reader would guess for a date.
                                        Flow {
                                            id: metaRow
                                            width: parent.width
                                            spacing: 4
                                            readonly property bool tagging:
                                                root.tagEditCol === cardItem.cardColIndex
                                                && root.tagEditIdx === cardItem.cardIndex
                                            // Present when the card has any of
                                            // this, and offered while the
                                            // pointer is on the card.
                                            visible: cardItem.cardData.labels.length > 0
                                                     || cardItem.cardData.due !== ""
                                                     || cardHover.hovered
                                                     || metaRow.tagging

                                            Repeater {
                                                model: cardItem.cardData.labels
                                                delegate: Rectangle {
                                                    id: cardLabelChip
                                                    required property var modelData
                                                    objectName: "kanbanCardLabel"
                                                    height: 16
                                                    radius: 8
                                                    width: cardLabelText.implicitWidth
                                                           + (labelChipHover.hovered ? 24 : 12)
                                                    color: Qt.alpha(
                                                        root.labelColor(cardLabelChip.modelData),
                                                        labelChipHover.hovered ? 0.35 : 0.2)
                                                    Text {
                                                        id: cardLabelText
                                                        x: 6
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        text: "#" + cardLabelChip.modelData
                                                        font.pixelSize: 9
                                                        color: root.labelColor(cardLabelChip.modelData)
                                                    }
                                                    Text {
                                                        visible: labelChipHover.hovered
                                                        anchors.right: parent.right
                                                        anchors.rightMargin: 5
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        text: "×"
                                                        font.pixelSize: 11
                                                        color: Theme.textMuted
                                                    }
                                                    HoverHandler {
                                                        id: labelChipHover
                                                        cursorShape: Qt.PointingHandCursor
                                                    }
                                                    TapHandler {
                                                        gesturePolicy: TapHandler.ReleaseWithinBounds
                                                        onTapped: root.removeLabel(
                                                            cardItem.cardColIndex,
                                                            cardItem.cardIndex,
                                                            cardLabelChip.modelData)
                                                    }
                                                    ToolTip.visible: labelChipHover.hovered
                                                    ToolTip.text: qsTr("Remove #%1")
                                                        .arg(cardLabelChip.modelData)
                                                    ToolTip.delay: 400
                                                }
                                            }

                                            // The tag being typed, with the
                                            // board's existing labels offered
                                            // under it. One field exists at a
                                            // time, on the card taking a tag.
                                            Loader {
                                                active: metaRow.tagging
                                                sourceComponent: tagFieldComponent
                                            }

                                            KanbanChip {
                                                objectName: "kanbanAddLabel"
                                                visible: cardHover.hovered && !metaRow.tagging
                                                label: qsTr("+ tag")
                                                tip: qsTr("Add a label, reusing one from this board")
                                                onActivated: root.beginTagEdit(
                                                    cardItem.cardColIndex, cardItem.cardIndex)
                                            }

                                            KanbanChip {
                                                objectName: "kanbanCardDue"
                                                visible: cardItem.cardData.due !== ""
                                                         || cardHover.hovered
                                                filled: cardItem.cardData.due !== ""
                                                label: cardItem.cardData.due !== ""
                                                    ? "◷ " + cardItem.cardData.due
                                                    : qsTr("+ due date")
                                                tip: cardItem.cardData.due !== ""
                                                    ? qsTr("Change or clear the due date")
                                                    : qsTr("Pick a due date")
                                                onActivated: duePicker.openFor(
                                                    cardItem.cardColIndex, cardItem.cardIndex)
                                            }
                                        }

                                        // The description: every line of it,
                                        // with its formulas and wiki-links
                                        // rendered, under the title where a
                                        // card's detail belongs. An empty one
                                        // offers itself while the pointer is
                                        // on the card.
                                        Item {
                                            id: descArea
                                            width: parent.width
                                            readonly property bool hasText:
                                                cardItem.cardData.description.length > 0
                                            readonly property bool showPlaceholder:
                                                !descArea.hasText && !cardItem.editingDescription
                                                && cardHover.hovered
                                            implicitHeight: cardItem.editingDescription
                                                ? Math.max(16, root.activeEditorHeight)
                                                : (descArea.hasText ? descText.contentHeight
                                                   : (descArea.showPlaceholder ? 14 : 0))
                                            height: implicitHeight
                                            visible: cardItem.editingDescription
                                                     || implicitHeight > 0

                                            readonly property bool isActiveField:
                                                cardItem.editingDescription
                                            onIsActiveFieldChanged: {
                                                if (isActiveField)
                                                    root.activeFieldItem = descArea
                                                else if (root.activeFieldItem === descArea)
                                                    root.activeFieldItem = null
                                            }
                                            Component.onDestruction: {
                                                if (root && root.activeFieldItem === descArea)
                                                    root.activeFieldItem = null
                                            }

                                            Text {
                                                id: descText
                                                visible: descArea.hasText
                                                         && !cardItem.editingDescription
                                                width: parent.width
                                                text: MarkdownFormatter.toRichText(
                                                          cardItem.cardData.description,
                                                          root.cardMathPixelSize,
                                                          Theme.textSecondary,
                                                          root.screenDevicePixelRatio,
                                                          Theme.link)
                                                textFormat: Text.RichText
                                                wrapMode: Text.Wrap
                                                font.pixelSize: root.cardFontSize - 1
                                                color: Theme.textSecondary
                                                onLinkActivated: function(link) {
                                                    AppActions.requestOpenLink(link)
                                                }
                                            }
                                            Text {
                                                visible: descArea.showPlaceholder
                                                width: parent.width
                                                text: qsTr("Add a description")
                                                font.pixelSize: root.cardFontSize - 1
                                                font.italic: true
                                                color: Theme.textFaint
                                            }
                                        }

                                        // When the card was added and when it
                                        // was last changed. A card written
                                        // before the board kept either has
                                        // nothing to say here and says
                                        // nothing, rather than claiming a day.
                                        Text {
                                            objectName: "kanbanCardDates"
                                            width: parent.width
                                            visible: cardItem.cardData.created !== ""
                                                     || cardItem.cardData.modified !== ""
                                            elide: Text.ElideRight
                                            font.pixelSize: 9
                                            color: Theme.textFaint
                                            text: {
                                                var created = cardItem.cardData.created
                                                var modified = cardItem.cardData.modified
                                                if (created !== "" && modified !== ""
                                                    && modified !== created)
                                                    return qsTr("Created %1 · modified %2")
                                                        .arg(created).arg(modified)
                                                if (created !== "")
                                                    return qsTr("Created %1").arg(created)
                                                return qsTr("Modified %1").arg(modified)
                                            }
                                        }
                                    }
                                }
                            }

                            // An empty column still has to be a place a card
                            // can be dropped, and has to say so.
                            Text {
                                visible: !columnItem.isCollapsed
                                         && columnItem.visibleIndices.length === 0
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: root.filtering && columnItem.colData.cards.length > 0
                                    ? qsTr("No cards match the filter")
                                    : qsTr("Drop a card here")
                                font.pixelSize: 11
                                font.italic: true
                                color: Theme.textFaint
                            }

                            // What the card window is holding back in this
                            // column, and the way past it.
                            Text {
                                objectName: "kanbanCardWindowNotice"
                                visible: columnItem.hiddenCards > 0
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: qsTr("%n more card(s) — show all", "",
                                           columnItem.hiddenCards)
                                font.pixelSize: 11
                                color: showAllCards.hovered ? Theme.accent : Theme.link
                                font.underline: showAllCards.hovered
                                HoverHandler {
                                    id: showAllCards
                                    cursorShape: Qt.PointingHandCursor
                                }
                                TapHandler {
                                    onTapped: root.revealAllCardsIn(
                                        columnItem.colData.name,
                                        columnItem.visibleIndices.length)
                                }
                            }

                            KanbanTextButton {
                                objectName: "kanbanAddCardFooter"
                                visible: !columnItem.isCollapsed
                                width: parent.width
                                label: qsTr("+ Add card")
                                tip: qsTr("Add a card to %1").arg(columnItem.colData.name)
                                outlined: false
                                onActivated: root.addCardAndEdit(columnItem.colIndex)
                            }
                        }
                    }
                }

                // Add-column affordance.
                KanbanTextButton {
                    objectName: "kanbanAddColumn"
                    width: 120
                    height: 40
                    label: qsTr("+ Column")
                    tip: qsTr("Add a column and name it")
                    onActivated: root.addColumnAndRename()
                }
            }
        }
    }

    // The one editor for the whole board, reparented into whichever card
    // field is live. It carries the block's only BlockEditorEngine, so a
    // hundred cards cost one engine rather than a hundred.
    Loader {
        id: cardFieldEditor
        parent: root.activeFieldItem !== null ? root.activeFieldItem : root
        active: root.activeFieldItem !== null
        anchors.fill: parent
        sourceComponent: cardFieldEditorComponent
    }

    Component {
        id: cardFieldEditorComponent
        Item {
            id: editorRoot
            readonly property int colIndex: root.editCol
            readonly property int cardIdx: root.editIdx
            readonly property bool onDescription: root.editField === "description"
            readonly property string sourceText: editorRoot.onDescription
                ? root.cardDescriptionText(editorRoot.colIndex, editorRoot.cardIdx)
                : root.cardLineText(editorRoot.colIndex, editorRoot.cardIdx)

            // Focus and caret placement used to come with a freshly created
            // per-field editor. One editor for the whole board stays loaded
            // while it moves between fields, so it re-applies them itself: on
            // creation for the first field, and on every move after that.
            function beginEditing() {
                cardArea.forceActiveFocus()
                cardArea.cursorPosition = cardArea.length
            }
            // The document is handed to the engine only once the TextArea has
            // finished building, which is what EditableBlock's editorActive
            // gate does for prose blocks: a TextArea applies its own (empty)
            // text to its document as it completes, and an engine attached
            // before that reads the write as the reader having emptied the
            // field.
            property bool editorReady: false
            Component.onCompleted: {
                editorRoot.editorReady = true
                beginEditing()
            }
            Connections {
                target: root
                function onActiveFieldItemChanged() {
                    if (root.activeFieldItem !== null)
                        editorRoot.beginEditing()
                }
            }

            // Drive the live field's height off what is being typed, so a
            // card grows to fit its text rather than clipping it.
            Binding {
                target: root
                property: "activeEditorHeight"
                value: cardArea.contentHeight
            }

            BlockEditorEngine {
                id: cardEngine
                document: editorRoot.editorReady ? cardArea.textDocument : null
                markdown: editorRoot.sourceText
                cursorPosition: cardArea.cursorPosition
                cursorActive: cardArea.activeFocus
                theme: root.appThemeRef
                linkResolver: DocumentOutline
                // With no collection open every [[wiki-link]] styles as an
                // ordinary link rather than all rendering "unresolved".
                wikiResolver: NoteCollection.isOpen ? NoteCollection : null
                contentFontPixelSize: root.cardFontSize
                onMarkdownEdited: function(md) {
                    if (root.isPooled)
                        return
                    if (editorRoot.onDescription)
                        root.commitCardDescription(editorRoot.colIndex,
                                                   editorRoot.cardIdx, md)
                    else
                        root.commitCardLine(editorRoot.colIndex,
                                            editorRoot.cardIdx, md)
                }
            }

            TextArea {
                id: cardArea
                objectName: "kanbanCardTextEditor"
                anchors.fill: parent
                background: null
                wrapMode: TextEdit.Wrap
                // No inset at all: the field sits exactly where the rendered
                // text sat, so nothing shifts when a card goes live.
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                font.pixelSize: editorRoot.onDescription ? root.cardFontSize - 1
                                                         : root.cardFontSize
                color: Theme.textPrimary
                selectionColor: Theme.selectionActiveTint
                selectedTextColor: Theme.textPrimary
                selectByMouse: true

                onActiveFocusChanged: {
                    // Deferred: moving between a card's two fields drops
                    // focus for an instant before the next one takes it.
                    if (!activeFocus) {
                        cardMathEntry.releaseOnFocusLoss()
                        cardWikiCompletion.releaseOnFocusLoss()
                        Qt.callLater(root.endEditIfFocusLeft)
                    }
                }
                // The assisted-entry state follows the caret and the text,
                // both deferred for the reason EditableBlock defers them:
                // during an edit the caret signal arrives before the text
                // property catches up, and an immediate read would see a
                // half-applied snapshot and dismiss the menu.
                function settleMathEntryState() { cardMathEntry.settleState() }
                function syncWikiMenuQuery() { cardWikiCompletion.syncQuery() }
                onTextChanged: {
                    if (cardMathEntry.tracking())
                        Qt.callLater(settleMathEntryState)
                    if (cardWikiCompletion.activeMenu())
                        Qt.callLater(syncWikiMenuQuery)
                }
                onCursorPositionChanged: {
                    if (cardMathEntry.tracking())
                        Qt.callLater(settleMathEntryState)
                    if (cardWikiCompletion.activeMenu())
                        Qt.callLater(syncWikiMenuQuery)
                }
                // The shared menus call these on whichever editor they were
                // opened for, so both names have to live on the TextArea.
                function applyMathCommand(row) { cardMathEntry.applyCommand(row) }
                function applyWikiCompletion(row) { cardWikiCompletion.applyCompletion(row) }

                Keys.onPressed: function(event) {
                    // An open menu owns its navigation keys first — otherwise
                    // Tab would leave the field and Escape would end the edit
                    // while the menu was still up.
                    if (cardMathEntry.handleMenuKey(event))
                        return
                    if (cardWikiCompletion.handleMenuKey(event))
                        return
                    if (cardMathEntry.handleBackslash(event))
                        return
                    if (cardWikiCompletion.handleBracket(event))
                        return
                    if (cardMathEntry.handleEntryKey(event))
                        return
                    if (event.key === Qt.Key_Escape) {
                        root.endEdit(); event.accepted = true; return
                    }
                    if (event.key === Qt.Key_Backtab
                        || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        root.editPreviousField(); event.accepted = true; return
                    }
                    if (event.key === Qt.Key_Tab) {
                        root.editNextField(); event.accepted = true; return
                    }
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        // Enter belongs to an open menu while it is up: it
                        // takes the highlighted entry.
                        if (cardMathEntry.handleReturn(event))
                            return
                        if (cardWikiCompletion.handleReturn(event))
                            return
                        // Shift+Enter breaks the line inside a description —
                        // the split the table and callout blocks already
                        // use. A card's own line holds one line, so there is
                        // nothing there for a break to mean.
                        if (editorRoot.onDescription
                            && (event.modifiers & Qt.ShiftModifier))
                            return          // the TextArea inserts the break
                        root.endEdit(); event.accepted = true; return
                    }
                }
                // A relayout moves every equation without changing a
                // character, so the overlay is told to re-ask for its
                // rectangles.
                onContentHeightChanged: if (editorRoot.hasMath) editorRoot.mathTick++
                onContentWidthChanged: if (editorRoot.hasMath) editorRoot.mathTick++
            }

            // Typing mathematics and wiki-links in a card: the `$…$`
            // auto-pair, the backslash command menu, the Tab chain through a
            // template's slots, and the note picker the second `[` opens —
            // the same objects a prose block uses, so both are entered the
            // same way wherever they are being written.
            MathEntryAssist {
                id: cardMathEntry
                objectName: "kanbanCardMathEntry"
                editor: cardArea
                engine: cardEngine
                shell: root.shell
            }
            WikiLinkCompletion {
                id: cardWikiCompletion
                objectName: "kanbanCardWikiCompletion"
                editor: cardArea
                engine: cardEngine
                shell: root.shell
            }

            // The equations for this field's hidden `$…$` spans, drawn over
            // the transparent boxes the engine reserves in their place — the
            // same layer the prose blocks use.
            property int mathTick: 0
            readonly property var mathBoxes: {
                var dep = cardArea.text
                var dep2 = editorRoot.mathTick
                return cardEngine.inlineMathBoxes()
            }
            readonly property bool hasMath: editorRoot.mathBoxes.length > 0

            Loader {
                active: editorRoot.hasMath
                anchors.fill: parent
                sourceComponent: cardMathOverlayComponent
            }
            Component {
                id: cardMathOverlayComponent
                InlineMathOverlay {
                    anchors.fill: parent
                    editor: cardArea
                    editorFont: cardArea.font
                    boxes: editorRoot.mathBoxes
                    tick: editorRoot.mathTick
                    textColor: Theme.textPrimary
                    pixelSize: root.cardMathPixelSize
                    verticalPadding: root.cardMathVerticalPadding
                    devicePixelRatio: root.screenDevicePixelRatio
                }
            }
        }
    }

    // The field a label is typed into, built into the chip row of whichever
    // card is taking one. Typing narrows the list under it; Enter takes the
    // highlighted label or the typed text, and Escape leaves the card alone.
    Component {
        id: tagFieldComponent
        TextField {
            id: tagField
            objectName: "kanbanTagField"
            width: 110
            height: 18
            padding: 2
            font.pixelSize: 10
            color: Theme.textPrimary
            placeholderText: qsTr("#tag")
            placeholderTextColor: Theme.textFaint
            selectionColor: Theme.selectionActiveTint
            selectedTextColor: Theme.textPrimary
            selectByMouse: true
            background: Rectangle {
                color: Theme.windowBackground
                border.color: Theme.accent
                border.width: 1
                radius: 8
            }
            Component.onCompleted: {
                tagField.forceActiveFocus()
                root.activeTagField = tagField
            }
            Component.onDestruction: {
                if (root && root.activeTagField === tagField)
                    root.activeTagField = null
            }
            onTextChanged: {
                root.tagQuery = tagField.text
                root.tagHighlight = -1
            }
            // Leaving the field with something typed keeps it, the way the
            // column-name field keeps a typed name. The list below does not
            // take focus, so choosing from it does not come through here.
            onActiveFocusChanged: if (!tagField.activeFocus) root.acceptTag()
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape) {
                    root.endTagEdit(); event.accepted = true; return
                }
                if (event.key === Qt.Key_Down) {
                    root.moveTagHighlight(1); event.accepted = true; return
                }
                if (event.key === Qt.Key_Up) {
                    root.moveTagHighlight(-1); event.accepted = true; return
                }
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.acceptTag(); event.accepted = true; return
                }
            }
        }
    }

    // The labels this board already uses, under the field. It takes no focus
    // of its own, so a click on a row leaves the field's caret where it is and
    // the field's own focus handling never sees the choice as a departure.
    Popup {
        id: tagMenu
        objectName: "kanbanTagMenu"
        parent: root.activeTagField !== null ? root.activeTagField : root
        visible: root.activeTagField !== null && root.tagChoices.length > 0
        x: 0
        y: parent ? parent.height + 2 : 0
        padding: 2
        focus: false
        closePolicy: Popup.NoAutoClose
        background: Rectangle {
            color: Theme.popupBackground
            border.color: Theme.borderStrong
            border.width: 1
            radius: 4
        }
        contentItem: Column {
            spacing: 0
            Repeater {
                model: root.tagChoices
                delegate: Rectangle {
                    id: tagChoiceRow
                    required property int index
                    required property var modelData
                    objectName: "kanbanTagChoice"
                    width: 116
                    height: 18
                    radius: 3
                    color: root.tagHighlight === tagChoiceRow.index
                        ? Theme.selectionActiveTint
                        : (tagChoiceHover.hovered ? Theme.hoverTint : "transparent")
                    Text {
                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: "#" + tagChoiceRow.modelData
                        font.pixelSize: 10
                        color: root.labelColor(tagChoiceRow.modelData)
                    }
                    HoverHandler {
                        id: tagChoiceHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: root.commitTag(tagChoiceRow.modelData)
                    }
                }
            }
        }
    }

    // The due-date calendar, opened from a card's date chip. The board writes
    // the day it answers with straight into the card, so the one date the
    // storage grammar is strict about is never typed.
    DayPicker {
        id: duePicker
        objectName: "kanbanDuePicker"
        anchors.centerIn: Overlay.overlay
        property int col: -1
        property int idx: -1
        function openFor(c, i) {
            var card = root.cardAt(c, i)
            if (!card)
                return
            duePicker.col = c
            duePicker.idx = i
            duePicker.openAt(card.due)
        }
        onDayPicked: function(day) {
            root.setCardDue(duePicker.col, duePicker.idx, day)
        }
        onDayCleared: root.setCardDue(duePicker.col, duePicker.idx, "")
    }

    // Right-click a card: everything the card can be told to do, including
    // the two that no pointer gesture can reach — moving it to a named column
    // and deleting it.
    Menu {
        id: cardMenu
        objectName: "kanbanCardMenu"
        property int col: -1
        property int idx: -1
        readonly property bool cardDone: {
            if (cardMenu.col < 0 || cardMenu.col >= root.columns.length)
                return false
            var cards = root.columns[cardMenu.col].cards
            return cardMenu.idx >= 0 && cardMenu.idx < cards.length
                && cards[cardMenu.idx].done
        }
        function openFor(c, i) {
            cardMenu.col = c
            cardMenu.idx = i
            cardMenu.popup()
        }

        MenuItem {
            objectName: "kanbanCardEditText"
            text: qsTr("Edit card text")
            onTriggered: root.beginEdit(cardMenu.col, cardMenu.idx, "title")
        }
        MenuItem {
            objectName: "kanbanCardEditDescription"
            text: qsTr("Edit description")
            onTriggered: root.beginEdit(cardMenu.col, cardMenu.idx, "description")
        }
        MenuItem {
            objectName: "kanbanCardToggleDone"
            text: cardMenu.cardDone ? qsTr("Mark as not done") : qsTr("Mark as done")
            onTriggered: root.toggleCardDone(cardMenu.col, cardMenu.idx)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "kanbanCardDetails"
            text: qsTr("Labels and due date…")
            onTriggered: cardDetails.openFor(cardMenu.col, cardMenu.idx)
        }
        Menu {
            id: moveToMenu
            objectName: "kanbanCardMoveMenu"
            title: qsTr("Move to column")
            Repeater {
                model: root.columns.length
                delegate: MenuItem {
                    id: moveToItem
                    required property int index
                    objectName: "kanbanMoveToItem"
                    text: root.columnName(moveToItem.index)
                    enabled: moveToItem.index !== cardMenu.col
                    onTriggered: root.moveCardToColumn(cardMenu.col, cardMenu.idx,
                                                       moveToItem.index)
                }
            }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "kanbanCardDelete"
            text: qsTr("Delete card")
            onTriggered: root.removeCardAt(cardMenu.col, cardMenu.idx)
        }
    }

    // The card-details popover: the fields that are structured rather than
    // typed — the labels, the due date the storage grammar is strict about —
    // and the moves a pointer gesture cannot reach. The card's text is edited
    // on the card itself.
    //
    // Its controls carry their own colors. The Fusion style the application
    // sets follows the desktop palette rather than the note theme, so a
    // field left to the style renders light-on-light in a dark theme.
    Popup {
        id: cardDetails
        objectName: "kanbanCardEditor"
        anchors.centerIn: Overlay.overlay
        width: 320
        modal: true
        focus: true
        padding: 12
        property int col: -1
        property int idx: -1
        // A due value the storage grammar cannot hold is refused here rather
        // than dropped on save: the board line only carries `📅 YYYY-MM-DD`,
        // so anything else would come back as title text with the due date
        // cleared. Empty means "no due date", which is always allowed.
        readonly property bool dueValid: dueField.text.trim().length === 0
                                         || KanbanTools.isValidDue(dueField.text.trim())
        background: Rectangle { color: Theme.popupBackground; border.color: Theme.borderStrong; border.width: 1; radius: 8 }
        function openFor(c, i) {
            if (c < 0 || c >= root.columns.length)
                return
            var cards = root.columns[c].cards
            if (i < 0 || i >= cards.length)
                return
            cardDetails.col = c
            cardDetails.idx = i
            labelsField.text = cards[i].labels.join(", ")
            dueField.text = cards[i].due
            cardDetails.open()
        }
        function save() {
            if (!cardDetails.dueValid)
                return
            var card = root.columns[cardDetails.col].cards[cardDetails.idx]
            var labels = labelsField.text.split(",")
                .map(function(s){return s.trim()})
                .filter(function(s){return s.length})
            // The title and the description are the card's own text, edited
            // on the card; this writes only what the popover holds.
            root.writeBoard(KanbanTools.setCard(root.content, cardDetails.col,
                cardDetails.idx, card.title, card.done, labels,
                dueField.text.trim(), card.description, root.today()))
            cardDetails.close()
        }
        // Move the card to the end of another column. Dragging is the primary
        // way to move a card; this keyboard/click path guarantees the same
        // reordering without a pointer gesture (accessibility and headless
        // reach), as one undo step.
        function moveToColumn(targetCol) {
            root.moveCardToColumn(cardDetails.col, cardDetails.idx, targetCol)
            cardDetails.close()
        }
        contentItem: Column {
            spacing: 6
            Text {
                text: qsTr("Card details")
                font.bold: true
                color: Theme.textPrimary
            }
            TextField {
                id: labelsField
                width: parent.width
                placeholderText: qsTr("Labels (comma separated)")
                color: Theme.textPrimary
                placeholderTextColor: Theme.textFaint
                selectionColor: Theme.selectionActiveTint
                selectedTextColor: Theme.textPrimary
                background: Rectangle {
                    color: Theme.windowBackground
                    border.color: Theme.border
                    border.width: 1
                    radius: 3
                }
            }
            TextField {
                id: dueField
                width: parent.width
                placeholderText: qsTr("Due date (YYYY-MM-DD)")
                color: Theme.textPrimary
                placeholderTextColor: Theme.textFaint
                selectionColor: Theme.selectionActiveTint
                selectedTextColor: Theme.textPrimary
                background: Rectangle {
                    color: Theme.windowBackground
                    border.color: cardDetails.dueValid ? Theme.border : Theme.danger
                    border.width: 1
                    radius: 3
                }
            }
            Text {
                objectName: "kanbanDueError"
                visible: !cardDetails.dueValid
                text: qsTr("Enter a date as YYYY-MM-DD, or leave it empty")
                font.pixelSize: 11
                color: Theme.textMuted
            }
            Text {
                text: qsTr("Move to column")
                font.pixelSize: 11
                color: Theme.textMuted
            }
            Flow {
                width: parent.width
                spacing: 4
                Repeater {
                    model: root.columns.length
                    delegate: KanbanTextButton {
                        id: moveToButton
                        required property int index
                        objectName: "kanbanMoveTo"
                        label: root.columnName(moveToButton.index)
                        tip: qsTr("Move this card to the end of %1")
                                .arg(root.columnName(moveToButton.index))
                        opacity: moveToButton.index === cardDetails.col ? 0.4 : 1
                        onActivated: {
                            if (moveToButton.index !== cardDetails.col)
                                cardDetails.moveToColumn(moveToButton.index)
                        }
                    }
                }
            }
            Row {
                spacing: 6
                KanbanTextButton {
                    label: qsTr("Save")
                    opacity: cardDetails.dueValid ? 1 : 0.4
                    onActivated: cardDetails.save()
                }
                KanbanTextButton {
                    label: qsTr("Delete card")
                    tip: qsTr("Remove this card (Ctrl+Z undoes it)")
                    onActivated: {
                        root.removeCardAt(cardDetails.col, cardDetails.idx)
                        cardDetails.close()
                    }
                }
                KanbanTextButton {
                    label: qsTr("Cancel")
                    onActivated: cardDetails.close()
                }
            }
        }
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
