// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The BarButton component's background is a separate scope, and several
// bindings read ids declared outside it. Binding resolves both.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// The toolbar (features.md §9.2): a block type dropdown, formatting
// toggles reflecting the caret's span membership, an insert menu over
// the block-menu catalog, and a view menu. Right-click offers the §9.2
// show/hide customization, persisted per group.
//
// Every control takes focus by keyboard only (Qt.TabFocus): clicking one
// still never blurs the block being edited, while Tab and the F6 region
// cycle can reach Insert, Templates and View, whose actions have no other
// shortcut. A control holding keyboard focus draws a focus ring, since a
// caret that cannot be seen is the same as no caret at all.
Rectangle {
    id: toolbar
    objectName: "toolbar"

    property var appWindow
    property var listView

    height: visible ? 36 : 0
    color: Theme.footerBackground

    Rectangle { // bottom edge
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    // ---- §9.2 customization: group visibility, persisted ----------
    property bool showBlockGroup: true
    property bool showFormatGroup: true
    property bool showInsertGroup: true
    property bool showViewGroup: true

    function applyPersistedCustomization() {
        showBlockGroup = AppSettings.value("toolbar.showBlockType", true)
        showFormatGroup = AppSettings.value("toolbar.showFormatting", true)
        showInsertGroup = AppSettings.value("toolbar.showInsert", true)
        showViewGroup = AppSettings.value("toolbar.showView", true)
    }
    Component.onCompleted: applyPersistedCustomization()

    function setGroupVisible(key, prop, value) {
        toolbar[prop] = value
        AppSettings.setValue(key, value)
    }

    // Pane focus entry (§14.1 tab order), reached from F6 like the sidebar and
    // note list. Insert, Templates and View come first because they are the
    // actions with no separate shortcut; Tab walks on to the rest.
    function focusPane() {
        var candidates = [insertButton, templatesButton, viewButton,
                          blockTypeCombo, backButton, forwardButton]
        for (var i = 0; i < candidates.length; ++i) {
            if (candidates[i].visible && candidates[i].enabled) {
                candidates[i].forceActiveFocus(Qt.TabFocusReason)
                return true
            }
        }
        return false
    }

    // ---- The caret's block and formatting state --------------------
    readonly property var targetBlock: {
        var focusDep = toolbar.appWindow ? toolbar.appWindow.activeFocusItem : null
        var indexDep = toolbar.appWindow ? toolbar.appWindow.lastFocusedBlock : 0
        if (!toolbar.appWindow || !listView)
            return null
        var item = listView.itemAtIndex(toolbar.appWindow.lastFocusedBlock)
        return (item && item.isFocused) ? item : null
    }
    readonly property int caretFlags:
        targetBlock && targetBlock.cursorFormatFlags !== undefined
            ? targetBlock.cursorFormatFlags : 0
    // Formatting applies to a focused, non-verbatim block; over a
    // cross-block text selection the commands are deliberately inert, so
    // the buttons disable rather than half-work.
    readonly property bool canFormat:
        targetBlock !== null
        && targetBlock.toggleSpanType !== undefined
        && !targetBlock.verbatimEditing
        && !DocumentSelection.hasTextSelection
    readonly property bool canConvert:
        targetBlock !== null && targetBlock.convertBlockType !== undefined

    // Alignment (§9.2) applies to paragraphs, headings, and images. The block
    // exposes setBlockAlignment; the current value comes from blockAlign
    // (text, default left) or imageAlign (image, default center).
    readonly property var alignableTypes: [0, 1, 2, 3, 10, 11]  // para, H1-4, image
    readonly property bool canAlign:
        targetBlock !== null
        && targetBlock.setBlockAlignment !== undefined
        && toolbar.alignableTypes.indexOf(targetBlock.blockType) >= 0
    readonly property string currentAlign: {
        if (!canAlign) return "left"
        if (targetBlock.blockAlign !== undefined) return targetBlock.blockAlign
        if (targetBlock.imageAlign !== undefined) return targetBlock.imageAlign
        return "left"
    }

    // Display-order type list for the dropdown (enum values are
    // persisted storage; H4 was appended after Divider).
    readonly property var typeNames: [
        qsTr("Text"), qsTr("Heading 1"), qsTr("Heading 2"),
        qsTr("Heading 3"), qsTr("Heading 4"), qsTr("Bulleted List"),
        qsTr("Numbered List"), qsTr("To-do"), qsTr("Quote"),
        qsTr("Code Block"), qsTr("Callout"), qsTr("Divider")]
    readonly property var typeValues: [0, 1, 2, 3, 10, 4, 5, 6, 7, 8, 12, 9]

    // The background every toolbar control shares, so the keyboard focus ring
    // is drawn the same way on all of them. The control is passed in rather
    // than reached through `parent`, which is untyped in a background scope.
    component BarBackground: Rectangle {
        property var control: null
        radius: 4
        color: control && control.checked ? Theme.selectionTint
             : control && control.hovered && control.enabled ? Theme.hoverTint
             : "transparent"
        // These controls decline mouse focus, so active focus here can only
        // have come from the keyboard — which is exactly when the ring is
        // wanted.
        border.width: control && control.activeFocus ? 2 : 0
        border.color: Theme.focusRing
    }

    component BarButton: ToolButton {
        // Named so the background, its own scope, reads the button's state
        // rather than reaching it through an untyped `parent`.
        id: barButton
        property int flagBit: 0
        focusPolicy: Qt.TabFocus
        implicitWidth: 30
        implicitHeight: 28
        font.pixelSize: 13
        enabled: toolbar.canFormat
        checked: barButton.flagBit !== 0
                 && (toolbar.caretFlags & barButton.flagBit) !== 0
        // Screen-reader name/role (§14.2): glyph buttons carry their tooltip
        // (e.g. "Bold (Ctrl+B)") as their accessible name.
        Accessible.role: Accessible.Button
        Accessible.name: barButton.ToolTip.text !== "" ? barButton.ToolTip.text : barButton.text
        Accessible.checkable: barButton.flagBit !== 0
        Accessible.checked: barButton.checked
        background: BarBackground { control: barButton }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.bottomMargin: 1
        spacing: 2

        // Back/forward over the note history; collection mode only, like
        // the shortcuts they mirror.
        ToolButton {
            id: backButton
            objectName: "toolbarBackButton"
            visible: toolbar.appWindow ? toolbar.appWindow.collectionOpen : false
            focusPolicy: Qt.TabFocus
            implicitWidth: 30
            implicitHeight: 28
            font.pixelSize: 14
            flat: true
            text: "←"
            enabled: NavigationHistory.canGoBack
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Back (Alt+Left)")
            onClicked: if (toolbar.appWindow) toolbar.appWindow.navigateBack()
        }
        ToolButton {
            id: forwardButton
            objectName: "toolbarForwardButton"
            visible: toolbar.appWindow ? toolbar.appWindow.collectionOpen : false
            focusPolicy: Qt.TabFocus
            implicitWidth: 30
            implicitHeight: 28
            font.pixelSize: 14
            flat: true
            text: "→"
            enabled: NavigationHistory.canGoForward
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Forward (Alt+Right)")
            onClicked: if (toolbar.appWindow) toolbar.appWindow.navigateForward()
        }

        ComboBox {
            id: blockTypeCombo
            objectName: "toolbarBlockTypeCombo"
            visible: toolbar.showBlockGroup
            focusPolicy: Qt.TabFocus
            implicitWidth: 130
            implicitHeight: 28
            font.pixelSize: 12
            flat: true
            enabled: toolbar.canConvert
            model: toolbar.typeNames
            currentIndex: toolbar.targetBlock
                ? toolbar.typeValues.indexOf(toolbar.targetBlock.blockType)
                : -1
            displayText: currentIndex >= 0 ? toolbar.typeNames[currentIndex]
                                           : qsTr("Block type")
            onActivated: function(index) {
                if (toolbar.targetBlock)
                    toolbar.targetBlock.convertBlockType(
                        toolbar.typeValues[index])
            }
        }

        ToolSeparator {
            visible: toolbar.showBlockGroup && toolbar.showFormatGroup
            implicitHeight: 24
        }

        RowLayout {
            visible: toolbar.showFormatGroup
            spacing: 1

            BarButton {
                objectName: "toolbarBoldButton"
                text: "B"; font.bold: true; flagBit: 0x2
                ToolTip.visible: hovered; ToolTip.text: qsTr("Bold (Ctrl+B)")
                onClicked: toolbar.targetBlock.toggleSpanType("bold")
            }
            BarButton {
                objectName: "toolbarItalicButton"
                text: "I"; font.italic: true; flagBit: 0x4
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Italic (Ctrl+I)")
                onClicked: toolbar.targetBlock.toggleSpanType("italic")
            }
            BarButton {
                objectName: "toolbarUnderlineButton"
                text: "U"; font.underline: true; flagBit: 0x10
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Underline (Ctrl+U)")
                onClicked: toolbar.targetBlock.toggleSpanType("underline")
            }
            BarButton {
                objectName: "toolbarStrikeButton"
                text: "S"; font.strikeout: true; flagBit: 0x8
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Strikethrough (Ctrl+Shift+S)")
                onClicked: toolbar.targetBlock.toggleSpanType("strike")
            }
            BarButton {
                objectName: "toolbarCodeButton"
                text: "<>"; flagBit: 0x20; font.pixelSize: 11
                implicitWidth: 34
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Inline code (Ctrl+E)")
                onClicked: toolbar.targetBlock.toggleSpanType("code")
            }
            BarButton {
                id: highlightButton
                objectName: "toolbarHighlightButton"
                text: "H"; flagBit: 0x40
                // Overrides the shared background to tint with the highlight
                // colour, so it repeats the pattern and needs its own id.
                background: Rectangle {
                    radius: 4
                    color: highlightButton.checked ? Theme.highlightBackground
                         : highlightButton.hovered && highlightButton.enabled
                           ? Theme.hoverTint
                         : "transparent"
                }
                ToolTip.visible: highlightButton.hovered
                ToolTip.text: qsTr("Highlight")
                onClicked: toolbar.targetBlock.toggleSpanType("highlight")
            }
            BarButton {
                objectName: "toolbarSuperscriptButton"
                text: "x²"; flagBit: 0x100; font.pixelSize: 11
                ToolTip.visible: hovered; ToolTip.text: qsTr("Superscript")
                onClicked: toolbar.targetBlock.toggleSpanType("superscript")
            }
            BarButton {
                objectName: "toolbarSubscriptButton"
                text: "x₂"; flagBit: 0x200; font.pixelSize: 11
                ToolTip.visible: hovered; ToolTip.text: qsTr("Subscript")
                onClicked: toolbar.targetBlock.toggleSpanType("subscript")
            }
            BarButton {
                objectName: "toolbarLinkButton"
                // A text label: the chain emoji has no glyph in the
                // default Linux UI fonts and rendered as tofu.
                text: qsTr("Link"); flagBit: 0x80; font.pixelSize: 11
                implicitWidth: 40; font.underline: true
                enabled: toolbar.canFormat
                         && toolbar.targetBlock.openLinkDialog !== undefined
                ToolTip.visible: hovered; ToolTip.text: qsTr("Link (Ctrl+K)")
                onClicked: toolbar.targetBlock.openLinkDialog()
            }
            // Text color: "A" with an underline in the
            // caret's current color; opens a swatch/custom/remove picker.
            BarButton {
                objectName: "toolbarColorButton"
                text: "A"; flagBit: 0x400
                ToolTip.visible: hovered; ToolTip.text: qsTr("Text color")
                onClicked: toolbarColorPicker.open()
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 16; height: 3; radius: 1
                    color: (toolbar.targetBlock && toolbar.targetBlock.currentColor)
                        ? toolbar.targetBlock.currentColor : Theme.textPrimary
                }
                ColorPicker {
                    id: toolbarColorPicker
                    y: parent.height
                    currentColor: (toolbar.targetBlock
                        && toolbar.targetBlock.currentColor !== undefined)
                        ? toolbar.targetBlock.currentColor : ""
                    onColorPicked: function(v) {
                        if (toolbar.targetBlock) toolbar.targetBlock.applyColor(v)
                    }
                    onRemoveRequested: {
                        if (toolbar.targetBlock) toolbar.targetBlock.removeColor()
                    }
                }
            }

            ToolSeparator { implicitHeight: 20 }

            // Alignment group (§9.2): left / center / right for paragraphs,
            // headings, and images. Disabled for any other block type.
            component AlignButton: ToolButton {
                // Named for the same reason BarButton is: its background and
                // accessibility bindings are separate scopes.
                id: alignButton
                property string alignValue: "left"
                focusPolicy: Qt.TabFocus
                implicitWidth: 28
                implicitHeight: 28
                font.pixelSize: 13
                enabled: toolbar.canAlign
                checked: toolbar.canAlign && toolbar.currentAlign === alignValue
                Accessible.role: Accessible.Button
                Accessible.name: alignButton.ToolTip.text !== ""
                                 ? alignButton.ToolTip.text : alignButton.text
                Accessible.checkable: true
                Accessible.checked: alignButton.checked
                onClicked: if (toolbar.targetBlock)
                               toolbar.targetBlock.setBlockAlignment(
                                   alignButton.alignValue)
                background: BarBackground { control: alignButton }
            }
            AlignButton {
                objectName: "toolbarAlignLeft"
                alignValue: "left"; text: "⇤"
                ToolTip.visible: hovered; ToolTip.text: qsTr("Align left")
            }
            AlignButton {
                objectName: "toolbarAlignCenter"
                alignValue: "center"; text: "⇔"
                ToolTip.visible: hovered; ToolTip.text: qsTr("Align center")
            }
            AlignButton {
                objectName: "toolbarAlignRight"
                alignValue: "right"; text: "⇥"
                ToolTip.visible: hovered; ToolTip.text: qsTr("Align right")
            }
        }

        ToolSeparator {
            visible: toolbar.showFormatGroup && toolbar.showInsertGroup
            implicitHeight: 24
        }

        ToolButton {
            id: insertButton
            objectName: "toolbarInsertButton"
            visible: toolbar.showInsertGroup
            focusPolicy: Qt.TabFocus
            text: qsTr("+ Insert")
            font.pixelSize: 12
            implicitHeight: 28
            Accessible.role: Accessible.ButtonMenu
            Accessible.name: qsTr("Insert block")
            background: BarBackground { control: insertButton }
            onClicked: insertMenu.popup(this, 0, height)

            Menu {
                id: insertMenu
                objectName: "toolbarInsertMenu"

                Repeater {
                    model: toolbar.typeNames
                    MenuItem {
                        required property int index
                        required property string modelData
                        text: modelData
                        onTriggered: toolbar.insertBlockOfType(
                            toolbar.typeValues[index])
                    }
                }
                // Wave-2 types that insert rather than convert (features.md
                // §4.2 parity). Each routes to the same flow the slash menu
                // uses.
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Table")
                    onTriggered: toolbar.insertSpecialBelow("table")
                }
                MenuItem {
                    text: qsTr("Task Board")
                    onTriggered: toolbar.insertSpecialBelow("kanban")
                }
                MenuItem {
                    text: qsTr("Math Block")
                    onTriggered: toolbar.insertBlockOfType(13)   // Block.MathBlock
                }
                MenuItem {
                    text: qsTr("Image")
                    onTriggered: toolbar.insertSpecialBelow("image")
                }
                MenuItem {
                    text: qsTr("Audio / Video")
                    onTriggered: toolbar.insertSpecialBelow("media")
                }
            }
        }

        Item { Layout.fillWidth: true }

        // features.md §18 templates: new-from-template and management.
        // Only meaningful with a collection open (templates live under .kvit).
        ToolButton {
            id: templatesButton
            objectName: "toolbarTemplatesButton"
            visible: toolbar.appWindow && toolbar.appWindow.collectionOpen
            focusPolicy: Qt.TabFocus
            text: qsTr("Templates")
            font.pixelSize: 12
            implicitHeight: 28
            Accessible.role: Accessible.ButtonMenu
            Accessible.name: qsTr("Templates")
            background: BarBackground { control: templatesButton }
            onClicked: {
                NoteTemplates.seedBuiltinsIfEmpty()
                templatesMenu.popup(this, 0, height)
            }
            Menu {
                id: templatesMenu
                objectName: "toolbarTemplatesMenu"
                Menu {
                    id: newFromTemplateMenu
                    objectName: "newFromTemplateMenu"
                    title: qsTr("New from template")
                    Repeater {
                        model: {
                            var r = NoteTemplates.revision  // dependency
                            return NoteTemplates.templateNames()
                        }
                        MenuItem {
                            required property string modelData
                            text: modelData
                            onTriggered:
                                toolbar.appWindow.createFromTemplate(modelData)
                        }
                    }
                }
                MenuSeparator {}
                MenuItem {
                    objectName: "manageTemplatesItem"
                    text: qsTr("Manage templates…")
                    onTriggered: toolbar.appWindow.templateDialog.openManage()
                }
            }
        }

        ToolButton {
            id: viewButton
            objectName: "toolbarViewButton"
            visible: toolbar.showViewGroup
            focusPolicy: Qt.TabFocus
            text: qsTr("View")
            font.pixelSize: 12
            implicitHeight: 28
            Accessible.role: Accessible.ButtonMenu
            Accessible.name: qsTr("View")
            background: BarBackground { control: viewButton }
            onClicked: viewMenu.popup(this, 0, height)

            Menu {
                id: viewMenu
                objectName: "toolbarViewMenu"

                MenuItem {
                    objectName: "viewMenuSidebar"
                    text: qsTr("Sidebar")
                    checkable: true
                    enabled: toolbar.appWindow.collectionOpen
                    checked: !toolbar.appWindow.sidebarCollapsed
                    onTriggered: toolbar.appWindow.sidebarCollapsed
                        = !toolbar.appWindow.sidebarCollapsed
                }
                MenuItem {
                    objectName: "viewMenuNoteList"
                    text: qsTr("Note list")
                    checkable: true
                    enabled: toolbar.appWindow.collectionOpen
                    checked: !toolbar.appWindow.noteListCollapsed
                    onTriggered: toolbar.appWindow.noteListCollapsed
                        = !toolbar.appWindow.noteListCollapsed
                }
                MenuItem {
                    objectName: "viewMenuOutline"
                    text: qsTr("Outline")
                    checkable: true
                    checked: toolbar.appWindow.outlineVisible
                    onTriggered: toolbar.appWindow.outlineVisible
                        = !toolbar.appWindow.outlineVisible
                }
                MenuItem {
                    objectName: "viewMenuBacklinks"
                    text: qsTr("Backlinks")
                    checkable: true
                    enabled: toolbar.appWindow.collectionOpen
                    checked: toolbar.appWindow.backlinksVisible
                    onTriggered: toolbar.appWindow.backlinksVisible
                        = !toolbar.appWindow.backlinksVisible
                }
                MenuSeparator {}
                MenuItem {
                    objectName: "viewMenuFocusMode"
                    text: qsTr("Focus mode")
                    checkable: true
                    checked: toolbar.appWindow.focusMode
                    onTriggered: toolbar.appWindow.focusMode
                        = !toolbar.appWindow.focusMode
                }
                MenuItem {
                    objectName: "viewMenuTypewriterMode"
                    text: qsTr("Typewriter mode")
                    checkable: true
                    checked: toolbar.appWindow.typewriterMode
                    onTriggered: toolbar.appWindow.typewriterMode
                        = !toolbar.appWindow.typewriterMode
                }
                MenuSeparator {}
                MenuItem {
                    objectName: "viewMenuStatusBar"
                    text: qsTr("Status bar")
                    checkable: true
                    checked: toolbar.appWindow.statusBarVisible
                    onTriggered: toolbar.appWindow.statusBarVisible
                        = !toolbar.appWindow.statusBarVisible
                }
                MenuItem {
                    objectName: "viewMenuCodeLineNumbers"
                    text: qsTr("Code line numbers")
                    checkable: true
                    // The revision read re-evaluates this when the setting flips
                    // from anywhere; the gutter binding in EditableBlock reads
                    // the same key.
                    checked: {
                        var r = AppSettings.revision  // dependency only
                        return AppSettings.value("view.codeLineNumbers", false) === true
                    }
                    onTriggered: AppSettings.setValue("view.codeLineNumbers",
                                                      !checked)
                }
                MenuItem {
                    objectName: "viewMenuEquationNumbers"
                    text: qsTr("Equation numbers")
                    checkable: true
                    // Same reactive pattern as code line numbers; MathBlock
                    // reads the same key.
                    checked: {
                        var r = AppSettings.revision  // dependency only
                        return AppSettings.value("view.equationNumbers", false) === true
                    }
                    onTriggered: AppSettings.setValue("view.equationNumbers",
                                                      !checked)
                }
                MenuSeparator {}
                Menu {
                    id: themeMenu
                    objectName: "viewMenuTheme"
                    title: qsTr("Theme")
                    Repeater {
                        model: Theme.availableThemes
                        MenuItem {
                            required property string modelData
                            text: Theme.displayName(modelData)
                            checkable: true
                            checked: Theme.themeId === modelData
                            onTriggered: Theme.themeId = modelData
                        }
                    }
                }
                MenuSeparator {}
                MenuItem {
                    objectName: "viewMenuExport"
                    text: qsTr("Export…")
                    onTriggered: toolbar.appWindow.exportDialog.openDialog()
                }
                MenuItem {
                    objectName: "viewMenuImport"
                    text: qsTr("Import…")
                    visible: toolbar.appWindow && toolbar.appWindow.collectionOpen
                    onTriggered: toolbar.appWindow.importDialog.openDialog()
                }
                MenuSeparator {}
                MenuItem {
                    objectName: "viewMenuSettings"
                    text: qsTr("Settings…")
                    onTriggered: toolbar.appWindow.openSettingsDialog()
                }
                MenuItem {
                    objectName: "viewMenuShortcuts"
                    text: qsTr("Keyboard shortcuts…")
                    onTriggered: toolbar.appWindow.openShortcutReference()
                }
                MenuItem {
                    objectName: "viewMenuFocusEditor"
                    text: qsTr("Focus editor")
                    onTriggered: toolbar.appWindow.focusEditor()
                }
                MenuItem {
                    objectName: "viewMenuReducedMotion"
                    text: qsTr("Reduced motion")
                    checkable: true
                    checked: Theme.reducedMotion
                    onTriggered: Theme.reducedMotion = checked
                }
                MenuSeparator {}
                MenuItem {
                    objectName: "viewMenuQuickCapture"
                    text: qsTr("Quick capture… (Ctrl+Alt+N)")
                    enabled: toolbar.appWindow && toolbar.appWindow.collectionOpen
                    onTriggered: toolbar.appWindow.openQuickCapture()
                }
            }
        }
    }

    // Insert below the caret's block (or at the end), focusing the new
    // block — the plus-button contract without the menu step.
    function insertBlockOfType(type) {
        var idx = toolbar.appWindow ? toolbar.appWindow.lastFocusedBlock : -1
        if (idx < 0 || idx >= BlockModel.count)
            idx = BlockModel.count - 1
        BlockModel.insertBlock(idx + 1, type, "")
        // Inserting below the caret can put the new block past the viewport,
        // where its delegate does not exist yet; the window's focus router
        // brings it into view and retries until it does.
        if (toolbar.appWindow)
            toolbar.appWindow.focusBlockAtIndex(idx + 1)
    }

    // Insert a wave-2 type that needs a flow rather than a bare convert
    // (§4.2): image/media open the file dialog, table the grid picker, a task
    // board seeds its columns. A new empty block is created below the caret's
    // block and handed to that flow.
    function insertSpecialBelow(kind) {
        var idx = toolbar.appWindow ? toolbar.appWindow.lastFocusedBlock : -1
        if (idx < 0 || idx >= BlockModel.count)
            idx = BlockModel.count - 1
        var newIdx = idx + 1
        BlockModel.insertBlock(newIdx, 0, "")
        if ((kind === "image" || kind === "media") && toolbar.appWindow.insertImageIntoBlock)
            toolbar.appWindow.insertImageIntoBlock(newIdx)
        else if (kind === "table" && toolbar.appWindow.insertTableIntoBlock)
            toolbar.appWindow.insertTableIntoBlock(newIdx)
        else if (kind === "kanban")
            BlockModel.convertBlock(newIdx, 8,   // Block.CodeBlock, kanban fence
                "## To do\n## In progress\n## Done", false, "kanban")
    }

    // §9.2 "toolbar customization (show/hide buttons)": right-click.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        z: -1
        onPressed: customizeMenu.popup()
    }
    Menu {
        id: customizeMenu
        objectName: "toolbarCustomizeMenu"
        MenuItem {
            text: qsTr("Block type")
            checkable: true
            checked: toolbar.showBlockGroup
            onTriggered: toolbar.setGroupVisible(
                "toolbar.showBlockType", "showBlockGroup", checked)
        }
        MenuItem {
            text: qsTr("Formatting")
            checkable: true
            checked: toolbar.showFormatGroup
            onTriggered: toolbar.setGroupVisible(
                "toolbar.showFormatting", "showFormatGroup", checked)
        }
        MenuItem {
            text: qsTr("Insert")
            checkable: true
            checked: toolbar.showInsertGroup
            onTriggered: toolbar.setGroupVisible(
                "toolbar.showInsert", "showInsertGroup", checked)
        }
        MenuItem {
            text: qsTr("View")
            checkable: true
            checked: toolbar.showViewGroup
            onTriggered: toolbar.setGroupVisible(
                "toolbar.showView", "showViewGroup", checked)
        }
    }
}
