// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import Kvit 1.0

// Callout / toggle block (features.md §1.2.10, §1.2.13). A quote-derived
// block: EditableBlock renders the multi-paragraph inline-formatted body,
// and calloutMode adds the typed header (icon + editable title + fold
// chevron) over a colored panel. The type reuses `language`, the fold state
// `checked`. Enter adds a paragraph line within the body (one block, like a
// quote), and a trailing empty line exits below the callout.
EditableBlock {
    id: root

    calloutMode: true
    enterInsertsNewline: true
    placeholder: qsTr("Callout text…")
    contentColor: theme.textPrimary
}
