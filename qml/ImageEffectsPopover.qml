// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Kvit 1.0

// Image-effects popover (features.md §1.2.8): toggle rounded
// corners, drop shadow, a border, and the maintain-aspect option. It reports a
// canonical attribute payload through `applied`, computed from the CURRENT
// attributes so unrelated keys (e.g. alignment) are preserved; the image writes
// it via setBlockAttributes (one undo step per toggle).
//
// Keyboard and screen-reader behaviour follows ColorPicker.qml
// (accessibility.md Finding 2). These four rows are checkboxes drawn by hand,
// so each one publishes its own checked state; the popup stays open while
// they are used, which is why they are tab stops rather than one arrow-key
// list.
Popup {
    id: root

    property string attributes: ""
    signal applied(string payload)

    // What had the keyboard before this opened, so closing can hand it back.
    property Item openedFrom: null
    onAboutToShow: {
        const w = root.parent ? root.parent.Window.window : null
        root.openedFrom = w ? w.activeFocusItem : null
    }
    onClosed: {
        if (root.openedFrom)
            root.openedFrom.forceActiveFocus()
        root.openedFrom = null
    }

    padding: Interface.px(8)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // Qt leaves a popup outside its window unless it is given a margin, and
    // an image can sit at the bottom of the page.
    margins: 6

    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: Interface.px(6)
    }

    function toggleFlag(key) {
        root.applied(BlockAttributes.withFlag(root.attributes, key,
                     !BlockAttributes.has(root.attributes, key)))
    }

    // A checkbox-styled row driven by external state (no internal toggle, so the
    // `on` binding never breaks on click — the model is the single truth).
    component ToggleRow: Item {
        id: row
        property string label: ""
        property bool on: false
        signal toggled()
        implicitWidth: Interface.px(180)
        implicitHeight: Interface.px(26)

        activeFocusOnTab: true
        Accessible.role: Accessible.CheckBox
        Accessible.name: row.label
        Accessible.checkable: true
        Accessible.checked: row.on
        Accessible.onToggleAction: row.toggled()
        Accessible.onPressAction: row.toggled()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
                row.toggled()
                event.accepted = true
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: Interface.px(4)
            visible: row.activeFocus
            border.width: 2
            border.color: Theme.focusRing
        }
        Rectangle {
            id: box
            width: 16; height: 16; radius: 3
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: row.on ? Theme.accent : "transparent"
            border.width: 1
            border.color: row.on ? Theme.accent : Theme.borderStrong
            Text {
                anchors.centerIn: parent
                visible: row.on
                text: "✓"; color: Theme.onAccent; font.pixelSize: Interface.small
            }
        }
        Text {
            anchors.left: box.right
            anchors.leftMargin: Interface.px(8)
            anchors.verticalCenter: parent.verticalCenter
            text: row.label
            color: Theme.textPrimary
            font.pixelSize: Interface.body
        }
        TapHandler { onTapped: row.toggled() }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    contentItem: Column {
        spacing: Interface.px(4)
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Image effects")
        ToggleRow {
            label: qsTr("Rounded corners")
            on: BlockAttributes.has(root.attributes, "rounded")
            onToggled: root.toggleFlag("rounded")
        }
        ToggleRow {
            label: qsTr("Drop shadow")
            on: BlockAttributes.has(root.attributes, "shadow")
            onToggled: root.toggleFlag("shadow")
        }
        ToggleRow {
            label: qsTr("Border")
            on: BlockAttributes.has(root.attributes, "border")
            onToggled: root.toggleFlag("border")
        }
        ToggleRow {
            label: qsTr("Maintain aspect ratio")
            // Maintained by default; unchecking stores aspect=stretch.
            on: BlockAttributes.str(root.attributes, "aspect", "") !== "stretch"
            onToggled: {
                var isMaintain =
                    BlockAttributes.str(root.attributes, "aspect", "") !== "stretch"
                root.applied(isMaintain
                    ? BlockAttributes.withValue(root.attributes, "aspect", "stretch")
                    : BlockAttributes.without(root.attributes, "aspect"))
            }
        }
    }
}
