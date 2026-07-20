// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The block-type menu (features.md §4), serving both the slash command
// and the gutter plus-button. It is one passive Popup: it NEVER takes
// focus — keystrokes keep flowing into the target block (which is what
// filters the menu), and the target delegate forwards Up/Down/Enter/Escape
// here while the menu targets it. Rows come ready-made from the C++
// BlockMenuModel catalog; this file owns no matching logic.
Popup {
    id: menu
    objectName: "blockMenu"

    // The block the menu is operating on, or -1 while closed.
    property int targetIndex: -1
    // "slash": opened by typing "/" on an empty block — the filter is
    // the content after the slash, and the menu closes when the slash
    // is deleted. "insert": opened by the gutter plus-button on a fresh
    // empty block — the whole content is the filter.
    property string mode: "slash"
    property string query: ""
    // Display rows from BlockMenuModel.itemsFor(): header and entry maps
    property var rows: []
    // Index into rows of the highlighted entry (never a header), -1 if
    // there are no matches
    property int highlightIndex: -1

    // Anchor in window coordinates: the text cursor rectangle the menu
    // opens beneath (§4.3 "position menu near cursor").
    property rect anchorRect: Qt.rect(0, 0, 0, 0)

    // Emitted after a selection converted the target block, so the view
    // can re-establish focus by index (the DelegateChooser may have
    // recreated the delegate).
    signal applied(int blockIndex, int type)

    modal: false
    dim: false
    focus: false
    closePolicy: Popup.CloseOnPressOutside
    parent: Overlay.overlay
    padding: 4
    width: 300

    // Deterministic content height (variable row heights would make
    // ListView.contentHeight an estimate): entries 44px, headers 24px,
    // scrolling past 320px (§4.3 "scrollable when many options").
    readonly property int rowsHeight: {
        var h = 0
        for (var i = 0; i < rows.length; i++)
            h += rows[i].kind === "header" ? 24 : 44
        return h
    }

    // Near the cursor, clamped into the viewport; flips above the
    // cursor when the space below would clip (§4.3).
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

    function openForBlock(index, openMode, rect) {
        targetIndex = index
        mode = openMode
        query = ""
        anchorRect = rect
        refilter()
        open()
    }

    // Fed by the target delegate on every model content change while
    // the menu is open (the query lives in the block).
    function updateQuery(content) {
        if (mode === "slash") {
            if (content.length === 0 || content.charAt(0) !== "/"
                || content.indexOf("\n") !== -1) {
                dismiss()
                return
            }
            query = content.substring(1)
        } else {
            if (content.indexOf("\n") !== -1) {
                dismiss()
                return
            }
            query = content
        }
        refilter()
    }

    function refilter() {
        rows = BlockMenuModel.itemsFor(query)
        highlightIndex = firstEntryIndex()
        if (menuList)  // the content item may not be instantiated yet
            menuList.positionViewAtBeginning()
    }

    function firstEntryIndex() {
        for (var i = 0; i < rows.length; i++)
            if (rows[i].kind === "entry")
                return i
        return -1
    }

    function highlightStep(direction) {
        if (rows.length === 0)
            return
        var i = highlightIndex
        for (var n = 0; n < rows.length; n++) {
            i = (i + direction + rows.length) % rows.length
            if (rows[i].kind === "entry") {
                highlightIndex = i
                break
            }
        }
        if (menuList)
            menuList.positionViewAtIndex(highlightIndex, ListView.Contain)
    }

    function highlightNext() { highlightStep(1) }
    function highlightPrevious() { highlightStep(-1) }

    function applyHighlighted() {
        if (highlightIndex >= 0 && highlightIndex < rows.length)
            applyRow(rows[highlightIndex])
    }

    // Selecting an entry is one convertBlock: the target's
    // content — the typed "/query" or the plus-button filter text — is
    // cleared and the type set in a single undo step. Dismiss precedes
    // the conversion because the type change may recreate the target's
    // delegate.
    function applyRow(row) {
        if (!row || row.kind !== "entry" || targetIndex < 0)
            return
        var idx = targetIndex
        var type = row.type
        // By entry, not by type: five catalog entries are CodeBlock, so the
        // type alone would bring back "Code Block" for every one of them.
        // The "/code <language>" rows carry no entryId and are not recorded.
        if (row.entryId !== undefined)
            BlockMenuModel.noteUsedEntry(row.entryId)
        dismiss()
        // Media types insert rather than convert: an empty Image block has
        // no path, so the menu hands off to the insert flow. Image and Media
        // both insert via the shared file/URL dialog, which classifies the
        // path: a media extension lands a Media block, everything else an
        // Image.
        // A Web Embed prompts for a URL and inserts an
        // ![](url) image expression that classifies to the embed card.
        if (type === Block.Image && row.language === "embed") {
            var winE = Window.window
            if (winE && winE.insertEmbedIntoBlock)
                winE.insertEmbedIntoBlock(idx)
            applied(idx, type)
            return
        }
        if (type === Block.Image || type === Block.Media) {
            var win = Window.window
            if (win && win.insertImageIntoBlock)
                win.insertImageIntoBlock(idx)
            applied(idx, type)
            return
        }
        if (type === Block.Table) {
            var win2 = Window.window
            if (win2 && win2.insertTableIntoBlock)
                win2.insertTableIntoBlock(idx)
            applied(idx, type)
            return
        }
        // Drop cap (§1.2.16) is a paragraph attribute rather than a stored
        // type, so it sets dropcap=<lines> instead of changing the type. The
        // menu only ever opens on an empty block, so the block's content here
        // is the typed "/dropcap" query itself: it is cleared exactly as every
        // other entry clears it, and the enlarged initial then applies to the
        // text the user goes on to type. Three lines is the menu default; the
        // block context menu offers the other spans.
        if (type === Block.Paragraph && row.language === "dropcap") {
            blockModel.convertBlock(idx, Block.Paragraph, "", false, "")
            blockModel.setBlockAttributes(
                idx, blockAttributes.withValue(blockModel.getAttributes(idx),
                                               "dropcap", "3"))
            applied(idx, type)
            return
        }
        // A "/code <language>" row carries the language to seed.
        var lang = row.language !== undefined ? row.language : ""
        // A Task Board (kanban fence) starts with three empty columns so the
        // board renders something to drop cards into, all in the one
        // convertBlock undo step. A Table of Contents (toc fence) seeds with
        // the document's current headings so its stored body is correct from
        // insertion.
        var seed = lang === "kanban" ? "## To do\n## In progress\n## Done"
                 : (lang === "toc" ? documentOutline.tocMarkdown()
                 : (lang === "mermaid"
                    ? "flowchart LR\n"
                      + "  A[Start] --> B{Decision}\n"
                      + "  B -->|yes| C[Done]\n"
                      + "  B -->|no| A"
                 // Collection query: a commented starter spec the user
                 // uncomments and adapts.
                 : (lang === "query"
                    ? "# from: projects/\n"
                      + "# where: status = active\n"
                      + "view: table\n"
                      + "columns: title, tags, modified\n"
                      + "sort: modified desc"
                    : "")))
        blockModel.convertBlock(idx, type, seed, false, lang)
        applied(idx, type)
    }

    function dismiss() {
        targetIndex = -1
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
        implicitHeight: menu.rows.length === 0
                        ? noMatches.implicitHeight + 12
                        : Math.min(menu.rowsHeight, 320)

        Text {
            id: noMatches
            objectName: "blockMenuNoMatches"
            visible: menu.rows.length === 0
            anchors.centerIn: parent
            text: qsTr("No matches")
            color: theme.textFaint
            font.pixelSize: 13
        }

        ListView {
            id: menuList
            objectName: "blockMenuList"
            anchors.fill: parent
            clip: true
            interactive: contentHeight > height
            model: menu.rows

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: rowItem
                required property var modelData
                required property int index

                readonly property bool isEntry: modelData.kind === "entry"

                width: menuList.width
                height: isEntry ? 44 : 24

                // Group header (§4.3 "grouped by category")
                Text {
                    visible: !rowItem.isEntry
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.isEntry ? "" : modelData.text
                    color: theme.textFaint
                    font.pixelSize: 10
                    font.bold: true
                    font.capitalization: Font.AllUppercase
                }

                Rectangle {
                    visible: rowItem.isEntry
                    anchors.fill: parent
                    radius: 4
                    color: rowItem.isEntry && rowItem.index === menu.highlightIndex
                           ? theme.focusTint : "transparent"

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 10

                        // Icon badge (glyphs until an icon set lands)
                        Rectangle {
                            width: 28
                            height: 28
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 5
                            color: theme.chipBackground
                            border.color: theme.border
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: rowItem.isEntry ? modelData.icon : ""
                                color: theme.textSecondary
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1

                            Text {
                                text: rowItem.isEntry ? modelData.name : ""
                                color: theme.textPrimary
                                font.pixelSize: 13
                            }
                            Text {
                                text: rowItem.isEntry ? modelData.description : ""
                                color: theme.textFaint
                                font.pixelSize: 11
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: rowItem.isEntry
                        hoverEnabled: true
                        onEntered: menu.highlightIndex = rowItem.index
                        onClicked: menu.applyRow(rowItem.modelData)
                    }
                }
            }
        }
    }
}
