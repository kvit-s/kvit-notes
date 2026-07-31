// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Reaching the application from outside its window (features.md §15): the
// tray icon's menu, the system-wide hotkey, and the small capture window they
// open.
//
// The in-app chord reads the same setting the system-wide registration uses,
// so changing the chord moves both. Hard-coding it here left the setting
// appearing to do nothing on every platform without a working grab, which is
// all of them today.
Item {
    id: integration

    // Wired by main.qml.
    property var appWindow

    // Capture only makes sense with somewhere to put the note.
    function openQuickCapture() {
        if (integration.appWindow.collectionOpen)
            quickCaptureWindow.openCapture()
    }

    QuickCaptureWindow {
        id: quickCaptureWindow
        onCaptured: function(relPath) {
            // Surface the captured note in the running window.
            if (integration.appWindow.collectionOpen)
                integration.appWindow.openNoteByPath(relPath)
        }
    }

    // The system-wide grab is one registration for the whole process, so every
    // window's shell hears it. Gated on the same flag the tray actions use, or
    // one press of the hotkey opens a capture window in front of every open
    // vault at once — each of them writing into a different vault, and only
    // the one on top visibly.
    Connections {
        target: GlobalHotkey
        function onActivated() {
            if (AppActions.trayTarget)
                integration.openQuickCapture()
        }
    }

    // The in-app chord, which works while the window is focused, so capture is
    // reachable where the system-wide grab is not available.
    Shortcut {
        sequence: {
            var r = AppSettings.revision // re-evaluate when a setting changes
            return AppSettings.value("hotkey.quickCapture", "Ctrl+Alt+N")
        }
        onActivated: integration.openQuickCapture()
    }

    // Tray menu actions (new note, quick capture, show window). The tray is one
    // shared object, so every window's shell hears these; AppActions.trayTarget
    // gates them so only the active window (the one the registry designated)
    // responds. A single-window composition leaves trayTarget true, so it works
    // without a registry — which is what the integration tests rely on.
    Connections {
        target: SystemTray
        function onQuickCaptureRequested() {
            if (AppActions.trayTarget)
                integration.openQuickCapture()
        }
        function onNewNoteRequested() {
            if (AppActions.trayTarget)
                integration.appWindow.createNoteInCurrentScope()
        }
        function onShowWindowRequested() {
            if (!AppActions.trayTarget)
                return
            integration.appWindow.show()
            integration.appWindow.raise()
            integration.appWindow.requestActivate()
        }
    }
}
