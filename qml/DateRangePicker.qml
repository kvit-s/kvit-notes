// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The MonthGrid delegate's own bindings and its nested handlers are
// separate scopes, so the cell is named and its model role declared
// rather than injected.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Kvit 1.0

// The custom date-range picker for global search (features.md §8.4).
// One month view with paging; the first day clicked starts the range,
// the second completes it (swapped if earlier), and each pick applies
// live through CollectionSearch.customFrom/customTo with the "custom"
// preset.
//
// Keyboard and screen-reader behaviour follows ColorPicker.qml
// (accessibility.md Finding 2): the popup takes focus and names itself, each
// day is a named choice rather than a bare number, and closing hands the
// keyboard back to whatever opened it.
Popup {
    id: picker
    objectName: "dateRangePicker"

    width: Interface.px(252)
    padding: Interface.px(10)
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // What had the keyboard before this opened, so closing can hand it back.
    property Item openedFrom: null
    onAboutToShow: {
        const w = picker.parent ? picker.parent.Window.window : null
        picker.openedFrom = w ? w.activeFocusItem : null
    }
    onClosed: {
        if (picker.openedFrom)
            picker.openedFrom.forceActiveFocus()
        picker.openedFrom = null
    }
    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: Interface.px(6)
    }

    property date visibleMonth: new Date()
    // Selection stage: picking the start, or completing the range.
    property bool pickingEnd: false

    function openFor() {
        var from = CollectionSearch.customFrom
        visibleMonth = (from && !isNaN(from.getTime())) ? from : new Date()
        pickingEnd = false
        open()
    }

    function dayValid(d) { return d && !isNaN(d.getTime()) }

    // Comparisons run over ISO day strings, not Date objects: the
    // QDate <-> JS Date conversion shifts by timezone offset, but
    // Qt.formatDate always names the intended calendar day. String
    // order on yyyy-MM-dd is date order.
    readonly property string fromKey:
        dayValid(CollectionSearch.customFrom)
            ? Qt.formatDate(CollectionSearch.customFrom, "yyyy-MM-dd") : ""
    readonly property string toKey:
        dayValid(CollectionSearch.customTo)
            ? Qt.formatDate(CollectionSearch.customTo, "yyyy-MM-dd") : ""
    function dayKey(year, month, day) {  // month 0-based (MonthGrid)
        function pad(n) { return (n < 10 ? "0" : "") + n }
        return year + "-" + pad(month + 1) + "-" + pad(day)
    }

    function pickDay(day) {
        CollectionSearch.datePreset = "custom"
        if (!pickingEnd) {
            CollectionSearch.customFrom = day
            CollectionSearch.customTo = day
            pickingEnd = true
        } else {
            var from = CollectionSearch.customFrom
            if (dayValid(from) && day < from) {
                CollectionSearch.customTo = from
                CollectionSearch.customFrom = day
            } else {
                CollectionSearch.customTo = day
            }
            pickingEnd = false
        }
    }

    contentItem: ColumnLayout {
        spacing: Interface.px(6)
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Custom date range")

        RowLayout {
            Layout.fillWidth: true
            ToolButton {
                objectName: "pickerPrevMonth"
                text: "‹"
                Accessible.name: qsTr("Previous month")
                focusPolicy: Qt.TabFocus
                implicitWidth: 24; implicitHeight: 24
                onClicked: picker.visibleMonth = new Date(
                    picker.visibleMonth.getFullYear(),
                    picker.visibleMonth.getMonth() - 1, 1)
            }
            Label {
                objectName: "pickerMonthLabel"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: Qt.formatDate(picker.visibleMonth, "MMMM yyyy")
                font.pixelSize: Interface.body
                font.bold: true
            }
            ToolButton {
                objectName: "pickerNextMonth"
                text: "›"
                Accessible.name: qsTr("Next month")
                focusPolicy: Qt.TabFocus
                implicitWidth: 24; implicitHeight: 24
                onClicked: picker.visibleMonth = new Date(
                    picker.visibleMonth.getFullYear(),
                    picker.visibleMonth.getMonth() + 1, 1)
            }
        }

        DayOfWeekRow {
            Layout.fillWidth: true
            font.pixelSize: Interface.caption
            delegate: Label {
                required property var model
                text: model.shortName
                font.pixelSize: Interface.caption
                color: Theme.textFaint
                horizontalAlignment: Text.AlignHCenter
            }
        }

        MonthGrid {
            id: grid
            objectName: "pickerMonthGrid"
            Layout.fillWidth: true
            month: picker.visibleMonth.getMonth()
            year: picker.visibleMonth.getFullYear()
            font.pixelSize: Interface.small

            delegate: Rectangle {
                id: dayCell
                required property var model
                readonly property string cellKey:
                    picker.dayKey(dayCell.model.year, dayCell.model.month, dayCell.model.day)
                readonly property bool isEndpoint:
                    dayCell.cellKey === picker.fromKey || dayCell.cellKey === picker.toKey
                readonly property bool inRange:
                    picker.fromKey !== "" && picker.toKey !== ""
                    && dayCell.cellKey >= picker.fromKey && dayCell.cellKey <= picker.toKey
                implicitWidth: Interface.px(30)
                implicitHeight: Interface.px(24)
                radius: Interface.px(4)
                color: dayCell.isEndpoint ? Theme.accent
                     : dayCell.inRange ? Theme.selectionTint
                     : dayHover.hovered ? Theme.hoverTint : "transparent"
                opacity: dayCell.model.month === grid.month ? 1 : 0.35

                // The full date, not the bare day number a sighted reader
                // gets from the column it sits under, plus whether this day
                // is in the range so far.
                Accessible.role: Accessible.Button
                Accessible.name: {
                    var name = Qt.formatDate(new Date(dayCell.model.year,
                                                      dayCell.model.month,
                                                      dayCell.model.day),
                                             "dddd d MMMM yyyy")
                    if (dayCell.isEndpoint)
                        return name + qsTr(", range endpoint")
                    if (dayCell.inRange)
                        return name + qsTr(", in range")
                    return name
                }
                Accessible.onPressAction: dayCell.pick()

                function pick() {
                    picker.pickDay(new Date(dayCell.model.year,
                                            dayCell.model.month,
                                            dayCell.model.day))
                }

                Label {
                    anchors.centerIn: parent
                    text: dayCell.model.day
                    font.pixelSize: Interface.small
                    color: dayCell.isEndpoint ? Theme.onAccent
                                              : Theme.textPrimary
                }
                HoverHandler { id: dayHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: dayCell.pick() }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                objectName: "pickerRangeLabel"
                Layout.fillWidth: true
                font.pixelSize: Interface.caption
                color: Theme.textMuted
                elide: Text.ElideRight
                text: {
                    var from = CollectionSearch.customFrom
                    var to = CollectionSearch.customTo
                    if (!picker.dayValid(from))
                        return qsTr("Pick a start day")
                    var fromText = Qt.formatDate(from, "yyyy-MM-dd")
                    if (!picker.dayValid(to))
                        return fromText + " – …"
                    return fromText + " – "
                        + Qt.formatDate(to, "yyyy-MM-dd")
                }
            }
            ToolButton {
                objectName: "pickerClearButton"
                text: qsTr("Clear")
                Accessible.name: qsTr("Clear the date range")
                font.pixelSize: Interface.caption
                focusPolicy: Qt.TabFocus
                implicitHeight: Interface.px(22)
                onClicked: {
                    CollectionSearch.customFrom = new Date(NaN)
                    CollectionSearch.customTo = new Date(NaN)
                    CollectionSearch.datePreset = "any"
                    picker.pickingEnd = false
                    picker.close()
                }
            }
        }
    }
}
