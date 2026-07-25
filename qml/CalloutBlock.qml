// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Callout / toggle block (features.md §1.2.10, §1.2.13). A quote-derived
// block: EditableBlock renders the multi-paragraph inline-formatted body,
// and calloutMode adds the typed header (icon + editable title + fold
// chevron) over a colored panel. The type reuses `language`, the fold state
// `checked`.
//
// The keyboard is the same here as in every other text block: Enter leaves
// for a new block (splitting the body when the caret is inside it), and
// Shift+Enter adds a line to the body. Enter used to add the line itself,
// which left the body with no key that ends it — a callout at the foot of a
// note could not be typed out of at all.
EditableBlock {
    id: root

    calloutMode: true
    placeholder: qsTr("Callout text…")
    contentColor: Theme.textPrimary
}
