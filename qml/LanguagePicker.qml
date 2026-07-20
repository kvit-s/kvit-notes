// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls

// The code-block language selector (phase10 step 1, decision 3). A menu of
// the eleven recognized languages plus "Plain text" (no highlighting),
// driven by the canonical `codeLanguageList` context property so the choices
// can never drift from what the highlighter supports. Opened from the code
// block's header button; the chosen id is written back as one undo step by
// the caller (EditableBlock.setCodeLanguage).
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

    MenuItem {
        text: (root.currentLanguage === "" ? "✓  " : "     ") + "Plain text"
        onTriggered: root.languageChosen("")
    }
    // The `plain` opt-out (diagrams-prd.md §5.3): unhighlighted code that the
    // diagram classifier never re-examines. Distinct from "Plain text" (empty
    // language), which stays eligible for auto-detection on the next ingest.
    MenuItem {
        text: (root.currentLanguage === "plain" ? "✓  " : "     ") + "Plain code"
        onTriggered: root.languageChosen("plain")
    }
    MenuSeparator {}
    // Diagram families (diagrams-prd.md §5.3): convert this fence to a text
    // diagram or a Mermaid diagram. Selecting one reroutes the block to its
    // diagram delegate as one undo step through the convertBlock(language) path.
    MenuItem {
        text: (root.currentLanguage === "mermaid" ? "✓  " : "     ") + "Mermaid"
        onTriggered: root.languageChosen("mermaid")
    }
    MenuItem {
        text: (root.currentLanguage === "diagram" ? "✓  " : "     ") + "Text diagram"
        onTriggered: root.languageChosen("diagram")
    }
    MenuSeparator {}
    Repeater {
        model: codeLanguageList
        MenuItem {
            required property var modelData
            text: (root.currentLanguage === modelData ? "✓  " : "     ")
                  + (root.displayNames[modelData] !== undefined
                     ? root.displayNames[modelData] : modelData)
            onTriggered: root.languageChosen(modelData)
        }
    }
}
