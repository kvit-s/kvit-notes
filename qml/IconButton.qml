// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// A glyph-labelled button that a screen reader can see (accessibility.md
// Finding 1).
//
// The application draws most of its small controls as a Rectangle with a
// MouseArea on it. Nothing about such an item reaches QAccessible, so an
// assistive technology cannot announce it, find it or activate it, and it is
// equally unreachable from the keyboard. This component is the replacement:
// it is a real control, so Qt publishes it with a role and a name, it takes
// tab focus, and Space or Return activates it.
//
// `glyph` is decoration and `label` is what the control is called. Both the
// tooltip and the accessible name come from `label`, so the two cannot drift
// apart — which is the failure mode of writing them separately, since only
// one of them is visible while working on the file.
//
// The tooltip shows on keyboard focus as well as hover. A pointer user
// otherwise gets an explanation that a keyboard user never sees.
AbstractButton {
    id: control

    // The character (or short string) drawn in the button.
    property string glyph: ""
    // What the control is called: the accessible name and the tooltip.
    property string label: ""
    // Longer help, announced after the name. Empty means the label says
    // everything there is to say.
    property string help: ""

    property color glyphColor: Theme.textMuted
    property color hoverGlyphColor: control.glyphColor
    property color hoverColor: Theme.hoverTint
    property color checkedColor: Theme.selectionTint
    // A design-pixel value, like the implicit size below: a call site writes
    // the size it wants at the default interface size and the scale is
    // applied here, so the glyph and the box it sits in grow together.
    property int glyphSize: Interface.px(14)
    property bool glyphBold: false
    property real backgroundRadius: 4
    // Whether the pointer changes to the pointing hand. Off for controls
    // that start a drag, which want their own cursor.
    property bool pointingCursor: true

    implicitWidth: Interface.px(18)
    implicitHeight: Interface.px(18)
    padding: 0
    hoverEnabled: true
    activeFocusOnTab: true
    // Mouse presses deliberately do not focus: these sit beside a text
    // editor, and clicking one must not take the caret out of the block.
    focusPolicy: Qt.TabFocus

    Accessible.role: control.checkable ? Accessible.CheckBox : Accessible.Button
    Accessible.name: control.label
    Accessible.description: control.help
    Accessible.checkable: control.checkable
    Accessible.checked: control.checked
    Accessible.onPressAction: control.clicked()
    Accessible.onToggleAction: control.toggle()

    ToolTip.text: control.help !== "" ? control.help : control.label
    ToolTip.visible: (control.hovered || control.activeFocus)
                     && control.ToolTip.text !== ""
    ToolTip.delay: control.activeFocus ? 0 : 500

    // AbstractButton already activates on Space. Return and Enter are the
    // other key a person expects to work on a focused button, and Qt only
    // binds those on a dialog's default button.
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (control.checkable)
                control.toggle()
            control.clicked()
            event.accepted = true
        }
    }

    HoverHandler {
        enabled: control.pointingCursor && control.enabled
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: control.backgroundRadius
        color: control.checked ? control.checkedColor
             : control.hovered && control.enabled ? control.hoverColor
             : "transparent"
        // Presses do not focus, so active focus here can only have come
        // from the keyboard — which is exactly when the ring is wanted.
        border.width: control.activeFocus ? 2 : 0
        border.color: Theme.focusRing
    }

    contentItem: Text {
        text: control.glyph
        color: control.hovered && control.enabled ? control.hoverGlyphColor
                                                  : control.glyphColor
        font.pixelSize: control.glyphSize
        font.bold: control.glyphBold
        opacity: control.enabled ? 1.0 : 0.45
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideNone
    }
}
