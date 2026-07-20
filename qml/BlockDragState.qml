// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// The block drag-and-drop state, as the delegates see it.
//
// Thirteen delegate files read this object 132 times, and until now it was
// an anonymous `QtObject` inside main.qml reachable only by name through
// `Window.window.blockDrag`. Seven members carry all of that traffic:
// `active`, `isMulti`, `sourceIndex`, `begin`, `update`, `drop`, `cancel`.
// That is an interface, and it was written down nowhere.
//
// Only the interface lives here. The implementation stays in main.qml,
// because it drives that window's own view objects — blockListView, the
// edge scroller, the drag proxy — and moving it would mean injecting four
// references back in, which is restructuring rather than naming. main.qml
// declares its instance AS this type and overrides the members, the same
// arrangement BlockDelegateBase has with the delegates: the type states the
// contract, the instance supplies the behaviour and keeps its scope.
//
// The defaults below are what an inert drag reports, so a reader of a
// delegate can see what "no drag in progress" looks like without opening
// main.qml.
QtObject {
    // Whether a drag is in progress at all. Delegates gate their drop
    // indicators and hover styling on this.
    property bool active: false
    // A multi-block drag, started from the handle of a selected block, moves
    // the whole selection and shows a gap indicator instead of live-moving a
    // single row.
    property bool isMulti: false
    // Live position of the dragged row, which changes during a single-block
    // drag as the row is moved through the list.
    property int sourceIndex: -1

    // Scene coordinates throughout: a delegate knows where it was pressed,
    // not where the list thinks that is.
    function begin(index, sceneX, sceneY) {}
    function update(sceneX, sceneY) {}
    // Commit at the current position, or abandon and put everything back.
    function drop() {}
    function cancel() {}
}
