// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The strip to the left of a block: the plus-button that adds a block below
// it (features.md §3.7), the dotted handle that both selects the block (§3.1)
// and starts a reorder drag (§3.2), and a menu button that exposes the block
// context menu without requiring a right-click.
//
// Selecting and dragging are the same press. Which one it was is only known
// on release, so the threshold test lives here: under five pixels of movement
// the press-release is a click that selects the row, and past it the gesture
// becomes a drag and the release drops it. That test is the whole reason this
// is a component rather than two buttons — it is the piece worth being able
// to reason about on its own.
//
// The drag coordinator itself belongs to the window, because a drag crosses
// rows and this one only knows its own. The gutter reports scene coordinates
// and lets the block forward them.
Item {
    id: root

    // Whether the pointer is anywhere over the row. The buttons fade in on
    // it, which is why it comes from the block rather than from the handler
    // below: the buttons occlude the row's own hover area, and reading only
    // that would make showing them look like leaving the row.
    property bool rowHovered: false
    // Whether a drag can start at all. False when the window has no drag
    // coordinator, in which case the press stays a select-block click rather
    // than becoming a drag that nothing would carry.
    property bool dragEnabled: false

    // Whether the pointer is over the gutter itself, which the block folds
    // into the row hover state it passes back in.
    readonly property alias hovered: gutterHover.hovered

    signal insertRequested()
    signal deleteRequested()
    signal handleMenuRequested()
    signal blockSelectRequested()
    signal dragStarted(real sceneX, real sceneY)
    signal dragMoved(real sceneX, real sceneY)
    signal dragDropped()
    signal dragCanceled()

    // Wide enough for both columns and the gap between them. The plus and
    // delete occupy the left column; the drag handle and menu button occupy
    // the right. The HoverHandler must cover the second row so the controls
    // stay visible while the pointer moves between them.
    width: Interface.px(40)
    height: Interface.px(44)

    HoverHandler {
        id: gutterHover
    }

    Row {
        objectName: "gutterButtons"
        // Top-anchored, not centre-in: the row is two buttons tall now (plus
        // over delete), so centring would push the plus and handle down off
        // the block's first line.
        anchors.top: parent.top
        anchors.topMargin: Interface.px(3)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Interface.px(4)
        // Stays visible while the handle is pressed: hiding an item cancels
        // its MouseArea's grab, which would kill a drag the moment the
        // pointer left this block's hover area (bites multi-drags, whose
        // source row does not follow the pointer).
        opacity: root.rowHovered || handleArea.pressed ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: 150 * Theme.motionScale }
        }

        // The plus (add a block below) with the delete (remove this block)
        // stacked under it; the handle and menu button form the other column.
        Column {
            spacing: Interface.px(2)

            IconButton {
                objectName: "plusButton"
                width: Interface.px(18)
                height: Interface.px(18)
                glyph: "+"
                glyphSize: Interface.px(14)
                glyphBold: true
                label: qsTr("Insert block below")
                onClicked: root.insertRequested()
            }

            // Delete this block. Undoable (Ctrl+Z), so no confirmation; the
            // red hover fill is the destructive cue. Stays lit while the
            // pointer is on it because the gutter HoverHandler covers it.
            IconButton {
                objectName: "deleteButton"
                width: Interface.px(18)
                height: Interface.px(18)
                glyph: "×"
                glyphSize: Interface.px(16)
                glyphBold: true
                hoverColor: Theme.danger
                hoverGlyphColor: Theme.labelOn(Theme.danger)
                label: qsTr("Delete block")
                onClicked: root.deleteRequested()
            }
        }

        Column {
            spacing: Interface.px(2)

            // Not an IconButton: this one press is both a click and the
            // start of a drag, and which it was is only known on release,
            // so the MouseArea below has to stay. What it gains here is the
            // half a button gives for free — a role, a name, tab focus and
            // a focus ring — with Space and Return standing in for the
            // click half of the gesture. Reordering from the keyboard
            // is Alt+Up and Alt+Down on the block itself, so this
            // control does not have to carry the drag half.
            Item {
                id: handleItem
                width: Interface.px(14)
                height: Interface.px(18)

                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Block handle")
                Accessible.description: qsTr("Selects the block; drag to reorder")
                Accessible.onPressAction: root.blockSelectRequested()
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter) {
                        root.blockSelectRequested()
                        event.accepted = true
                    }
                }

                ToolTip.text: qsTr("Block handle")
                ToolTip.visible: handleItem.activeFocus

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -2
                    radius: Interface.px(4)
                    color: "transparent"
                    visible: handleItem.activeFocus
                    border.width: 2
                    border.color: Theme.focusRing
                }

                Column {
                    anchors.centerIn: parent
                    spacing: Interface.px(2)
                    opacity: 0.6

                    Repeater {
                        model: 2

                        Row {
                            spacing: Interface.px(2)

                            Repeater {
                                model: 2

                                Rectangle {
                                    width: Interface.px(3)
                                    height: Interface.px(3)
                                    radius: 1.5
                                    color: Theme.textFaint
                                }
                            }
                        }
                    }
                }

                // preventStealing keeps the Flickable from grabbing the
                // vertical movement a drag starts with.
                MouseArea {
                    id: handleArea
                    objectName: "dragHandle"
                    anchors.fill: parent
                    anchors.margins: -2
                    hoverEnabled: true
                    cursorShape: Qt.OpenHandCursor
                    preventStealing: true
                    // Right-click remains a second route to the block menu.
                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                    property real pressX: 0
                    property real pressY: 0
                    property bool dragging: false

                    onPressed: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            root.handleMenuRequested()
                            return
                        }
                        pressX = mouse.x
                        pressY = mouse.y
                        dragging = false
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed
                            || (pressedButtons & Qt.RightButton))
                            return
                        if (!root.dragEnabled)
                            return
                        var sp = handleArea.mapToItem(null, mouse.x, mouse.y)
                        if (!dragging) {
                            if (Math.abs(mouse.x - pressX) < 5
                                && Math.abs(mouse.y - pressY) < 5)
                                return
                            dragging = true
                            root.dragStarted(sp.x, sp.y)
                        } else {
                            root.dragMoved(sp.x, sp.y)
                        }
                    }
                    onReleased: function(mouse) {
                        if (mouse.button === Qt.RightButton)
                            return
                        if (dragging) {
                            dragging = false
                            root.dragDropped()
                            return
                        }
                        root.blockSelectRequested()
                    }
                    onCanceled: {
                        if (dragging) {
                            dragging = false
                            root.dragCanceled()
                        }
                    }
                }
            }

            IconButton {
                objectName: "blockMenuButtonArea"
                width: Interface.px(14)
                height: Interface.px(18)
                label: qsTr("Block menu")
                Accessible.role: Accessible.ButtonMenu
                onClicked: root.handleMenuRequested()

                // Three stacked rules rather than a glyph, so the drawing
                // replaces IconButton's text content item.
                contentItem: Item {
                    Column {
                        anchors.centerIn: parent
                        spacing: Interface.px(2)

                        Repeater {
                            model: 3
                            Rectangle {
                                width: Interface.px(8)
                                height: 1.5
                                radius: 0.75
                                color: Theme.textMuted
                            }
                        }
                    }
                }
            }
        }
    }
}
