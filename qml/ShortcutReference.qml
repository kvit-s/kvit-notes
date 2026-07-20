// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Two nested Repeaters, each delegate holding Labels that are their own
// scopes. Binding them lets the inner content address the category and
// the row by id instead of reaching a model role by injection.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// Discoverable keyboard-shortcut cheat sheet (features.md §13).
// It renders the ShortcutCatalog — the same source the test_shortcutmap audit
// checks — grouped by section. Actions without a shortcut (the documented
// deviations) show a dash and their reason, so nothing reads as missing.
Dialog {
    id: root
    objectName: "shortcutReference"

    title: qsTr("Keyboard shortcuts")
    modal: true
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 80 : 560)
    height: Math.min(620, parent ? parent.height - 80 : 620)
    standardButtons: Dialog.Close

    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: 8
    }
    header: Label {
        text: root.title
        font.pixelSize: 16
        font.bold: true
        color: Theme.textPrimary
        padding: 16
    }

    function rowsFor(category) {
        var all = ShortcutCatalog.model()
        var out = []
        for (var i = 0; i < all.length; i++)
            if (all[i].category === category)
                out.push(all[i])
        return out
    }

    contentItem: ScrollView {
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Column {
            width: root.availableWidth
            spacing: 14

            Repeater {
                model: ShortcutCatalog.categories()

                Column {
                    // The category name, read by the heading Label and by the
                    // inner Repeater's model, both separate scopes.
                    id: category
                    required property string modelData
                    width: parent.width
                    spacing: 2

                    Label {
                        text: category.modelData
                        font.pixelSize: 13
                        font.bold: true
                        color: Theme.accent
                        bottomPadding: 4
                    }

                    Repeater {
                        model: root.rowsFor(category.modelData)

                        RowLayout {
                            id: shortcutRow
                            required property var modelData
                            width: parent.width
                            spacing: 12

                            Label {
                                text: shortcutRow.modelData.action
                                color: Theme.textPrimary
                                font.pixelSize: 13
                                Layout.preferredWidth: 180
                            }
                            // The chord as key caps, or a dash for a deviation.
                            Rectangle {
                                visible: shortcutRow.modelData.chord !== ""
                                radius: 4
                                color: Theme.chipBackground
                                border.color: Theme.border
                                border.width: 1
                                implicitWidth: chordLabel.implicitWidth + 14
                                implicitHeight: chordLabel.implicitHeight + 6
                                Label {
                                    id: chordLabel
                                    anchors.centerIn: parent
                                    text: shortcutRow.modelData.displayChord
                                    font.pixelSize: 12
                                    font.family: "monospace"
                                    color: Theme.textPrimary
                                }
                            }
                            Label {
                                visible: shortcutRow.modelData.chord === ""
                                text: "—"
                                color: Theme.textMuted
                                font.pixelSize: 13
                            }
                            Label {
                                visible: shortcutRow.modelData.note !== ""
                                text: shortcutRow.modelData.note
                                color: Theme.textMuted
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Item { Layout.fillWidth: shortcutRow.modelData.note === "" }
                        }
                    }
                }
            }
        }
    }
}
