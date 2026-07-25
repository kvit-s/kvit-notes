// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The Repeater delegate and its handlers are separate component scopes, so
// their reads of `root` and of the delegate's own role are unqualified
// access. The delegate declares that role as a required property, so binding
// the scopes changes nothing it depended on.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// Callout type picker (features.md §1.2.10): the seven kinds a callout can
// be, each with the icon and accent it renders in. It only reports the
// choice; the callout applies it as one undo step through setCalloutType,
// which keeps the body, title, fold state and any custom colour.
//
// The rows come in from the delegate rather than being listed here, so the
// picker and the renderer cannot disagree about what types exist.
Popup {
    id: root

    // [{ key, icon, label }], in header order.
    property var types: []
    // The callout's current type, whose row is marked.
    property string currentType: ""

    signal typePicked(string value)

    padding: 6
    modal: false
    focus: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: 6
    }

    contentItem: Column {
        spacing: 2

        Repeater {
            model: root.types
            delegate: Rectangle {
                id: row
                // Named so the handlers below, each its own scope, can reach
                // the role rather than relying on injection.
                required property var modelData

                width: 148
                height: 26
                radius: 4
                color: rowHover.hovered ? Theme.hoverTint
                     : (root.currentType === row.modelData.key
                        ? Theme.selectionTint : "transparent")

                Text {
                    id: rowIcon
                    objectName: "calloutTypeIcon"
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    horizontalAlignment: Text.AlignHCenter
                    // A toggle has no icon of its own; its chevron stands in,
                    // so the row still reads as a row.
                    text: row.modelData.icon !== "" ? row.modelData.icon : "▾"
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    font.bold: true
                }
                Text {
                    objectName: "calloutTypeLabel"
                    anchors.left: rowIcon.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.label
                    color: Theme.textPrimary
                    font.pixelSize: 12
                }

                HoverHandler { id: rowHover }
                TapHandler {
                    onTapped: {
                        root.typePicked(row.modelData.key)
                        root.close()
                    }
                }
            }
        }
    }
}
