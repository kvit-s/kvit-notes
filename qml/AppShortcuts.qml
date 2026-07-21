// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// The window-level keyboard map (features.md §13), and the mouse back and
// forward buttons, which issue the same two navigation commands from a
// different device.
//
// What is here is what belongs to the window rather than to one piece of it:
// undo and redo, save, the find bar, note history and the quick switcher,
// the panel and view toggles, focus mode, the shortcut reference, pane
// cycling, and global search. Shortcuts that only make sense inside one
// workflow live with it — Ctrl+O and Ctrl+N with the document-session
// dialogs, Escape with the block drag, quick capture with the system
// integration — so that each of those files can be read on its own.
//
// The undo and redo entries are the fallbacks for when no text area has
// focus; a focused block handles those keys itself.
Item {
    id: shortcuts

    // Wired by main.qml.
    property var appWindow
    property var findBar
    property var quickSwitcher
    // Named apart from its id so the wiring cannot resolve to this property.
    property var sidebarPanel

    Shortcut {
        sequence: "F6"
        onActivated: shortcuts.appWindow.cyclePane()
    }

    // Global keyboard shortcuts for undo/redo
    // Note: These are backup shortcuts when no TextArea has focus.
    // When a TextArea is focused, BlockDelegate handles Ctrl+Z/Y/Shift+Z directly.
    Shortcut {
        sequences: [StandardKey.Undo]  // Ctrl+Z (and platform variants)
        onActivated: {
            if (UndoStack && UndoStack.canUndo) {
                UndoStack.undo()
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Redo]  // Ctrl+Y or Ctrl+Shift+Z depending on platform
        onActivated: {
            if (UndoStack && UndoStack.canRedo) {
                UndoStack.redo()
            }
        }
    }

    // File shortcuts
    Shortcut {
        sequences: [StandardKey.Save]  // Ctrl+S
        onActivated: {
            if (DocumentManager.hasFile) {
                DocumentManager.saveAsync()
            } else {
                DocumentManager.saveFileDialog()
            }
        }
    }

    // Find bar (features.md §7.1).
    // Application context: these work from a focused block, from the
    // bar's own fields, and from block-selection mode alike.
    Shortcut {
        sequences: [StandardKey.Find] // Ctrl+F
        context: Qt.ApplicationShortcut
        onActivated: shortcuts.findBar.open(false)
    }

    Shortcut {
        // Explicit, not StandardKey.Replace: the platform theme maps
        // that to Ctrl+R or nothing on some Linux desktops, and §7.2
        // names Ctrl+H on Windows/Linux and Cmd+Option+F on macOS.
        sequence: Qt.platform.os === "osx" ? "Meta+Alt+F" : "Ctrl+H"
        context: Qt.ApplicationShortcut
        onActivated: shortcuts.findBar.open(true)
    }

    Shortcut {
        sequences: [StandardKey.FindNext] // F3
        context: Qt.ApplicationShortcut
        onActivated: shortcuts.findBar.findNextShortcut()
    }

    Shortcut {
        sequences: [StandardKey.FindPrevious] // Shift+F3
        context: Qt.ApplicationShortcut
        onActivated: shortcuts.findBar.findPreviousShortcut()
    }

    // Wiki-link navigation: history and the quick switcher, collection
    // mode only.
    Shortcut {
        sequence: "Alt+Left"
        context: Qt.ApplicationShortcut
        enabled: shortcuts.appWindow
                 && shortcuts.appWindow.collectionOpen
        onActivated: shortcuts.appWindow.navigateBack()
    }

    Shortcut {
        sequence: "Alt+Right"
        context: Qt.ApplicationShortcut
        enabled: shortcuts.appWindow
                 && shortcuts.appWindow.collectionOpen
        onActivated: shortcuts.appWindow.navigateForward()
    }

    Shortcut {
        sequence: "Ctrl+P"
        context: Qt.ApplicationShortcut
        enabled: shortcuts.appWindow
                 && shortcuts.appWindow.collectionOpen
        onActivated: shortcuts.quickSwitcher.toggle()
    }

    // Mouse back/forward buttons navigate too. The area accepts ONLY
    // those buttons, so ordinary clicks pass straight through to the UI
    // beneath it.
    MouseArea {
        anchors.fill: parent
        z: 10000
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        enabled: shortcuts.appWindow
                 && shortcuts.appWindow.collectionOpen
        onClicked: function(mouse) {
            if (mouse.button === Qt.BackButton)
                shortcuts.appWindow.navigateBack()
            else if (mouse.button === Qt.ForwardButton)
                shortcuts.appWindow.navigateForward()
        }
    }

    // Toggle Sidebar (features.md §13.4): hides both panels for focused
    // writing.
    Shortcut {
        sequence: "Ctrl+\\"
        context: Qt.ApplicationShortcut
        onActivated: shortcuts.appWindow.panelsVisible =
            !shortcuts.appWindow.panelsVisible
    }

    // Settings (the platform convention — the shortcut table in
    // features.md §13 assigns no key).
    Shortcut {
        sequence: "Ctrl+,"
        context: Qt.ApplicationShortcut
        onActivated: shortcuts.appWindow.openSettingsDialog()
    }

    // Toggle the document outline (features.md §17.1).
    Shortcut {
        sequence: "Ctrl+Shift+O"
        context: Qt.ApplicationShortcut
        onActivated: shortcuts.appWindow.outlineVisible =
            !shortcuts.appWindow.outlineVisible
    }

    // Toggle the backlinks pane.
    Shortcut {
        sequence: "Ctrl+Shift+B"
        context: Qt.ApplicationShortcut
        enabled: shortcuts.appWindow
                 && shortcuts.appWindow.collectionOpen
        onActivated: shortcuts.appWindow.backlinksVisible =
            !shortcuts.appWindow.backlinksVisible
    }

    // Focus mode (§16.1): F11 toggles on Windows/Linux; Cmd+Ctrl+F does so on
    // macOS. Escape exits when active (a single-key
    // exit, as the plan requires). Typewriter mode has no default shortcut.
    Shortcut {
        sequence: Qt.platform.os === "osx" ? "Meta+Ctrl+F" : "F11"
        context: Qt.ApplicationShortcut
        onActivated: shortcuts.appWindow.focusMode =
            !shortcuts.appWindow.focusMode
    }

    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: shortcuts.appWindow
                 && shortcuts.appWindow.focusMode
        onActivated: shortcuts.appWindow.focusMode = false
    }

    Shortcut {
        sequence: "F1"
        onActivated: shortcuts.appWindow.openShortcutReference()
    }

    // Global search (§8.4; the Obsidian/VSCode convention — the spec
    // assigns no key).
    Shortcut {
        sequence: "Ctrl+Shift+F"
        context: Qt.ApplicationShortcut
        enabled: shortcuts.appWindow
                 && shortcuts.appWindow.collectionOpen
        onActivated: {
            shortcuts.appWindow.panelsVisible = true
            shortcuts.sidebarPanel.focusSearch()
        }
    }
}
