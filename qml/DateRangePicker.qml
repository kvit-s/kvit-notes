// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// The custom date-range picker for global search (features.md §8.4).
// One month view with paging; the first day clicked starts the range,
// the second completes it (swapped if earlier), and each pick applies
// live through CollectionSearch.customFrom/customTo with the "custom"
// preset.
Popup {
    id: picker
    objectName: "dateRangePicker"

    width: 252
    padding: 10
    background: Rectangle {
        color: theme.popupBackground
        border.color: theme.borderStrong
        border.width: 1
        radius: 6
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
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            ToolButton {
                objectName: "pickerPrevMonth"
                text: "‹"
                focusPolicy: Qt.NoFocus
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
                font.pixelSize: 12
                font.bold: true
            }
            ToolButton {
                objectName: "pickerNextMonth"
                text: "›"
                focusPolicy: Qt.NoFocus
                implicitWidth: 24; implicitHeight: 24
                onClicked: picker.visibleMonth = new Date(
                    picker.visibleMonth.getFullYear(),
                    picker.visibleMonth.getMonth() + 1, 1)
            }
        }

        DayOfWeekRow {
            Layout.fillWidth: true
            font.pixelSize: 10
            delegate: Label {
                required property var model
                text: model.shortName
                font.pixelSize: 10
                color: theme.textFaint
                horizontalAlignment: Text.AlignHCenter
            }
        }

        MonthGrid {
            id: grid
            objectName: "pickerMonthGrid"
            Layout.fillWidth: true
            month: picker.visibleMonth.getMonth()
            year: picker.visibleMonth.getFullYear()
            font.pixelSize: 11

            delegate: Rectangle {
                required property var model
                readonly property string cellKey:
                    picker.dayKey(model.year, model.month, model.day)
                readonly property bool isEndpoint:
                    cellKey === picker.fromKey || cellKey === picker.toKey
                readonly property bool inRange:
                    picker.fromKey !== "" && picker.toKey !== ""
                    && cellKey >= picker.fromKey && cellKey <= picker.toKey
                implicitWidth: 30
                implicitHeight: 24
                radius: 4
                color: isEndpoint ? theme.accent
                     : inRange ? theme.selectionTint
                     : dayHover.hovered ? theme.hoverTint : "transparent"
                opacity: model.month === grid.month ? 1 : 0.35

                Label {
                    anchors.centerIn: parent
                    text: model.day
                    font.pixelSize: 11
                    color: parent.isEndpoint ? theme.onAccent
                                             : theme.textPrimary
                }
                HoverHandler { id: dayHover }
                TapHandler {
                    onTapped: picker.pickDay(new Date(parent.model.year,
                                                      parent.model.month,
                                                      parent.model.day))
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                objectName: "pickerRangeLabel"
                Layout.fillWidth: true
                font.pixelSize: 10
                color: theme.textMuted
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
                font.pixelSize: 10
                focusPolicy: Qt.NoFocus
                implicitHeight: 22
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
