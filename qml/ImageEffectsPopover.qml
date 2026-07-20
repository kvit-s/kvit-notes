// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls

// Image-effects popover (features.md §1.2.8, phase12 §1.2.8): toggle rounded
// corners, drop shadow, a border, and the maintain-aspect option. It reports a
// canonical attribute payload through `applied`, computed from the CURRENT
// attributes so unrelated keys (e.g. alignment) are preserved; the image writes
// it via setBlockAttributes (one undo step per toggle).
Popup {
    id: root

    property string attributes: ""
    signal applied(string payload)

    padding: 8
    modal: false
    focus: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        color: theme.popupBackground
        border.color: theme.borderStrong
        border.width: 1
        radius: 6
    }

    function toggleFlag(key) {
        root.applied(blockAttributes.withFlag(root.attributes, key,
                     !blockAttributes.has(root.attributes, key)))
    }

    // A checkbox-styled row driven by external state (no internal toggle, so the
    // `on` binding never breaks on click — the model is the single truth).
    component ToggleRow: Item {
        property string label: ""
        property bool on: false
        signal toggled()
        implicitWidth: 180
        implicitHeight: 26
        Rectangle {
            id: box
            width: 16; height: 16; radius: 3
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: parent.on ? theme.accent : "transparent"
            border.width: 1
            border.color: parent.on ? theme.accent : theme.border
            Text {
                anchors.centerIn: parent
                visible: box.parent.on
                text: "✓"; color: "white"; font.pixelSize: 11
            }
        }
        Text {
            anchors.left: box.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: parent.label
            color: theme.textPrimary
            font.pixelSize: 12
        }
        TapHandler { onTapped: parent.toggled() }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    contentItem: Column {
        spacing: 4
        ToggleRow {
            label: qsTr("Rounded corners")
            on: blockAttributes.has(root.attributes, "rounded")
            onToggled: root.toggleFlag("rounded")
        }
        ToggleRow {
            label: qsTr("Drop shadow")
            on: blockAttributes.has(root.attributes, "shadow")
            onToggled: root.toggleFlag("shadow")
        }
        ToggleRow {
            label: qsTr("Border")
            on: blockAttributes.has(root.attributes, "border")
            onToggled: root.toggleFlag("border")
        }
        ToggleRow {
            label: qsTr("Maintain aspect ratio")
            // Maintained by default; unchecking stores aspect=stretch.
            on: blockAttributes.str(root.attributes, "aspect", "") !== "stretch"
            onToggled: {
                var isMaintain =
                    blockAttributes.str(root.attributes, "aspect", "") !== "stretch"
                root.applied(isMaintain
                    ? blockAttributes.withValue(root.attributes, "aspect", "stretch")
                    : blockAttributes.without(root.attributes, "aspect"))
            }
        }
    }
}
