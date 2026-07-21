// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The frame a callout block draws around its text (features.md §1.2.10): a
// tinted panel with an accent bar down its left edge, and a header carrying
// the fold chevron, the type icon, an editable title, and the custom-color
// dot.
//
// The chrome resolves nothing itself. A callout's type, fold state, and custom
// color are all block state that the delegate reads from the model and passes
// in as values, and every gesture leaves as a signal, because each of them is
// one undo step the model has to record. The title field is the exception that
// shapes the interface: it holds text the user has typed but not committed, so
// the delegate needs `commitPendingTitle()` to flush it at moments the field
// itself cannot see — the row being recycled by the list, or the document
// being saved.
Item {
    id: root

    // The color the whole frame derives from: the type's accent, or the
    // block's custom color where it has one.
    property color accent: Theme.textMuted
    // A folded callout collapses to its header; the body is hidden by the
    // delegate, which owns the text.
    property bool folded: false
    // Header glyph and placeholder for the callout's type. An unrecognized
    // type still gets both, so it renders as a callout rather than failing.
    property string icon: ""
    property string typeLabel: ""
    // The committed title, from the model.
    property string title: ""
    property int headerHeight: 0
    // The block's custom color ("" when it uses the typed default), which the
    // picker shows as the current choice.
    property string customColor: ""
    // Whether the pointer is over the row. The color dot stays faint until
    // then, so an unhovered callout shows no editing affordance.
    property bool rowHovered: false

    signal foldToggled()
    signal titleCommitted(string text)
    signal colorPicked(string value)
    signal colorResetRequested()

    // Push an uncommitted title into the model. Called on focus loss by the
    // field itself, and by the delegate for the two cases the field cannot
    // observe: the row leaving the view, and a save collecting pending edits.
    function commitPendingTitle() {
        if (titleField.text !== root.title)
            root.titleCommitted(titleField.text)
    }

    Rectangle {
        id: panel
        objectName: "calloutPanel"
        anchors.fill: parent
        radius: 5
        color: Qt.alpha(root.accent, 0.10)
        border.width: 1
        border.color: Qt.alpha(root.accent, 0.35)
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 4
            radius: 2
            color: root.accent
        }
    }

    Item {
        id: header
        objectName: "calloutHeader"
        x: 10
        y: 0
        z: 3
        width: root.width - 14
        height: root.headerHeight

        Text {
            id: chevron
            objectName: "calloutFoldChevron"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.folded ? "▸" : "▾"
            color: root.accent
            font.pixelSize: 12
            TapHandler { onTapped: root.foldToggled() }
        }
        Text {
            id: typeIcon
            anchors.left: chevron.right
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            visible: root.icon !== ""
            text: root.icon
            color: root.accent
            font.pixelSize: 13
            font.bold: true
        }
        TextField {
            id: titleField
            objectName: "calloutTitleField"
            anchors.left: typeIcon.visible ? typeIcon.right : chevron.right
            anchors.leftMargin: 6
            anchors.right: colorDot.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            placeholderText: root.typeLabel
            color: root.accent
            font.pixelSize: 13
            font.bold: true
            background: null
            padding: 0
            Connections {
                target: DocumentManager
                function onPendingEditsRequested() {
                    root.commitPendingTitle()
                }
            }
            onEditingFinished: root.commitPendingTitle()
        }
        Rectangle {
            id: colorDot
            objectName: "calloutColorDot"
            anchors.right: parent.right
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            width: 14; height: 14; radius: 7
            color: root.accent
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.4)
            opacity: root.rowHovered || colorPicker.visible ? 1 : 0.35
            Behavior on opacity { NumberAnimation { duration: 150 } }
            TapHandler { onTapped: colorPicker.open() }

            CalloutColorPicker {
                id: colorPicker
                x: parent ? parent.width - width : 0
                y: parent ? parent.height + 4 : 0
                currentColor: root.customColor
                onColorPicked: function(v) { root.colorPicked(v) }
                onResetRequested: root.colorResetRequested()
            }
        }
    }
}
