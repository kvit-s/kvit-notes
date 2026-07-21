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
Item {
    id: menus

    // Wired by main.qml: the toolbar for the block-type list the "Turn into"
    // submenu offers, and the block-selection key handler for the operations
    // the selection menu shares with it.
    property var toolbar
    property var selectionKeys

    // Whether an open menu is holding this target's selection.
    function holdsSelection(target) {
        return (textContextMenu.visible && textContextMenu.target === target)
            || (linkContextMenu.visible && linkContextMenu.target === target)
    }

    function openTextMenu(target) {
        if (DocumentSelection.hasBlockSelection
            && DocumentSelection.isBlockSelected(target.index)) {
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

    function openHandleMenu(target) {
        if (DocumentSelection.hasBlockSelection
            && DocumentSelection.isBlockSelected(target.index)) {
            selectionContextMenu.popup()
            return
        }
        blockContextMenu.target = target
        blockContextMenu.popup()
    }

    Menu {
        id: textContextMenu
        objectName: "textContextMenu"
        property var target: null
        readonly property bool hasSel: target
            && target.selectionEndDoc > target.selectionStartDoc

        MenuItem {
            objectName: "ctxCut"
            text: qsTr("Cut")
            enabled: textContextMenu.hasSel
            onTriggered: textContextMenu.target.cutSelection()
        }
        MenuItem {
            objectName: "ctxCopy"
            text: qsTr("Copy")
            enabled: textContextMenu.hasSel
            onTriggered: textContextMenu.target.copySelection()
        }
        MenuItem {
            objectName: "ctxPaste"
            text: qsTr("Paste")
            enabled: Clipboard.hasText
            onTriggered: textContextMenu.target.pasteClipboard(false)
        }
        MenuItem {
            objectName: "ctxPastePlain"
            text: qsTr("Paste as plain text")
            enabled: Clipboard.hasText
            onTriggered: textContextMenu.target.pasteClipboard(true)
        }
        MenuSeparator {}
        Menu {
            title: qsTr("Formatting")
            enabled: textContextMenu.target
                     && !textContextMenu.target.verbatimEditing
            Repeater {
                model: [
                    { name: qsTr("Bold"), type: "bold" },
                    { name: qsTr("Italic"), type: "italic" },
                    { name: qsTr("Underline"), type: "underline" },
                    { name: qsTr("Strikethrough"), type: "strike" },
                    { name: qsTr("Inline code"), type: "code" },
                    { name: qsTr("Highlight"), type: "highlight" },
                    { name: qsTr("Superscript"), type: "superscript" },
                    { name: qsTr("Subscript"), type: "subscript" },
                    { name: qsTr("Inline math"), type: "math" }]
                MenuItem {
                    id: spanTypeItem
                    required property var modelData
                    text: spanTypeItem.modelData.name
                    onTriggered: textContextMenu.target.toggleSpanType(
                        spanTypeItem.modelData.type)
                }
            }
        }
        Menu {
            title: qsTr("Text color")
            enabled: textContextMenu.target
                     && !textContextMenu.target.verbatimEditing
            Repeater {
                model: [
                    { name: qsTr("Red"), value: "#e05c5c" },
                    { name: qsTr("Orange"), value: "#e0a04c" },
                    { name: qsTr("Green"), value: "#58a866" },
                    { name: qsTr("Blue"), value: "#4a90d9" },
                    { name: qsTr("Purple"), value: "#9068c8" },
                    { name: qsTr("Pink"), value: "#d06ca8" }]
                MenuItem {
                    id: colorItem
                    required property var modelData
                    text: colorItem.modelData.name
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
                text: qsTr("Custom…")
                onTriggered: {
                    textColorDialog.target = textContextMenu.target
                    textColorDialog.open()
                }
            }
            MenuItem {
                text: qsTr("Remove color")
                enabled: textContextMenu.target
                         && textContextMenu.target.currentColor !== ""
                onTriggered: textContextMenu.target.removeColor()
            }
        }
        MenuItem {
            text: qsTr("Link…")
            enabled: textContextMenu.target
                     && !textContextMenu.target.verbatimEditing
            onTriggered: textContextMenu.target.openLinkDialog()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Select all")
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
            text: qsTr("Open link")
            onTriggered: linkContextMenu.target.openLinkUnderCursor()
        }
        MenuItem {
            objectName: "ctxEditLink"
            text: qsTr("Edit link…")
            onTriggered: linkContextMenu.target.openLinkDialog()
        }
        MenuItem {
            objectName: "ctxRemoveLink"
            text: qsTr("Remove link")
            onTriggered: linkContextMenu.target.removeLinkAtCursor()
        }
    }

    Menu {
        id: blockContextMenu
        objectName: "blockContextMenu"
        property var target: null

        Menu {
            title: qsTr("Turn into")
            Repeater {
                model: menus.toolbar.typeNames
                MenuItem {
                    required property int index
                    required property string modelData
                    text: modelData
                    onTriggered: blockContextMenu.target.convertBlockType(
                        menus.toolbar.typeValues[index])
                }
            }
        }
        // Alignment (§9.2): paragraphs, headings, and images.
        Menu {
            objectName: "ctxAlignMenu"
            title: qsTr("Align")
            enabled: blockContextMenu.target
                && blockContextMenu.target.setBlockAlignment !== undefined
                && [0, 1, 2, 3, 10, 11].indexOf(blockContextMenu.target.blockType) >= 0
            MenuItem {
                text: qsTr("Left")
                onTriggered: blockContextMenu.target.setBlockAlignment("left")
            }
            MenuItem {
                text: qsTr("Center")
                onTriggered: blockContextMenu.target.setBlockAlignment("center")
            }
            MenuItem {
                text: qsTr("Right")
                onTriggered: blockContextMenu.target.setBlockAlignment("right")
            }
        }
        // Drop cap (§1.2.16): a paragraph-only enlarged initial.
        Menu {
            objectName: "ctxDropCapMenu"
            title: qsTr("Drop cap")
            enabled: blockContextMenu.target
                && blockContextMenu.target.setDropCap !== undefined
                && blockContextMenu.target.blockType === 0   // Paragraph
            MenuItem {
                text: qsTr("None")
                onTriggered: blockContextMenu.target.setDropCap(0)
            }
            MenuItem {
                text: qsTr("2 lines")
                onTriggered: blockContextMenu.target.setDropCap(2)
            }
            MenuItem {
                text: qsTr("3 lines")
                onTriggered: blockContextMenu.target.setDropCap(3)
            }
            MenuItem {
                text: qsTr("5 lines")
                onTriggered: blockContextMenu.target.setDropCap(5)
            }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "ctxBlockDuplicate"
            text: qsTr("Duplicate")
            onTriggered: BlockModel.duplicateBlocks(
                [blockContextMenu.target.index])
        }
        MenuItem {
            objectName: "ctxBlockDelete"
            text: qsTr("Delete")
            onTriggered: BlockModel.removeBlocks(
                [blockContextMenu.target.index])
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Move up")
            enabled: blockContextMenu.target
                     && blockContextMenu.target.index > 0
            onTriggered: BlockModel.moveBlocksBy(
                [blockContextMenu.target.index], -1)
        }
        MenuItem {
            text: qsTr("Move down")
            enabled: blockContextMenu.target
                     && blockContextMenu.target.index < BlockModel.count - 1
            onTriggered: BlockModel.moveBlocksBy(
                [blockContextMenu.target.index], 1)
        }
        MenuItem {
            text: qsTr("Indent")
            onTriggered: BlockModel.changeIndentForBlocks(
                [blockContextMenu.target.index], 1)
        }
        MenuItem {
            text: qsTr("Outdent")
            onTriggered: BlockModel.changeIndentForBlocks(
                [blockContextMenu.target.index], -1)
        }
    }

    Menu {
        id: selectionContextMenu
        objectName: "selectionContextMenu"

        MenuItem {
            objectName: "ctxSelCopy"
            text: qsTr("Copy")
            onTriggered: menus.selectionKeys.copyBlocksToClipboard()
        }
        MenuItem {
            text: qsTr("Cut")
            onTriggered: {
                menus.selectionKeys.copyBlocksToClipboard()
                menus.selectionKeys.removeSelectedBlocks()
            }
        }
        MenuItem {
            objectName: "ctxSelDuplicate"
            text: qsTr("Duplicate")
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
            text: qsTr("Delete")
            onTriggered: menus.selectionKeys.removeSelectedBlocks()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Move up")
            onTriggered: {
                BlockModel.moveBlocksBy(
                    DocumentSelection.selectedIndexes(), -1)
                menus.selectionKeys.revealSelectionEdge()
            }
        }
        MenuItem {
            text: qsTr("Move down")
            onTriggered: {
                BlockModel.moveBlocksBy(
                    DocumentSelection.selectedIndexes(), 1)
                menus.selectionKeys.revealSelectionEdge()
            }
        }
        MenuItem {
            text: qsTr("Indent")
            onTriggered: BlockModel.changeIndentForBlocks(
                DocumentSelection.selectedIndexes(), 1)
        }
        MenuItem {
            text: qsTr("Outdent")
            onTriggered: BlockModel.changeIndentForBlocks(
                DocumentSelection.selectedIndexes(), -1)
        }
    }
}
