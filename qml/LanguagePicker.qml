// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The Repeater delegate below is its own component scope, so its references
// to `root` are what qmllint calls unqualified access. This binds the
// enclosing component's ids into the delegate, which is what makes them
// resolvable — and it is safe here because the delegate already declares
// `required property var modelData` rather than relying on injection.
//
// This file is the first to reach all its C++ state through the Kvit module,
// so it is the first the lint gate checks at full strength; the rest need the
// same treatment as they follow.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The code-block language selector. A menu of
// the eleven recognized languages plus "Plain text" (no highlighting),
// driven by the canonical `CodeLanguages.supported` list so the choices
// can never drift from what the highlighter supports. Opened from the code
// block's header button; the chosen id is written back as one undo step by
// the caller (EditableBlock.setCodeLanguage).
//
// Deliberately the one menu with no access keys. The entries are a list of
// values to pick from rather than commands to run, most of them come from a
// list in C++, and half the names ("C++", "JSON") share their first letters,
// so a key per entry would be arbitrary where the arrow keys already are not.
// The convention and the `&` spelling are in src/platform/menuaccesskeys.h.
Menu {
    id: root

    property string currentLanguage: ""
    signal languageChosen(string lang)

    // Human-friendly labels for the canonical ids.
    readonly property var displayNames: ({
        "python": "Python", "javascript": "JavaScript", "cpp": "C++",
        "java": "Java", "html": "HTML", "css": "CSS", "sql": "SQL",
        "bash": "Bash", "json": "JSON", "xml": "XML", "markdown": "Markdown"
    })

    DiscoverableMenuItem {
        text: (root.currentLanguage === "" ? "✓  " : "     ") + "Plain text"
        onTriggered: root.languageChosen("")
    }
    // The `plain` opt-out: unhighlighted code that the diagram classifier
    // never re-examines. Distinct from "Plain text" (empty language), which
    // stays eligible for auto-detection on the next ingest.
    DiscoverableMenuItem {
        text: (root.currentLanguage === "plain" ? "✓  " : "     ") + "Plain code"
        onTriggered: root.languageChosen("plain")
    }
    MenuSeparator {}
    // Diagram families: convert this fence to a text diagram or a Mermaid
    // diagram. Selecting one reroutes the block to its diagram delegate as one
    // undo step through the convertBlock(language) path.
    DiscoverableMenuItem {
        text: (root.currentLanguage === "mermaid" ? "✓  " : "     ") + "Mermaid"
        onTriggered: root.languageChosen("mermaid")
    }
    DiscoverableMenuItem {
        text: (root.currentLanguage === "diagram" ? "✓  " : "     ") + "Text diagram"
        onTriggered: root.languageChosen("diagram")
    }
    MenuSeparator {}
    Repeater {
        model: CodeLanguages.supported
        DiscoverableMenuItem {
            required property var modelData
            text: (root.currentLanguage === modelData ? "✓  " : "     ")
                  + (root.displayNames[modelData] !== undefined
                     ? root.displayNames[modelData] : modelData)
            onTriggered: root.languageChosen(modelData)
        }
    }
}
