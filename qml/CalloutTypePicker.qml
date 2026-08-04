// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The ListView delegate and its handlers are separate component scopes, so
// their reads of `root` and of the delegate's own role are unqualified
// access. The delegate declares that role as a required property, so binding
// the scopes changes nothing it depended on.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Kvit 1.0

// Callout type picker (features.md §1.2.10): the seven kinds a callout can
// be, each with the icon and accent it renders in. It only reports the
// choice; the callout applies it as one undo step through setCalloutType,
// which keeps the body, title, fold state and any custom colour.
//
// The rows come in from the delegate rather than being listed here, so the
// picker and the renderer cannot disagree about what types exist.
//
// Keyboard and screen-reader behaviour follows ColorPicker.qml
// (accessibility.md Finding 2); a ListView carries the up/down movement the
// Repeater of rectangles had no way to offer.
Popup {
    id: root

    // [{ key, icon, label }], in header order.
    property var types: []
    // The callout's current type, whose row is marked.
    property string currentType: ""

    signal typePicked(string value)

    // What had the keyboard before this opened, so closing can hand it back.
    property Item openedFrom: null

    padding: Interface.px(6)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // A popup with negative margins — Qt's default — is not pushed within
    // its window, and this one opens below a button that can sit anywhere
    // down the page. On a callout at the foot of a note the seven kinds ran
    // past the bottom edge and were simply cut off, so the last of them could
    // be neither seen nor reached. Any margin at all makes Qt keep it inside.
    margins: 6

    function choose(key) {
        root.typePicked(key)
        root.close()
    }

    onAboutToShow: {
        const w = root.parent ? root.parent.Window.window : null
        root.openedFrom = w ? w.activeFocusItem : null
    }
    onOpened: typeList.forceActiveFocus()
    onClosed: {
        if (root.openedFrom)
            root.openedFrom.forceActiveFocus()
        root.openedFrom = null
    }

    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: Interface.px(6)
    }

    contentItem: ListView {
        id: typeList
        objectName: "calloutTypeList"
        // implicit, not width/height: a Popup sizes its content item to its
        // own available space, so an explicit height here is overwritten and
        // the popup is left with no size of its own to keep inside the
        // window. That is what pushes the last kind off the bottom edge.
        implicitWidth: Interface.px(148)
        implicitHeight: contentHeight
        spacing: Interface.px(2)
        interactive: false
        keyNavigationEnabled: true
        focus: true
        model: root.types
        currentIndex: {
            for (var i = 0; i < root.types.length; ++i) {
                if (root.types[i].key === root.currentType)
                    return i
            }
            return 0
        }
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Callout type")

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                || event.key === Qt.Key_Space) {
                const chosen = root.types[typeList.currentIndex]
                if (chosen)
                    root.choose(chosen.key)
                event.accepted = true
            }
        }

        delegate: Rectangle {
            id: row
            // Named so the handlers below, each its own scope, can reach
            // the role rather than relying on injection.
            required property var modelData
            required property int index

            readonly property bool isCurrent:
                root.currentType === row.modelData.key
            readonly property bool isFocused: typeList.currentIndex === row.index

            width: Interface.px(148)
            height: Interface.px(26)
            radius: Interface.px(4)
            color: rowHover.hovered ? Theme.hoverTint
                 : (row.isCurrent ? Theme.selectionTint : "transparent")
            border.width: (typeList.activeFocus && row.isFocused) ? 2 : 0
            border.color: Theme.focusRing

            Accessible.role: Accessible.RadioButton
            Accessible.name: row.modelData.label
            Accessible.checkable: true
            Accessible.checked: row.isCurrent
            Accessible.focused: row.isFocused
            Accessible.onPressAction: root.choose(row.modelData.key)

            Text {
                id: rowIcon
                objectName: "calloutTypeIcon"
                anchors.left: parent.left
                anchors.leftMargin: Interface.px(8)
                anchors.verticalCenter: parent.verticalCenter
                width: Interface.px(14)
                horizontalAlignment: Text.AlignHCenter
                // A toggle has no icon of its own; its chevron stands in,
                // so the row still reads as a row.
                text: row.modelData.icon !== "" ? row.modelData.icon : "▾"
                color: Theme.textSecondary
                font.pixelSize: Interface.body
                font.bold: true
            }
            Text {
                objectName: "calloutTypeLabel"
                anchors.left: rowIcon.right
                anchors.leftMargin: Interface.px(8)
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.label
                color: Theme.textPrimary
                font.pixelSize: Interface.body
            }

            HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                onTapped: {
                    typeList.currentIndex = row.index
                    root.choose(row.modelData.key)
                }
            }
        }
    }
}
