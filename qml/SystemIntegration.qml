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

    Connections {
        target: GlobalHotkey
        function onActivated() { integration.openQuickCapture() }
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

    Connections {
        target: SystemTray
        function onQuickCaptureRequested() { integration.openQuickCapture() }
        function onNewNoteRequested() {
            integration.appWindow.createNoteInCurrentScope()
        }
        function onShowWindowRequested() {
            integration.appWindow.show()
            integration.appWindow.raise()
            integration.appWindow.requestActivate()
        }
    }
}
