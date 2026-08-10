// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// Optional roots and views chrome. Root entries are the workspace roots the
// reader has kept in this rail, independent of which one is currently
// composed. Switching still travels through AppActions, so lock and unsaved
// settlement remain owned by the root-switch path.
Row {
    id: rails
    objectName: "navigationRails"
    property var appWindow
    property var roots: []
    // Removing the active root is a two-phase operation. A dirty document can
    // keep the switch in a confirmation dialog, so do not forget the root
    // until rootChanged proves the switch actually completed.
    property string pendingRemoval: ""
    readonly property int railWidth: Interface.px(36)
    width: railWidth * 2
    spacing: 0

    function refreshRoots() {
        var saved = AppSettings.value("workspace.roots", [])
        var next = []
        for (var i = 0; i < saved.length; ++i) {
            if (saved[i] !== "" && next.indexOf(saved[i]) < 0)
                next.push(saved[i])
        }
        if (NoteCollection.isOpen && next.indexOf(NoteCollection.rootPath) < 0)
            next.push(NoteCollection.rootPath)
        roots = next
        AppSettings.setValue("workspace.roots", next)
    }
    function removeRoot(path) {
        if (path === NoteCollection.rootPath) {
            if (roots.length < 2) {
                AppActions.requestCloseVault(path)
                return
            }
            var openRoots = AppSettings.value("session.openVaults", [])
            var nextPath = ""
            for (var i = 0; i < roots.length; ++i) {
                if (roots[i] !== path && openRoots.indexOf(roots[i]) < 0) {
                    nextPath = roots[i]
                    break
                }
            }
            // Every alternative already belongs to another window. Closing
            // this one is the only operation that preserves one writer per
            // vault; switching would merely raise the other window.
            if (nextPath === "") {
                AppActions.requestCloseVault(path)
                return
            }
            pendingRemoval = path
            AppActions.requestOpenVault(nextPath)
            return
        }
        AppActions.requestCloseVault(path)
        var next = roots.filter(function(item) { return item !== path })
        roots = next
        AppSettings.setValue("workspace.roots", next)
    }
    function focusPane() {
        if (rootList.currentIndex < 0 && rootList.count > 0)
            rootList.currentIndex = 0
        rootList.forceActiveFocus(Qt.TabFocusReason)
    }

    Component.onCompleted: refreshRoots()
    Connections {
        target: NoteCollection
        function onRootChanged() {
            if (rails.pendingRemoval !== ""
                    && NoteCollection.rootPath !== rails.pendingRemoval) {
                var removed = rails.pendingRemoval
                rails.pendingRemoval = ""
                var kept = rails.roots.filter(function(item) {
                    return item !== removed
                })
                AppSettings.setValue("workspace.roots", kept)
            }
            rails.refreshRoots()
        }
    }
    Connections {
        target: AppSettings
        function onValueChanged(key) {
            if (key === "workspace.roots")
                rails.refreshRoots()
        }
    }

    Rectangle {
        width: rails.railWidth
        height: rails.height
        color: Theme.windowBackground
        border.color: Theme.border

        ColumnLayout {
            anchors.fill: parent
            spacing: Interface.px(2)
            ListView {
                id: rootList
                objectName: "rootRail"
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: rails.roots
                clip: true
                activeFocusOnTab: true
                keyNavigationEnabled: true
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Open roots")
                Keys.onReturnPressed: if (currentIndex >= 0)
                    AppActions.requestOpenVault(rails.roots[currentIndex])
                Keys.onEnterPressed: if (currentIndex >= 0)
                    AppActions.requestOpenVault(rails.roots[currentIndex])

                delegate: Rectangle {
                    id: rootButton
                    required property string modelData
                    required property int index
                    width: rootList.width
                    height: rails.railWidth
                    readonly property bool current:
                        modelData === NoteCollection.rootPath
                    readonly property var mark: {
                        var revision = Extensions.rootStatusRevision
                        return Extensions.rootStatus(modelData)
                    }
                    color: current ? Theme.selectionTint
                                   : rootHover.hovered ? Theme.hoverTint
                                                       : "transparent"
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.split("/").pop()
                    Accessible.description: mark.tooltip || modelData
                    Accessible.onPressAction: AppActions.requestOpenVault(modelData)
                    HoverHandler { id: rootHover }
                    ToolTip.visible: rootHover.hovered
                    ToolTip.text: (rootButton.mark.tooltip || "") !== ""
                        ? rootButton.modelData + "\n" + rootButton.mark.tooltip
                        : rootButton.modelData
                    Label {
                        anchors.centerIn: parent
                        text: rootButton.modelData.split("/").pop().substring(0, 2)
                        font.bold: rootButton.current
                        font.pixelSize: Interface.small
                        color: Theme.textPrimary
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: Interface.px(4)
                        width: Interface.px(7)
                        height: width
                        radius: width / 2
                        visible: rootButton.mark.color !== undefined
                                 && rootButton.mark.color !== ""
                        color: rootButton.mark.color || "transparent"
                        border.color: Theme.border
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: {
                            rootList.currentIndex = rootButton.index
                            AppActions.requestOpenVault(rootButton.modelData)
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.MiddleButton
                        onTapped: rails.removeRoot(rootButton.modelData)
                    }
                }
            }
            ToolButton {
                Layout.fillWidth: true
                text: "+"
                Accessible.name: qsTr("Open another root")
                onClicked: rails.appWindow.openFolderFromDialog(false)
            }
            ToolButton {
                Layout.fillWidth: true
                text: "−"
                enabled: rootList.currentIndex >= 0
                Accessible.name: qsTr("Close selected root")
                onClicked: rails.removeRoot(rails.roots[rootList.currentIndex])
            }
        }
    }

    Rectangle {
        width: rails.railWidth
        height: rails.height
        color: Theme.panelBackground
        border.color: Theme.border

        ListView {
            id: viewList
            objectName: "sidebarViewRail"
            anchors.fill: parent
            activeFocusOnTab: true
            keyNavigationEnabled: true
            model: {
                var result = [
                    { id: "files", title: qsTr("Files"), glyph: "▤" },
                    { id: "notes", title: qsTr("Notes"), glyph: "N" },
                    { id: "folders", title: qsTr("Folders"), glyph: "▰" },
                    { id: "tags", title: qsTr("Tags"), glyph: "#" },
                    { id: "search", title: qsTr("Search"), glyph: "⌕" }
                ]
                var extras = Extensions.sidebarViews()
                for (var i = 0; i < extras.length; ++i) {
                    result.push({id: extras[i].id, title: extras[i].title,
                                 glyph: extras[i].glyph || "◇"})
                }
                return result
            }
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Sidebar views")
            Keys.onReturnPressed: if (currentIndex >= 0)
                rails.appWindow.sidebarView = model[currentIndex].id
            Keys.onEnterPressed: if (currentIndex >= 0)
                rails.appWindow.sidebarView = model[currentIndex].id

            delegate: Rectangle {
                id: viewButton
                required property var modelData
                required property int index
                readonly property string viewId: modelData.id
                width: viewList.width
                height: rails.railWidth
                color: rails.appWindow.sidebarView === viewId
                    ? Theme.selectionTint
                    : viewHover.hovered ? Theme.hoverTint : "transparent"
                Accessible.role: Accessible.Button
                Accessible.name: modelData.title
                Accessible.selected: rails.appWindow.sidebarView === viewId
                Accessible.onPressAction: rails.appWindow.sidebarView = viewId
                HoverHandler { id: viewHover }
                ToolTip.visible: viewHover.hovered
                ToolTip.text: viewButton.modelData.title
                Label {
                    anchors.centerIn: parent
                    text: viewButton.modelData.glyph
                    color: Theme.textPrimary
                    font.pixelSize: Interface.body
                }
                TapHandler {
                    onTapped: {
                        viewList.currentIndex = viewButton.index
                        rails.appWindow.sidebarView = viewButton.viewId
                    }
                }
            }
        }
    }
}
