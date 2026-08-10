// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

Rectangle {
    id: dock
    objectName: "bottomDock"
    property var appWindow
    property var tabs: Extensions.bottomDockTabs()
    readonly property bool hasTabs: tabs && tabs.length > 0
    property int currentIndex: 0
    readonly property var activeTab:
        hasTabs && currentIndex >= 0 && currentIndex < tabs.length
        ? tabs[currentIndex] : null
    color: Theme.panelBackground
    border.color: Theme.border
    visible: hasTabs && appWindow && !appWindow.focusMode
    height: !visible ? 0 : appWindow.bottomDockCollapsed
            ? Interface.px(34) : appWindow.bottomDockHeight

    function restoreActiveTab() {
        if (!hasTabs) {
            currentIndex = -1
            return
        }
        var wanted = AppSettings.value("dock.activeTab", "")
        currentIndex = 0
        for (var i = 0; i < tabs.length; ++i) {
            if (tabs[i].id === wanted) {
                currentIndex = i
                break
            }
        }
    }
    function focusPane() {
        tabBar.forceActiveFocus(Qt.TabFocusReason)
    }

    Component.onCompleted: restoreActiveTab()
    onTabsChanged: restoreActiveTab()
    onCurrentIndexChanged: {
        // activeTab is a dependent binding and can still hold the previous
        // row while this change handler runs. Index the source list directly
        // so a click persists the tab that was actually selected.
        if (hasTabs && currentIndex >= 0 && currentIndex < tabs.length)
            AppSettings.setValue("dock.activeTab", tabs[currentIndex].id)
    }

    Shortcut {
        sequence: "Ctrl+J"
        enabled: dock.hasTabs
        onActivated: dock.appWindow.bottomDockCollapsed =
            !dock.appWindow.bottomDockCollapsed
    }

    MouseArea {
        id: resizeHandle
        objectName: "bottomDockResizeHandle"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Interface.px(6)
        cursorShape: Qt.SizeVerCursor
        visible: !dock.appWindow.bottomDockCollapsed
        property real pressY: 0
        property real pressHeight: 0
        onPressed: function(mouse) {
            pressY = mapToItem(null, mouse.x, mouse.y).y
            pressHeight = dock.appWindow.bottomDockHeight
        }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            var now = mapToItem(null, mouse.x, mouse.y).y
            dock.appWindow.bottomDockHeight = Math.max(
                Interface.px(110), Math.min(dock.appWindow.height * 0.65,
                                            pressHeight + pressY - now))
        }
    }

    RowLayout {
        id: header
        anchors.top: parent.top
        anchors.topMargin: Interface.px(4)
        anchors.left: parent.left
        anchors.right: parent.right
        height: Interface.px(30)
        spacing: 0

        TabBar {
            id: tabBar
            objectName: "bottomDockTabBar"
            Layout.fillWidth: true
            currentIndex: dock.currentIndex
            onCurrentIndexChanged: dock.currentIndex = currentIndex
            Repeater {
                model: dock.tabs
                TabButton {
                    required property var modelData
                    text: modelData.title
                    Accessible.name: qsTr("Bottom dock tab: %1").arg(text)
                }
            }
        }
        ToolButton {
            objectName: "bottomDockCollapseButton"
            text: dock.appWindow.bottomDockCollapsed ? "⌃" : "⌄"
            Accessible.name: dock.appWindow.bottomDockCollapsed
                ? qsTr("Expand bottom dock") : qsTr("Collapse bottom dock")
            ToolTip.visible: hovered || visualFocus
            ToolTip.text: Accessible.name + qsTr(" (Ctrl+J)")
            onClicked: dock.appWindow.bottomDockCollapsed =
                !dock.appWindow.bottomDockCollapsed
        }
    }

    Loader {
        id: tabContent
        objectName: "bottomDockContent"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        active: dock.activeTab !== null && !dock.appWindow.bottomDockCollapsed
        source: active ? dock.activeTab.source : ""
    }
}
