// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls

// The editor window, as a block delegate sees it.
//
// Delegates reach the window they are in with `Window.window`, which is typed
// QQuickWindow and declares none of the editor's own state. Every read was
// therefore unresolvable, and there were 179 of them across thirteen files —
// mostly drag state, plus which block last had focus and a few editor modes.
//
// Declaring those here lets a delegate say
//
//     readonly property KvitShell shell: Window.window as KvitShell
//
// once per file and then read `shell.blockDrag.active` like any other typed
// expression. main.qml's root IS this type, so nothing moves and nothing is
// mirrored: `blockDrag` is still owned by the window that owns it, and the
// properties below are assigned there.
//
// The cast yields null for any other window, so a delegate hosted somewhere
// without an editor shell gets null rather than a wrong answer — which is why
// existing null guards stay where they are. The cast makes the read typed; it
// does not make the window guaranteed to be present.
//
// This is deliberately the small surface delegates actually use, not
// everything main.qml has. Anything added here becomes something thirteen
// files may depend on, so the cost of a new entry is higher than it looks.
ApplicationWindow {
    // Block drag-and-drop. See BlockDragState for what the delegates read.
    property BlockDragState blockDrag: null
    // The cross-block text drag, which is a different gesture with its own
    // state; delegates only ask whether one is running.
    property QtObject crossBlockDrag: null

    // Which row the caret is in, and which row last had it. `caretBlockIndex`
    // is live; `lastFocusedBlock` survives focus leaving the editor, which is
    // what lets focus return to the right place.
    property int caretBlockIndex: -1
    property int lastFocusedBlock: 0

    // Typewriter scrolling (features.md §10.4): the caret line is kept
    // vertically centred. Delegates check it before scrolling themselves.
    property bool typewriterMode: false
}
