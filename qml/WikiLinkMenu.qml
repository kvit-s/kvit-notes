// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls

// The [[ completion popup (pre-launch-plan.md §3.5): opened when "[[" is
// typed in prose, listing the collection's notes through the shared fuzzy
// matcher (QuickSwitcherModel); typing "#" after a target narrows to that
// note's headings. Like the block menu it is PASSIVE — it never takes
// focus; keystrokes keep flowing into the host editor, which feeds the
// query and forwards Up/Down/Enter/Tab/Escape here while it targets that
// editor. Selection inserts through the host's applyWikiCompletion.
Popup {
    id: menu
    objectName: "wikiLinkMenu"

    // The TextArea being completed, or null while closed.
    property var host: null
    property string query: ""
    // Rows: {kind:"note", title, folder, target} or {kind:"heading", heading}
    property var rows: []
    property int highlightIndex: -1

    property rect anchorRect: Qt.rect(0, 0, 0, 0)

    modal: false
    dim: false
    focus: false
    closePolicy: Popup.CloseOnPressOutside
    parent: Overlay.overlay
    padding: 4
    width: 320

    readonly property int rowsHeight: rows.length * 40

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

    function targets(editor) {
        return host === editor
    }

    function openForHost(editor, rect) {
        host = editor
        anchorRect = rect
        query = ""
        refilter()
        open()
    }

    function updateQuery(value) {
        query = value
        refilter()
    }

    function refilter() {
        var hashIdx = query.indexOf("#")
        if (hashIdx >= 0) {
            // Heading narrowing: [[target#... lists the picked note's
            // headings; the bare [[#... form lists the open document's.
            var target = query.substring(0, hashIdx).trim()
            var headingQuery = query.substring(hashIdx + 1).toLowerCase()
            var heads = []
            if (target === "") {
                var own = documentOutline.headings()
                for (var i = 0; i < own.length; ++i)
                    heads.push(own[i].text)
            } else {
                var relPath = noteCollection.resolveWikiTarget(target)
                if (relPath !== "")
                    heads = noteCollection.headingsFor(relPath)
            }
            var headRows = []
            for (var h = 0; h < heads.length; ++h) {
                if (headingQuery === ""
                    || heads[h].toLowerCase().indexOf(headingQuery) >= 0)
                    headRows.push({ kind: "heading", heading: heads[h] })
            }
            rows = headRows
        } else {
            var items = quickSwitcherModel.itemsFor(query, 8)
            var noteRows = []
            for (var n = 0; n < items.length; ++n) {
                // The minimal unambiguous target: the bare title when it
                // resolves to this note, else the full path (".md" implied).
                var title = items[n].title
                var target2 = noteCollection.resolveWikiTarget(title)
                        === items[n].relPath
                    ? title
                    : items[n].relPath.replace(/\.md$/i, "")
                noteRows.push({ kind: "note", title: title,
                                folder: items[n].folder, target: target2 })
            }
            rows = noteRows
        }
        // With no matches the popup stays open showing its hint — the
        // typed text itself becomes a create-on-click link.
        highlightIndex = rows.length > 0 ? 0 : -1
    }

    function highlightStep(direction) {
        if (rows.length === 0)
            return
        highlightIndex = (highlightIndex + direction + rows.length)
                         % rows.length
        menuList.positionViewAtIndex(highlightIndex, ListView.Contain)
    }
    function highlightNext() { highlightStep(1) }
    function highlightPrevious() { highlightStep(-1) }

    function applyHighlighted() {
        if (highlightIndex < 0 || highlightIndex >= rows.length)
            return
        var row = rows[highlightIndex]
        var editor = host
        dismiss()
        if (editor && editor.applyWikiCompletion)
            editor.applyWikiCompletion(row)
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
        implicitHeight: menu.rows.length === 0
                        ? noMatches.implicitHeight + 12
                        : Math.min(menu.rowsHeight, 280)

        Text {
            id: noMatches
            visible: menu.rows.length === 0
            anchors.centerIn: parent
            text: qsTr("No matches — Enter keeps the typed link")
            color: theme.textFaint
            font.pixelSize: 12
        }

        ListView {
            id: menuList
            objectName: "wikiLinkMenuList"
            anchors.fill: parent
            clip: true
            interactive: contentHeight > height
            model: menu.rows

            delegate: Rectangle {
                required property var modelData
                required property int index
                width: menuList.width
                height: 40
                radius: 4
                color: index === menu.highlightIndex
                       ? theme.hoverTint : "transparent"

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    spacing: 0

                    Text {
                        width: parent.width
                        text: modelData.kind === "heading"
                              ? "# " + modelData.heading : modelData.title
                        color: theme.textPrimary
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: modelData.kind === "note"
                                 && modelData.folder !== ""
                        text: modelData.kind === "note"
                              ? modelData.folder : ""
                        color: theme.textFaint
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: menu.highlightIndex = index
                    onClicked: menu.applyHighlighted()
                }
            }
        }
    }
}
