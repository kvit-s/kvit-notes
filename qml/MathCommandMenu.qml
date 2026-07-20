// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Kvit 1.0

// The math-command menu: the popup a backslash keystroke opens on both
// math-editing surfaces — a MathBlock's source editor and a revealed
// inline $…$ span in a prose block. Like BlockMenu it is one passive
// Popup: it NEVER takes focus — keystrokes keep flowing into the host
// editor (which is what feeds the query), and the host forwards
// navigation keys here while the menu targets it.
//
// Two modes, one popup: an empty query shows the browse panel (category
// list + rendered-glyph grid, the LyX-toolbar analog); a non-empty query
// shows the flat ranked completion list. Rows come ready-made from the
// C++ MathCommandModel; this file owns no matching logic. Selecting an
// entry hands the row to the host's applyMathCommand(row) — insertion
// semantics (template, caret slot, Tab chain) live with the editor.
Popup {
    id: menu
    objectName: "mathCommandMenu"

    // The host editor being served (the item exposing applyMathCommand),
    // or null while closed.
    property var host: null
    // Display-math context: environment entries insert their multi-line
    // form (insertDisplay) instead of the single-line one.
    property bool displayContext: false
    // Text typed between the trigger backslash and the caret.
    property string query: ""
    readonly property bool completionMode: query.length > 0

    // Anchor in window coordinates: the caret rectangle.
    property rect anchorRect: Qt.rect(0, 0, 0, 0)

    // ---- Browse-mode state ----
    property var cats: []
    property int categoryIndex: 0
    property var gridRows: []
    property int gridIndex: -1
    // Keyboard focus pane: false = glyph grid (default), true = categories.
    property bool inCategoryPane: false
    readonly property int gridColumns: 8
    readonly property int cellSize: 44

    // ---- Completion-mode state ----
    property var rows: []
    property int highlightIndex: -1

    readonly property int glyphPixelSize: 15

    modal: false
    dim: false
    focus: false
    closePolicy: Popup.CloseOnPressOutside
    parent: Overlay.overlay
    padding: 4
    width: completionMode ? 320 : 470

    // Near the caret, clamped into the viewport; flips above the caret
    // when the space below would clip (BlockMenu's rule).
    x: {
        var ow = parent ? parent.width : 0
        return Math.max(4, Math.min(anchorRect.x, ow - width - 4))
    }
    y: {
        var oh = parent ? parent.height : 0
        var below = anchorRect.y + anchorRect.height + 4
        if (below + height > oh && anchorRect.y - height - 4 >= 0)
            return anchorRect.y - height - 4
        return Math.max(4, Math.min(below, oh - height - 4))
    }

    // aarrggbb hex for the image://math/ query (MathBlock's helper).
    function argbHex(c) {
        function h(x) { return ("0" + Math.round(x * 255).toString(16)).slice(-2) }
        return h(c.a) + h(c.r) + h(c.g) + h(c.b)
    }
    function glyphSource(tex) {
        if (!tex || tex.length === 0)
            return ""
        return "image://math/" + MathRenderer.encode(tex)
             + "?fg=" + argbHex(theme.textPrimary)
             + "&size=" + menu.glyphPixelSize
             + "&dpr=" + (Screen.devicePixelRatio > 0
                          ? Math.round(Screen.devicePixelRatio * 100) / 100 : 1)
    }

    function openForHost(hostItem, rect, display) {
        host = hostItem
        displayContext = display === true
        anchorRect = rect
        query = ""
        cats = MathCommandModel.categories()
        categoryIndex = 0
        inCategoryPane = false
        reloadGrid()
        refilter()
        open()
    }

    function targets(hostItem) {
        return opened && host === hostItem
    }

    // Fed by the host on every relevant edit while the menu is open.
    // A query that matches nothing closes the menu (the typed character
    // stays in the source).
    function updateQuery(text) {
        query = text
        if (completionMode) {
            refilter()
            if (rows.length === 0)
                dismiss()
        } else {
            reloadGrid()
        }
    }

    function refilter() {
        rows = completionMode ? MathCommandModel.itemsFor(query) : []
        highlightIndex = rows.length > 0 ? 0 : -1
        if (completionList)
            completionList.positionViewAtBeginning()
    }

    function reloadGrid() {
        gridRows = cats.length > 0
            ? MathCommandModel.itemsForCategory(cats[categoryIndex]) : []
        gridIndex = gridRows.length > 0 ? 0 : -1
    }

    function selectCategory(index) {
        if (index < 0 || index >= cats.length)
            return
        categoryIndex = index
        reloadGrid()
    }

    // ---- Key forwarding targets (hosts call these) ----
    function highlightNext() {
        if (completionMode) {
            if (rows.length > 0) {
                highlightIndex = (highlightIndex + 1) % rows.length
                if (completionList)
                    completionList.positionViewAtIndex(highlightIndex, ListView.Contain)
            }
        } else if (inCategoryPane) {
            selectCategory(Math.min(categoryIndex + 1, cats.length - 1))
        } else if (gridRows.length > 0) {
            gridIndex = Math.min(gridIndex + gridColumns, gridRows.length - 1)
        }
    }
    function highlightPrevious() {
        if (completionMode) {
            if (rows.length > 0) {
                highlightIndex = (highlightIndex - 1 + rows.length) % rows.length
                if (completionList)
                    completionList.positionViewAtIndex(highlightIndex, ListView.Contain)
            }
        } else if (inCategoryPane) {
            selectCategory(Math.max(categoryIndex - 1, 0))
        } else if (gridRows.length > 0) {
            gridIndex = Math.max(gridIndex - gridColumns, 0)
        }
    }
    // Left/Right are browse-mode-only (in completion mode the host closes
    // the menu and lets the caret move). Returns true when consumed.
    function moveLeft() {
        if (completionMode)
            return false
        if (inCategoryPane)
            return true
        if (gridIndex % gridColumns === 0) {
            inCategoryPane = true
            return true
        }
        gridIndex = Math.max(gridIndex - 1, 0)
        return true
    }
    function moveRight() {
        if (completionMode)
            return false
        if (inCategoryPane) {
            inCategoryPane = false
            return true
        }
        if (gridRows.length > 0)
            gridIndex = Math.min(gridIndex + 1, gridRows.length - 1)
        return true
    }

    function applyHighlighted() {
        if (completionMode) {
            if (highlightIndex >= 0 && highlightIndex < rows.length)
                applyRow(rows[highlightIndex])
            return
        }
        if (inCategoryPane) {  // Enter on a category focuses its grid
            inCategoryPane = false
            return
        }
        if (gridIndex >= 0 && gridIndex < gridRows.length)
            applyRow(gridRows[gridIndex])
    }

    // Insertion is the host's job (it owns the text and the slot chain);
    // recency is recorded here so both surfaces share it. Dismiss precedes
    // the insertion, mirroring BlockMenu.
    function applyRow(row) {
        if (!row || row.kind !== "entry")
            return
        var h = host
        MathCommandModel.noteUsed(row.name)
        dismiss()
        if (h && h.applyMathCommand)
            h.applyMathCommand(row)
    }

    function dismiss() {
        host = null
        close()
    }

    background: Rectangle {
        color: theme.popupBackground
        border.color: theme.borderStrong
        border.width: 1
        radius: 6
    }

    contentItem: Item {
        implicitWidth: menu.width - menu.leftPadding - menu.rightPadding
        implicitHeight: menu.completionMode
            ? (menu.rows.length === 0
               ? 40 : Math.min(menu.rows.length * 36, 324))
            : browsePanel.height

        // ---- Completion list ----
        Text {
            objectName: "mathMenuNoMatches"
            visible: menu.completionMode && menu.rows.length === 0
            anchors.centerIn: parent
            text: qsTr("No matches")
            color: theme.textFaint
            font.pixelSize: 13
        }

        ListView {
            id: completionList
            objectName: "mathMenuCompletionList"
            anchors.fill: parent
            visible: menu.completionMode
            clip: true
            interactive: contentHeight > height
            model: menu.completionMode ? menu.rows : []

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: completionRow
                required property var modelData
                required property int index

                width: completionList.width
                height: 36
                radius: 4
                color: index === menu.highlightIndex ? theme.focusTint
                                                     : "transparent"

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 10

                    // The rendered glyph — pixel-honest via image://math.
                    Item {
                        width: 34
                        height: 28
                        anchors.verticalCenter: parent.verticalCenter
                        Image {
                            anchors.centerIn: parent
                            source: menu.glyphSource(completionRow.modelData.preview)
                            width: Math.min(implicitWidth, 32)
                            height: Math.min(implicitHeight, 26)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            cache: true
                            asynchronous: true
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: completionRow.modelData.name
                        color: theme.textPrimary
                        font.family: "monospace"
                        font.pixelSize: 13
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: completionRow.modelData.category
                        color: theme.textFaint
                        font.pixelSize: 11
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: menu.highlightIndex = completionRow.index
                    onClicked: menu.applyRow(completionRow.modelData)
                }
            }
        }

        // ---- Browse panel: categories | glyph grid + name echo ----
        Row {
            id: browsePanel
            visible: !menu.completionMode
            width: parent.width
            height: 292
            spacing: 6

            ListView {
                id: categoryList
                objectName: "mathMenuCategoryList"
                width: 132
                height: browsePanel.height
                clip: true
                interactive: contentHeight > height
                model: menu.cats

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: categoryRow
                    required property string modelData
                    required property int index

                    width: categoryList.width
                    height: 26
                    radius: 4
                    color: index === menu.categoryIndex
                           ? (menu.inCategoryPane ? theme.focusTint
                                                  : theme.hoverTint)
                           : "transparent"

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: categoryRow.modelData
                        color: categoryRow.index === menu.categoryIndex
                               ? theme.textPrimary : theme.textSecondary
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: menu.selectCategory(categoryRow.index)
                        onClicked: {
                            menu.selectCategory(categoryRow.index)
                            menu.inCategoryPane = false
                        }
                    }
                }
            }

            Rectangle { width: 1; height: browsePanel.height; color: theme.border }

            Column {
                width: browsePanel.width - categoryList.width - 13
                spacing: 4

                GridView {
                    id: glyphGrid
                    objectName: "mathMenuGlyphGrid"
                    width: parent.width
                    height: browsePanel.height - 26
                    clip: true
                    interactive: contentHeight > height
                    cellWidth: Math.floor(width / menu.gridColumns)
                    cellHeight: menu.cellSize
                    model: menu.gridRows

                    onModelChanged: positionViewAtBeginning()

                    delegate: Rectangle {
                        id: gridCell
                        required property var modelData
                        required property int index

                        width: glyphGrid.cellWidth - 2
                        height: glyphGrid.cellHeight - 2
                        radius: 4
                        color: index === menu.gridIndex && !menu.inCategoryPane
                               ? theme.focusTint : "transparent"
                        border.color: index === menu.gridIndex
                                      ? theme.accent : "transparent"
                        border.width: index === menu.gridIndex ? 1 : 0

                        // Rendered glyph; entries with no preview (\\, &)
                        // show their name as text instead.
                        Image {
                            anchors.centerIn: parent
                            visible: gridCell.modelData.preview !== ""
                            source: menu.glyphSource(gridCell.modelData.preview)
                            width: Math.min(implicitWidth, parent.width - 6)
                            height: Math.min(implicitHeight, parent.height - 6)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            cache: true
                            asynchronous: true
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: gridCell.modelData.preview === ""
                            text: gridCell.modelData.name
                            color: theme.textPrimary
                            font.family: "monospace"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                menu.inCategoryPane = false
                                menu.gridIndex = gridCell.index
                            }
                            onClicked: menu.applyRow(gridCell.modelData)
                        }
                    }
                }

                // Name echo: the highlighted entry's command and meaning.
                Text {
                    objectName: "mathMenuNameEcho"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: {
                        if (menu.gridIndex < 0 || menu.gridIndex >= menu.gridRows.length)
                            return ""
                        var row = menu.gridRows[menu.gridIndex]
                        return row.description !== ""
                            ? row.name + "  —  " + row.description : row.name
                    }
                    color: theme.textMuted
                    font.pixelSize: 12
                }
            }
        }
    }
}
