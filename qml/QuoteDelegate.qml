// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick

// Quote block (features.md §1.2.6): stacked accent bars (one per nesting
// depth), muted text, and an optional attribution line — the last content
// line starting with "— " renders right-aligned below the body as chrome,
// excluded from the editable text via metaTail.
EditableBlock {
    id: root

    contentColor: theme.textSecondary

    // The attribution tail: the final "— …" line, with its leading newline,
    // when there is a body before it.
    metaTail: {
        var c = root.content
        var nl = c.lastIndexOf("\n")
        var lastLine = nl >= 0 ? c.substring(nl + 1) : c
        if (nl >= 0 && lastLine.indexOf("— ") === 0)
            return "\n" + lastLine
        return ""
    }
    readonly property string attributionText:
        metaTail !== "" ? metaTail.substring(3) : ""   // after "\n— "

    // Stacked accent bars, one per depth level (indentLevel + 1). The bars
    // live in a Row inside an Item — a Row's implicitWidth is read-only, so
    // the width the leading loader reads comes from the wrapping Item.
    leadingChrome: Component {
        Item {
            implicitWidth: (root.indentLevel + 1) * 6
            Row {
                anchors.fill: parent
                spacing: 3
                Repeater {
                    model: root.indentLevel + 1
                    delegate: Rectangle {
                        objectName: "quoteBar"
                        width: 3
                        radius: 1.5
                        color: theme.quoteBar
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 8
                    }
                }
            }
        }
    }

    // The attribution line below the body.
    trailingChrome: root.attributionText !== "" ? attributionComponent : null
    Component {
        id: attributionComponent
        Item {
            implicitHeight: attrText.implicitHeight + 4
            Text {
                id: attrText
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.top: parent.top
                text: "— " + root.attributionText
                color: theme.textMuted
                font.pixelSize: Math.max(11, typography.baseSize - 2)
                font.italic: false
            }
        }
    }
}
