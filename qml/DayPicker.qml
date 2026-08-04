// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The month grid's delegate is its own scope, so the cell is named and its
// model role declared rather than injected.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// A calendar for choosing one day, opened where a date is being set — a task
// board's due date today. It speaks ISO days rather than JS Dates: a date
// crossing that boundary shifts by the timezone offset, so the day picked in
// one place is not always the day stored in another, whereas `yyyy-MM-dd`
// names the intended calendar day and sorts as a string.
//
// Its own controls carry their own colours, as the board's do, because the
// Fusion style this application sets follows the desktop palette rather than
// the note theme.
//
// The date-range picker in global search (DateRangePicker.qml) is a different
// thing: two endpoints, applied live to a search as they are chosen. This one
// answers with a day and closes.
//
// It already took focus; what it lacked was anything to say. The popup names
// itself, each day is announced as its full date rather than a bare number,
// and the four glyph buttons carry names instead of "‹" and "›"
// (accessibility.md Finding 2).
Popup {
    id: picker
    objectName: "dayPicker"

    // The day the calendar opens on and marks, as `yyyy-MM-dd`. Empty opens on
    // the current month with nothing marked.
    property string selectedDay: ""
    // Whether to offer clearing the day; false where a date is required.
    property bool clearable: true

    signal dayPicked(string day)
    signal dayCleared()

    width: Interface.px(252)
    padding: Interface.px(10)
    modal: true
    dim: false
    focus: true
    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: Interface.px(6)
    }

    property date visibleMonth: new Date()

    function openAt(day) {
        picker.selectedDay = day
        var parts = /^(\d{4})-(\d{2})-(\d{2})$/.exec(day)
        picker.visibleMonth = parts
            ? new Date(Number(parts[1]), Number(parts[2]) - 1, Number(parts[3]))
            : new Date()
        picker.open()
    }

    function dayKey(year, month, day) {   // month 0-based, as MonthGrid gives it
        function pad(n) { return (n < 10 ? "0" : "") + n }
        return year + "-" + pad(month + 1) + "-" + pad(day)
    }
    readonly property string todayKey: Qt.formatDate(new Date(), "yyyy-MM-dd")

    // A flat glyph button and a flat labelled one, transparent until the
    // pointer reaches them.
    component NavButton: Rectangle {
        id: navButton
        property string glyph: ""
        // What the glyph means. The glyph itself is read out as whatever the
        // screen reader's dictionary calls that character.
        property string label: ""
        signal activated()
        width: Interface.px(22)
        height: Interface.px(22)
        radius: Interface.px(4)
        color: navHover.hovered ? Theme.hoverTint : "transparent"
        border.width: navButton.activeFocus ? 2 : 0
        border.color: Theme.focusRing
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: navButton.label
        Accessible.onPressAction: navButton.activated()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
                navButton.activated()
                event.accepted = true
            }
        }
        Text {
            anchors.centerIn: parent
            text: navButton.glyph
            font.pixelSize: Interface.px(14)
            color: Theme.textMuted
        }
        HoverHandler { id: navHover; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: navButton.activated()
        }
    }
    component FlatButton: Rectangle {
        id: flatButton
        property string label: ""
        signal activated()
        implicitWidth: flatButtonLabel.implicitWidth + 18
        implicitHeight: Interface.px(22)
        radius: Interface.px(4)
        color: flatHover.hovered ? Theme.hoverTint : "transparent"
        border.width: flatButton.activeFocus ? 2 : 1
        border.color: flatButton.activeFocus ? Theme.focusRing : Theme.borderStrong
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: flatButton.label
        Accessible.onPressAction: flatButton.activated()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
                flatButton.activated()
                event.accepted = true
            }
        }
        Text {
            id: flatButtonLabel
            anchors.centerIn: parent
            text: flatButton.label
            color: flatHover.hovered ? Theme.textPrimary : Theme.textMuted
            font.pixelSize: Interface.small
        }
        HoverHandler { id: flatHover; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: flatButton.activated()
        }
    }

    contentItem: ColumnLayout {
        spacing: Interface.px(6)
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Pick a day")

        RowLayout {
            Layout.fillWidth: true
            NavButton {
                objectName: "dayPickerPrevMonth"
                glyph: "‹"
                label: qsTr("Previous month")
                onActivated: picker.visibleMonth = new Date(
                    picker.visibleMonth.getFullYear(),
                    picker.visibleMonth.getMonth() - 1, 1)
            }
            Text {
                objectName: "dayPickerMonthLabel"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: Qt.formatDate(picker.visibleMonth, "MMMM yyyy")
                font.pixelSize: Interface.body
                font.bold: true
                color: Theme.textPrimary
            }
            NavButton {
                objectName: "dayPickerNextMonth"
                glyph: "›"
                label: qsTr("Next month")
                onActivated: picker.visibleMonth = new Date(
                    picker.visibleMonth.getFullYear(),
                    picker.visibleMonth.getMonth() + 1, 1)
            }
        }

        DayOfWeekRow {
            Layout.fillWidth: true
            delegate: Text {
                required property var model
                text: model.shortName
                font.pixelSize: Interface.caption
                color: Theme.textFaint
                horizontalAlignment: Text.AlignHCenter
            }
        }

        MonthGrid {
            id: grid
            objectName: "dayPickerGrid"
            Layout.fillWidth: true
            month: picker.visibleMonth.getMonth()
            year: picker.visibleMonth.getFullYear()

            delegate: Rectangle {
                id: dayCell
                required property var model
                readonly property string cellKey:
                    picker.dayKey(dayCell.model.year, dayCell.model.month,
                                  dayCell.model.day)
                readonly property bool isSelected:
                    dayCell.cellKey === picker.selectedDay
                implicitWidth: Interface.px(30)
                implicitHeight: Interface.px(24)
                radius: Interface.px(4)
                color: dayCell.isSelected ? Theme.accent
                     : dayHover.hovered ? Theme.hoverTint : "transparent"
                // Today is outlined rather than filled, so it still reads as
                // today when another day is the one chosen.
                border.width: dayCell.cellKey === picker.todayKey ? 1 : 0
                border.color: Theme.accent
                opacity: dayCell.model.month === grid.month ? 1 : 0.35

                Accessible.role: Accessible.Button
                Accessible.name: {
                    var name = Qt.formatDate(new Date(dayCell.model.year,
                                                      dayCell.model.month,
                                                      dayCell.model.day),
                                             "dddd d MMMM yyyy")
                    if (dayCell.cellKey === picker.todayKey)
                        name += qsTr(", today")
                    return name
                }
                Accessible.checkable: true
                Accessible.checked: dayCell.isSelected
                Accessible.onPressAction: dayCell.choose()

                function choose() {
                    picker.selectedDay = dayCell.cellKey
                    picker.dayPicked(dayCell.cellKey)
                    picker.close()
                }

                Text {
                    anchors.centerIn: parent
                    text: dayCell.model.day
                    font.pixelSize: Interface.small
                    color: dayCell.isSelected ? Theme.onAccent : Theme.textPrimary
                }
                HoverHandler { id: dayHover; cursorShape: Qt.PointingHandCursor }
                // Every control here takes its press rather than watching it:
                // Qt offers a press to the handlers of every item under it
                // before any item accepts, so a calendar over a board would
                // otherwise put the click through to the card behind it too.
                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: dayCell.choose()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Interface.px(6)
            FlatButton {
                objectName: "dayPickerToday"
                label: qsTr("Today")
                onActivated: {
                    picker.selectedDay = picker.todayKey
                    picker.dayPicked(picker.todayKey)
                    picker.close()
                }
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                objectName: "dayPickerClear"
                visible: picker.clearable && picker.selectedDay !== ""
                label: qsTr("Clear")
                onActivated: {
                    picker.selectedDay = ""
                    picker.dayCleared()
                    picker.close()
                }
            }
        }
    }
}
