// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Kvit 1.0

// A Dialog that places the keyboard when it opens and hands it back when it
// closes (accessibility.md Finding 6).
//
// Modality already traps focus inside an open dialog, so a plain Dialog is
// not reachable-from-outside; what it does not do is decide where inside
// itself the keyboard starts, or where it goes afterwards. Both gaps show up
// the same way: a screen reader announces the dialog's title and then falls
// silent, and a keyboard user presses Tab an unknown number of times to reach
// the first field. On close the caret was left wherever Qt happened to put
// it rather than back on the control that opened the dialog.
//
// Set `initialFocusItem` to the field the dialog is really about — the text
// field of a rename, the list of a chooser. Leaving it unset falls back to
// the footer's own safest button, which is the right answer for the many
// dialogs here that are a sentence and two or three choices: focus lands on
// the one that changes nothing, so Return and Space are safe from the first
// moment and the destructive choice has to be reached for.
//
// The three signals are taken through Connections rather than through
// `onOpened:` handlers. A signal handler is a property, so a handler written
// at the use site would replace one written here, and several of these
// dialogs do define their own — the failure would be silent and would look
// like the base class simply not working.
//
// ONE TRAP, and it costs an afternoon to find. A `Popup` declared inside a
// dialog that derives from this type AND assigns its own `contentItem` — a
// progress dialog inside an import dialog, say — must set `parent`
// explicitly, to whatever the outer dialog is parented to:
//
//     KvitDialog {
//         id: outer
//         contentItem: ColumnLayout { … }
//         KvitDialog { parent: outer.parent; … }   // <- required
//     }
//
// Left to default, the inner popup is parented to the content item this base
// created, which the derived file's own `contentItem:` assignment then throws
// away. The inner popup is left holding an item that is in no window, and
// `Popup.open()` on a popup with no window returns having done nothing: no
// warning, no error, just a dialog that never appears. Declaring the same
// popup inside a plain `Dialog` works, because there is no base content item
// to be replaced — which is why this only shows up after the conversion.
Dialog {
    id: control

    // What should hold the keyboard once this is open.
    property Item initialFocusItem: null
    // What held it before, so closing can give it back. Recorded rather than
    // rediscovered: by the time the dialog is open it has the focus itself.
    property Item openedFrom: null

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    // The footer button that cancels, or the first one if none of them does.
    // Null for a dialog whose footer is not a button box, which then keeps
    // Qt's own choice unless initialFocusItem says otherwise.
    function safestButton() {
        // `footer` is typed as a plain item, so the cast is what makes the
        // button list readable; it yields null for a footer that is not a
        // button box, which is exactly the case this has no answer for.
        const box = control.footer as DialogButtonBox
        if (!box)
            return null
        let first = null
        for (let i = 0; i < box.contentChildren.length; ++i) {
            const b = box.contentChildren[i] as Button
            if (!b || !b.visible || !b.enabled)
                continue
            if (first === null)
                first = b
            if (b.DialogButtonBox.buttonRole === DialogButtonBox.RejectRole)
                return b
        }
        return first
    }

    // Whether `item` is one of this dialog's own — walked rather than asked,
    // since an item cannot be asked whether another is its ancestor.
    function ownsFocusItem(item) {
        let p = item
        while (p) {
            if (p === control.contentItem || p === control.footer
                || p === control.header)
                return true
            p = p.parent
        }
        return false
    }

    function focusFooterIfNothingElseTookIt() {
        if (!control.opened)
            return
        const w = control.parent ? control.parent.Window.window : null
        if (w && control.ownsFocusItem(w.activeFocusItem))
            return
        const b = control.safestButton()
        if (b)
            b.forceActiveFocus()
    }

    Connections {
        target: control
        function onAboutToShow() {
            const w = control.parent ? control.parent.Window.window : null
            control.openedFrom = w ? w.activeFocusItem : null
        }
        function onOpened() {
            if (control.initialFocusItem) {
                control.initialFocusItem.forceActiveFocus()
                return
            }
            // Deferred, because a dialog may place its own focus in its own
            // onOpened handler and the order two handlers on one signal run
            // in is not something worth depending on. By the next event loop
            // pass that handler has run, and the fallback below only fires if
            // nothing inside the dialog took the keyboard.
            Qt.callLater(control.focusFooterIfNothingElseTookIt)
        }
        function onClosed() {
            // A dialog can close because the window is going away, in which
            // case the item recorded on the way in may already be gone.
            if (control.openedFrom)
                control.openedFrom.forceActiveFocus()
            control.openedFrom = null
        }
    }
}
