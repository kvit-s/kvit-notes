// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The colour and span-type menus build their items from a Repeater, and each
// item is its own component scope. Binding them lets an item address itself
// by id instead of reaching modelData by injection.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Kvit 1.0

// The right-click menus (features.md §9.5), and the routing that decides
// which one a click gets: a press inside a selected block goes to the
// selection menu, a press on a link to the link menu, and anything else in
// text to the text menu. Each item triggers the same operation the keyboard
// path uses, so the two cannot drift apart.
//
// One instance of each menu serves every block. The block it is acting on is
// held in `target` while the menu is open, which is also what lets a delegate
// ask whether a menu is currently keeping its selection alive: a menu takes
// focus, and the delegate would otherwise deselect on focus loss.
//
// Every command marks its access key with `&` — the letter typed to run it
// while the menu is open, drawn underlined on Windows and Linux and taken out
// on macOS, which has no such convention. The letters have to be distinct
// within one menu, and tools/check-menu-access-keys.py — the MenuAccessKeyGuard
// test — is what keeps them that way. The one list here
// that is data rather than labels — the block types the "Turn into" submenu
// offers — goes through MenuText.plain() instead, so a `&` in a type's name
// stays an ampersand. See src/platform/menuaccesskeys.h.
Item {
    id: menus

    // Wired by main.qml: the toolbar for the block-type list the "Turn into"
    // submenu offers, and the block-selection key handler for the operations
    // the selection menu shares with it.
    property var toolbar
    property var selectionKeys
    property var appWindow

    function markdownForIndexes(indexes) {
        return DocumentSerializer.serializeBlocks(BlockModel, indexes)
    }

    // DocumentExporter keeps its media base between calls because collection
    // export advances it note by note. A direct Copy as HTML must therefore
    // point it back at the active document every time, including a loose file
    // whose folder is not a collection root.
    function prepareRenderContext() {
        var noteDir = appWindow && appWindow.currentNoteDir
            ? appWindow.currentNoteDir() : ""
        DocumentExporter.setImageContext(
            noteDir, NoteCollection.isOpen ? NoteCollection.rootPath : "")
    }

    // The ordinary copy is the same multi-flavour payload as Ctrl+C on a
    // block selection: structural Markdown for Kvit/plain targets and rendered
    // inline HTML for rich-text targets.
    function copyIndexes(indexes) {
        var markdown = markdownForIndexes(indexes)
        Clipboard.setMarkdown(markdown, MarkdownFormatter.toHtml(markdown))
    }

    // Copy-as deliberately writes source text in the requested format. It
    // does not attach Kvit's private Markdown marker, so pasting the HTML back
    // into the editor cannot be mistaken for a lossless internal copy.
    function copyIndexesAs(indexes, format) {
        var markdown = markdownForIndexes(indexes)
        if (format === "markdown") {
            Clipboard.text = markdown
        } else if (format === "text") {
            prepareRenderContext()
            Clipboard.text = DocumentExporter.plainTextForModelBlocks(
                BlockModel, indexes)
                .replace(/\n$/, "")
        } else if (format === "html") {
            prepareRenderContext()
            Clipboard.text = DocumentExporter.htmlForModelBlocks(
                BlockModel, indexes)
        }
    }

    function exportIndexes(indexes) {
        if (appWindow && appWindow.exportDialog)
            appWindow.exportDialog.openForBlocks(indexes)
    }

    // Whether an open menu is holding this target's selection.
    function holdsSelection(target) {
        return (textContextMenu.visible && textContextMenu.target === target)
            || (linkContextMenu.visible && linkContextMenu.target === target)
    }

    function openTextMenu(target) {
        if (DocumentSelection.hasBlockSelection
            && DocumentSelection.isBlockSelected(target.index)) {
            selectionContextMenu.target = target
            selectionContextMenu.popup()
            return
        }
        textContextMenu.target = target
        textContextMenu.popup()
    }

    function openLinkMenu(target) {
        linkContextMenu.target = target
        linkContextMenu.popup()
    }

    function openHandleMenu(target, keyboard) {
        var menu
        if (DocumentSelection.hasBlockSelection
            && DocumentSelection.isBlockSelected(target.index)) {
            selectionContextMenu.target = target
            menu = selectionContextMenu
        } else {
            blockContextMenu.target = target
            menu = blockContextMenu
        }
        if (keyboard) {
            // Position beside the gutter without reparenting the shared menu
            // to a pooled delegate. popup(target, ...) keeps that transient
            // parent; once the row is recycled the menu is parentless and no
            // later block can open it.
            var pos = target.mapToItem(menus, 40, 4)
            menu.popup(pos.x, pos.y)
        } else {
            menu.popup()
        }
    }

    Menu {
        id: textContextMenu
        objectName: "textContextMenu"
        property var target: null
        readonly property bool hasSel: target
            && target.selectionEndDoc > target.selectionStartDoc

        MenuItem {
            objectName: "ctxCut"
            text: MenuText.label(qsTr("Cu&t"))
            enabled: textContextMenu.hasSel
            onTriggered: textContextMenu.target.cutSelection()
        }
        MenuItem {
            objectName: "ctxCopy"
            text: MenuText.label(qsTr("&Copy"))
            enabled: textContextMenu.hasSel
            onTriggered: textContextMenu.target.copySelection()
        }
        MenuItem {
            objectName: "ctxPaste"
            text: MenuText.label(qsTr("&Paste"))
            enabled: Clipboard.hasText
            onTriggered: textContextMenu.target.pasteClipboard(false)
        }
        MenuItem {
            objectName: "ctxPastePlain"
            text: MenuText.label(qsTr("Paste as plain te&xt"))
            enabled: Clipboard.hasText
            onTriggered: textContextMenu.target.pasteClipboard(true)
        }
        MenuSeparator {}
        Menu {
            title: MenuText.label(qsTr("&Formatting"))
            enabled: textContextMenu.target
                     && !textContextMenu.target.verbatimEditing
            Repeater {
                model: [
                    { name: qsTr("&Bold"), type: "bold" },
                    { name: qsTr("&Italic"), type: "italic" },
                    { name: qsTr("&Underline"), type: "underline" },
                    { name: qsTr("&Strikethrough"), type: "strike" },
                    { name: qsTr("Inline &code"), type: "code" },
                    { name: qsTr("&Highlight"), type: "highlight" },
                    { name: qsTr("Su&perscript"), type: "superscript" },
                    { name: qsTr("Subsc&ript"), type: "subscript" },
                    { name: qsTr("Inline &math"), type: "math" }]
                MenuItem {
                    id: spanTypeItem
                    required property var modelData
                    text: MenuText.label(spanTypeItem.modelData.name)
                    onTriggered: textContextMenu.target.toggleSpanType(
                        spanTypeItem.modelData.type)
                }
            }
        }
        Menu {
            title: MenuText.label(qsTr("Text c&olor"))
            enabled: textContextMenu.target
                     && !textContextMenu.target.verbatimEditing
            Repeater {
                model: [
                    { name: qsTr("&Red"), value: "#e05c5c" },
                    { name: qsTr("&Orange"), value: "#e0a04c" },
                    { name: qsTr("&Green"), value: "#58a866" },
                    { name: qsTr("&Blue"), value: "#4a90d9" },
                    { name: qsTr("&Purple"), value: "#9068c8" },
                    { name: qsTr("Pin&k"), value: "#d06ca8" }]
                MenuItem {
                    id: colorItem
                    required property var modelData
                    text: MenuText.label(colorItem.modelData.name)
                    // A leading swatch of the color the item applies.
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        width: 14; height: 14; radius: 3
                        color: colorItem.modelData.value
                        border.color: Theme.border
                    }
                    onTriggered: textContextMenu.target.applyColor(colorItem.modelData.value)
                }
            }
            MenuSeparator {}
            MenuItem {
                text: MenuText.label(qsTr("&Custom…"))
                onTriggered: {
                    textColorDialog.target = textContextMenu.target
                    textColorDialog.open()
                }
            }
            MenuItem {
                text: MenuText.label(qsTr("Re&move color"))
                enabled: textContextMenu.target
                         && textContextMenu.target.currentColor !== ""
                onTriggered: textContextMenu.target.removeColor()
            }
        }
        MenuItem {
            text: MenuText.label(qsTr("Lin&k…"))
            enabled: textContextMenu.target
                     && !textContextMenu.target.verbatimEditing
            onTriggered: textContextMenu.target.openLinkDialog()
        }
        MenuSeparator {}
        MenuItem {
            text: MenuText.label(qsTr("&Select all"))
            onTriggered: textContextMenu.target.selectAllText()
        }
    }

    // The custom-color picker for the text context menu.
    // The target block is captured when the menu opens, since the dialog is
    // asynchronous.
    ColorDialog {
        id: textColorDialog
        property var target: null
        onAccepted: {
            if (!target) return
            var s = selectedColor.toString()
            if (s.length === 9)
                s = "#" + s.substr(3)
            target.applyColor(s)
        }
    }

    Menu {
        id: linkContextMenu
        objectName: "linkContextMenu"
        property var target: null

        MenuItem {
            objectName: "ctxOpenLink"
            text: MenuText.label(qsTr("&Open link"))
            onTriggered: linkContextMenu.target.openLinkUnderCursor()
        }
        MenuItem {
            objectName: "ctxEditLink"
            text: MenuText.label(qsTr("&Edit link…"))
            onTriggered: linkContextMenu.target.openLinkDialog()
        }
        MenuItem {
            objectName: "ctxRemoveLink"
            text: MenuText.label(qsTr("&Remove link"))
            onTriggered: linkContextMenu.target.removeLinkAtCursor()
        }
    }

    Menu {
        id: blockContextMenu
        objectName: "blockContextMenu"
        property var target: null

        MenuItem {
            objectName: "ctxBlockCopy"
            text: MenuText.label(qsTr("&Copy"))
            onTriggered: menus.copyIndexes([blockContextMenu.target.index])
        }
        Menu {
            objectName: "ctxBlockCopyAs"
            title: MenuText.label(qsTr("Copy &as…"))
            MenuItem {
                objectName: "ctxBlockCopyAsMarkdown"
                text: MenuText.label(qsTr("&Markdown"))
                onTriggered: menus.copyIndexesAs(
                    [blockContextMenu.target.index], "markdown")
            }
            MenuItem {
                objectName: "ctxBlockCopyAsText"
                text: MenuText.label(qsTr("&Plain text"))
                onTriggered: menus.copyIndexesAs(
                    [blockContextMenu.target.index], "text")
            }
            MenuItem {
                objectName: "ctxBlockCopyAsHtml"
                text: MenuText.label(qsTr("&HTML"))
                onTriggered: menus.copyIndexesAs(
                    [blockContextMenu.target.index], "html")
            }
        }
        MenuItem {
            objectName: "ctxBlockExport"
            text: MenuText.label(qsTr("&Export…"))
            onTriggered: menus.exportIndexes([blockContextMenu.target.index])
        }
        MenuSeparator {}
        Menu {
            title: MenuText.label(qsTr("&Turn into"))
            Repeater {
                model: menus.toolbar.typeNames
                MenuItem {
                    required property int index
                    required property string modelData
                    text: MenuText.plain(modelData)
                    onTriggered: blockContextMenu.target.convertBlockType(
                        menus.toolbar.typeValues[index])
                }
            }
        }
        // Alignment (§9.2): paragraphs, headings, and images.
        Menu {
            objectName: "ctxAlignMenu"
            title: MenuText.label(qsTr("Ali&gn"))
            enabled: blockContextMenu.target
                && blockContextMenu.target.setBlockAlignment !== undefined
                && blockContextMenu.target.isAlignable === true
            MenuItem {
                text: MenuText.label(qsTr("&Left"))
                onTriggered: blockContextMenu.target.setBlockAlignment("left")
            }
            MenuItem {
                text: MenuText.label(qsTr("&Center"))
                onTriggered: blockContextMenu.target.setBlockAlignment("center")
            }
            MenuItem {
                text: MenuText.label(qsTr("&Right"))
                onTriggered: blockContextMenu.target.setBlockAlignment("right")
            }
        }
        // Drop cap (§1.2.16): a paragraph-only enlarged initial.
        Menu {
            objectName: "ctxDropCapMenu"
            title: MenuText.label(qsTr("Dro&p cap"))
            enabled: blockContextMenu.target
                && blockContextMenu.target.setDropCap !== undefined
                && blockContextMenu.target.blockType === 0   // Paragraph
            MenuItem {
                text: MenuText.label(qsTr("&None"))
                onTriggered: blockContextMenu.target.setDropCap(0)
            }
            MenuItem {
                text: MenuText.label(qsTr("&2 lines"))
                onTriggered: blockContextMenu.target.setDropCap(2)
            }
            MenuItem {
                text: MenuText.label(qsTr("&3 lines"))
                onTriggered: blockContextMenu.target.setDropCap(3)
            }
            MenuItem {
                text: MenuText.label(qsTr("&5 lines"))
                onTriggered: blockContextMenu.target.setDropCap(5)
            }
        }
        // Fold this block's line breaks into spaces. The model owns which
        // blocks that applies to, so the entry greys itself out on a block
        // with no break and on the types whose newlines are content.
        MenuItem {
            objectName: "ctxRemoveLineBreaks"
            text: MenuText.label(qsTr("Remove &line breaks"))
            enabled: blockContextMenu.target
                && BlockModel.canJoinLines([blockContextMenu.target.index])
            onTriggered: BlockModel.joinLinesForBlocks(
                [blockContextMenu.target.index])
        }
        MenuSeparator {}
        MenuItem {
            objectName: "ctxBlockDuplicate"
            text: MenuText.label(qsTr("D&uplicate"))
            onTriggered: BlockModel.duplicateBlocks(
                [blockContextMenu.target.index])
        }
        MenuItem {
            objectName: "ctxBlockDelete"
            text: MenuText.label(qsTr("&Delete"))
            onTriggered: BlockModel.removeBlocks(
                [blockContextMenu.target.index])
        }
        MenuSeparator {}
        MenuItem {
            text: MenuText.label(qsTr("&Move up"))
            enabled: blockContextMenu.target
                     && blockContextMenu.target.index > 0
            onTriggered: BlockModel.moveBlocksBy(
                [blockContextMenu.target.index], -1)
        }
        MenuItem {
            text: MenuText.label(qsTr("Move dow&n"))
            enabled: blockContextMenu.target
                     && blockContextMenu.target.index < BlockModel.count - 1
            onTriggered: BlockModel.moveBlocksBy(
                [blockContextMenu.target.index], 1)
        }
        MenuItem {
            text: MenuText.label(qsTr("&Indent"))
            onTriggered: BlockModel.changeIndentForBlocks(
                [blockContextMenu.target.index], 1)
        }
        MenuItem {
            text: MenuText.label(qsTr("&Outdent"))
            onTriggered: BlockModel.changeIndentForBlocks(
                [blockContextMenu.target.index], -1)
        }
    }

    Menu {
        id: selectionContextMenu
        objectName: "selectionContextMenu"
        property var target: null

        MenuItem {
            objectName: "ctxSelCopy"
            text: MenuText.label(qsTr("&Copy"))
            onTriggered: menus.selectionKeys.copyBlocksToClipboard()
        }
        Menu {
            objectName: "ctxSelCopyAs"
            title: MenuText.label(qsTr("Copy &as…"))
            MenuItem {
                objectName: "ctxSelCopyAsMarkdown"
                text: MenuText.label(qsTr("&Markdown"))
                onTriggered: menus.copyIndexesAs(
                    DocumentSelection.selectedIndexes(), "markdown")
            }
            MenuItem {
                objectName: "ctxSelCopyAsText"
                text: MenuText.label(qsTr("&Plain text"))
                onTriggered: menus.copyIndexesAs(
                    DocumentSelection.selectedIndexes(), "text")
            }
            MenuItem {
                objectName: "ctxSelCopyAsHtml"
                text: MenuText.label(qsTr("&HTML"))
                onTriggered: menus.copyIndexesAs(
                    DocumentSelection.selectedIndexes(), "html")
            }
        }
        MenuItem {
            objectName: "ctxSelExport"
            text: MenuText.label(qsTr("&Export…"))
            onTriggered: menus.exportIndexes(
                DocumentSelection.selectedIndexes())
        }
        MenuSeparator {}
        MenuItem {
            text: MenuText.label(qsTr("Cu&t"))
            onTriggered: {
                menus.selectionKeys.copyBlocksToClipboard()
                menus.selectionKeys.removeSelectedBlocks()
            }
        }
        MenuItem {
            objectName: "ctxSelDuplicate"
            text: MenuText.label(qsTr("D&uplicate"))
            onTriggered: {
                var clones = BlockModel.duplicateBlocks(
                    DocumentSelection.selectedIndexes())
                if (clones.length > 0)
                    menus.selectionKeys.selectRange(
                        Number(clones[0]),
                        Number(clones[clones.length - 1]))
            }
        }
        MenuItem {
            objectName: "ctxSelDelete"
            text: MenuText.label(qsTr("&Delete"))
            onTriggered: menus.selectionKeys.removeSelectedBlocks()
        }
        // The same fold across the whole selection, as one undo step: what
        // unwraps a hard-wrapped note in one go. The revision read
        // re-evaluates the enabled state as the selection changes.
        MenuItem {
            objectName: "ctxSelRemoveLineBreaks"
            text: MenuText.label(qsTr("Remove &line breaks"))
            enabled: {
                var revision = DocumentSelection.revision  // dependency only
                return BlockModel.canJoinLines(
                    DocumentSelection.selectedIndexes())
            }
            onTriggered: BlockModel.joinLinesForBlocks(
                DocumentSelection.selectedIndexes())
        }
        MenuSeparator {}
        MenuItem {
            text: MenuText.label(qsTr("&Move up"))
            onTriggered: {
                BlockModel.moveBlocksBy(
                    DocumentSelection.selectedIndexes(), -1)
                menus.selectionKeys.revealSelectionEdge()
            }
        }
        MenuItem {
            text: MenuText.label(qsTr("Move dow&n"))
            onTriggered: {
                BlockModel.moveBlocksBy(
                    DocumentSelection.selectedIndexes(), 1)
                menus.selectionKeys.revealSelectionEdge()
            }
        }
        MenuItem {
            text: MenuText.label(qsTr("&Indent"))
            onTriggered: BlockModel.changeIndentForBlocks(
                DocumentSelection.selectedIndexes(), 1)
        }
        MenuItem {
            text: MenuText.label(qsTr("&Outdent"))
            onTriggered: BlockModel.changeIndentForBlocks(
                DocumentSelection.selectedIndexes(), -1)
        }
    }
}
