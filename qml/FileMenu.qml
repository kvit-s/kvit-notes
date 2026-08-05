// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The Repeaters below are their own component scopes and read this file's
// root by id. Binding resolves that.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kvit 1.0

// The File menu: everything that acts on documents rather than on what the
// window shows — opening a vault or a loose file, the recent list, creating a
// note from a template, capturing one, moving notes in and out of the
// collection, and the application-wide settings.
//
// It is a file of its own because it has two homes and must be one definition.
// On Windows and Linux it hangs off the toolbar's File button; on macOS the
// same commands belong in the system menu bar at the top of the screen, where
// main.qml instantiates this component instead (see "The menu bar on macOS"
// there). Only one of the two exists in a running window.
//
// The commands act on `appWindow`, which is main.qml's root; everything else
// they need is a singleton.
//
// Each command's label marks its access key with `&`, and MenuText.label()
// takes the markers back out on macOS, which has no such convention. The
// recent vaults and the template names go through MenuText.plain() instead:
// they are names off the disk, so an `&` in one is part of the name.
// See src/platform/menuaccesskeys.h.
Menu {
    id: fileMenu
    objectName: "toolbarFileMenu"
    title: MenuText.label(qsTr("&File"))

    // The editor window these commands act on. Untyped because KvitShell
    // declares only what block delegates read, not the window's own API.
    property var appWindow

    // What a submenu's own row in this menu is built from: Qt creates that
    // row itself, from the delegate, so it is the one entry the declarations
    // below cannot cover. See DiscoverableMenuItem.qml.
    delegate: DiscoverableMenuItem {}

    // The template list below is built from what is on disk, so the built-ins
    // are seeded before the menu reads it. This is the moment for it wherever
    // the menu lives: the toolbar's button and the macOS menu bar both raise
    // this signal before the menu appears.
    onAboutToShow: {
        if (fileMenu.appWindow && fileMenu.appWindow.collectionOpen)
            NoteTemplates.seedBuiltinsIfEmpty()
    }

    DiscoverableMenuItem {
        objectName: "fileMenuOpenFile"
        text: MenuText.label(qsTr("&Open File…"))
        // Routed by window mode: a vault window opens the file in
        // its own single-file window; single-file mode replaces the
        // current document in place.
        onTriggered: fileMenu.appWindow.openFileFromDialog()
    }
    DiscoverableMenuItem {
        objectName: "fileMenuOpenFolder"
        text: MenuText.label(qsTr("Open &Folder…"))
        // Switches this window to the chosen vault (raising an
        // existing window if that vault is already open).
        onTriggered: fileMenu.appWindow.openFolderFromDialog(false)
    }
    DiscoverableMenuItem {
        objectName: "fileMenuOpenFolderNewWindow"
        text: MenuText.label(qsTr("Open Folder in New &Window…"))
        onTriggered: fileMenu.appWindow.openFolderFromDialog(true)
    }
    MenuSeparator {}

    DiscoverableMenuItem {
        objectName: "fileMenuSave"
        text: MenuText.label(qsTr("&Save"))
        enabled: DocumentManager
                 && (!DocumentManager.hasFile
                     || DocumentManager.isDirty)
        onTriggered: fileMenu.appWindow.saveCurrentDocument(false)
    }
    DiscoverableMenuItem {
        objectName: "fileMenuSaveAs"
        text: MenuText.label(qsTr("Save &As…"))
        onTriggered: fileMenu.appWindow.saveCurrentDocument(true)
    }
    MenuSeparator {}

    Menu {
        id: recentVaultsMenu
        objectName: "fileMenuRecent"
        title: MenuText.label(qsTr("Open &Recent"))
        enabled: recentVaultsRepeater.count > 0
        Repeater {
            id: recentVaultsRepeater
            model: {
                var r = AppSettings.revision  // reactive dependency
                return AppSettings.value("session.recentVaults", [])
            }
            DiscoverableMenuItem {
                required property string modelData
                text: MenuText.plain(modelData)
                onTriggered: AppActions.requestOpenVault(modelData)
            }
        }
    }

    // features.md §18 templates, and quick capture: both make a
    // new note, and both need a collection, since templates live
    // under .kvit and a captured note has to land somewhere.
    // Disabled rather than hidden without one, so the commands can
    // still be found in single-file mode.
    MenuSeparator {}
    Menu {
        id: newFromTemplateMenu
        objectName: "newFromTemplateMenu"
        title: MenuText.label(qsTr("&New from template"))
        enabled: fileMenu.appWindow && fileMenu.appWindow.collectionOpen
        Repeater {
            model: {
                var r = NoteTemplates.revision  // dependency
                return NoteTemplates.templateNames()
            }
            DiscoverableMenuItem {
                required property string modelData
                text: MenuText.plain(modelData)
                onTriggered:
                    fileMenu.appWindow.createFromTemplate(modelData)
            }
        }
    }
    DiscoverableMenuItem {
        objectName: "manageTemplatesItem"
        text: MenuText.label(qsTr("&Manage templates…"))
        enabled: fileMenu.appWindow && fileMenu.appWindow.collectionOpen
        onTriggered: fileMenu.appWindow.templateDialog.openManage()
    }
    DiscoverableMenuItem {
        objectName: "fileMenuQuickCapture"
        text: MenuText.label(qsTr("&Quick capture note… (Ctrl+Alt+N)"))
        enabled: fileMenu.appWindow && fileMenu.appWindow.collectionOpen
        onTriggered: fileMenu.appWindow.openQuickCapture()
    }

    // Notes in and out of the collection (features.md §12.5–12.6).
    MenuSeparator {}
    DiscoverableMenuItem {
        objectName: "fileMenuImport"
        text: MenuText.label(qsTr("&Import…"))
        enabled: fileMenu.appWindow && fileMenu.appWindow.collectionOpen
        onTriggered: fileMenu.appWindow.importDialog.openDialog()
    }
    DiscoverableMenuItem {
        objectName: "fileMenuExport"
        text: MenuText.label(qsTr("&Export…"))
        onTriggered: fileMenu.appWindow.exportDialog.openDialog()
    }

    MenuSeparator {}
    DiscoverableMenuItem {
        objectName: "fileMenuSettings"
        text: MenuText.label(qsTr("Se&ttings…"))
        onTriggered: fileMenu.appWindow.openSettingsDialog()
    }
    DiscoverableMenuItem {
        objectName: "fileMenuShortcuts"
        text: MenuText.label(qsTr("&Keyboard shortcuts…"))
        onTriggered: fileMenu.appWindow.openShortcutReference()
    }
}
