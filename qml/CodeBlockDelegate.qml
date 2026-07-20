// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// Basic code block (features.md §1.2.7): monospace on a light panel,
// whitespace preserved exactly, content verbatim — the engine parses and
// reveals nothing, and formatting/link shortcuts are inert. Syntax
// highlighting and the language selector ride on the codeChrome flag
// below; the language tag is stored for round-trip.
EditableBlock {
    id: root

    verbatimEditing: true
    enterInsertsNewline: true
    showPanel: true
    // Syntax highlighting (via the engine's codeLanguage),
    // the language selector, optional line-number gutter, copy button, and
    // horizontal scrolling for long lines all ride on this flag.
    codeChrome: true
    contentFontFamily: typography.monoFamily
    contentFontSize: {
        var base = typography.baseSize  // re-evaluation dependency
        return typography.sizeForBlockType(root.blockType)
    }
}
