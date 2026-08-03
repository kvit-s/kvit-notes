// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The theme Repeater below is its own component scope and reads this file's
// root by id. Binding resolves that.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The View menu: what the window shows — the panels, the editor modes, the
// theme. Anything that acts on documents rather than on the view lives in
// FileMenu.qml beside it.
//
// Like the File menu this has two homes and is therefore one definition: the
// toolbar's View button on Windows and Linux, the macOS system menu bar in
// main.qml. Only one of the two exists in a running window.
Menu {
    id: viewMenu
    objectName: "toolbarViewMenu"
    title: qsTr("View")

    // The editor window these commands act on (main.qml's root).
    property var appWindow

    delegate: DiscoverableMenuItem {}

    DiscoverableMenuItem {
        objectName: "viewMenuSidebar"
        text: qsTr("Sidebar")
        checkable: true
        enabled: viewMenu.appWindow.collectionOpen
        checked: !viewMenu.appWindow.sidebarCollapsed
        onTriggered: viewMenu.appWindow.sidebarCollapsed
            = !viewMenu.appWindow.sidebarCollapsed
    }
    DiscoverableMenuItem {
        objectName: "viewMenuNoteList"
        text: qsTr("Note list")
        checkable: true
        enabled: viewMenu.appWindow.collectionOpen
        checked: !viewMenu.appWindow.noteListCollapsed
        onTriggered: viewMenu.appWindow.noteListCollapsed
            = !viewMenu.appWindow.noteListCollapsed
    }
    DiscoverableMenuItem {
        objectName: "viewMenuOutline"
        text: qsTr("Outline")
        checkable: true
        checked: viewMenu.appWindow.outlineVisible
        onTriggered: viewMenu.appWindow.outlineVisible
            = !viewMenu.appWindow.outlineVisible
    }
    DiscoverableMenuItem {
        objectName: "viewMenuBacklinks"
        text: qsTr("Backlinks")
        checkable: true
        enabled: viewMenu.appWindow.collectionOpen
        checked: viewMenu.appWindow.backlinksVisible
        onTriggered: viewMenu.appWindow.backlinksVisible
            = !viewMenu.appWindow.backlinksVisible
    }
    MenuSeparator {}
    DiscoverableMenuItem {
        objectName: "viewMenuFocusMode"
        text: qsTr("Focus mode")
        checkable: true
        checked: viewMenu.appWindow.focusMode
        onTriggered: viewMenu.appWindow.focusMode
            = !viewMenu.appWindow.focusMode
    }
    DiscoverableMenuItem {
        objectName: "viewMenuTypewriterMode"
        text: qsTr("Typewriter mode")
        checkable: true
        checked: viewMenu.appWindow.typewriterMode
        onTriggered: viewMenu.appWindow.typewriterMode
            = !viewMenu.appWindow.typewriterMode
    }
    MenuSeparator {}
    DiscoverableMenuItem {
        objectName: "viewMenuStatusBar"
        text: qsTr("Status bar")
        checkable: true
        checked: viewMenu.appWindow.statusBarVisible
        onTriggered: viewMenu.appWindow.statusBarVisible
            = !viewMenu.appWindow.statusBarVisible
    }
    DiscoverableMenuItem {
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
    DiscoverableMenuItem {
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
            DiscoverableMenuItem {
                required property string modelData
                text: Theme.displayName(modelData)
                checkable: true
                checked: Theme.themeId === modelData
                onTriggered: Theme.themeId = modelData
            }
        }
    }
    DiscoverableMenuItem {
        objectName: "viewMenuReducedMotion"
        text: qsTr("Reduced motion")
        checkable: true
        checked: Theme.reducedMotion
        onTriggered: Theme.reducedMotion = checked
    }
    MenuSeparator {}
    DiscoverableMenuItem {
        objectName: "viewMenuFocusEditor"
        text: qsTr("Focus editor")
        onTriggered: viewMenu.appWindow.focusEditor()
    }
}
