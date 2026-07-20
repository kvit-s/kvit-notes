// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kvit 1.0

// The settings dialog: Appearance — the features.md §10.1 theme picker
// and the §10.3 accent/highlight color selection — and Typography — the
// six §10.2 settings. Every control binds live to the theme / Typography
// / AppSettings objects, so the document behind the dialog previews each
// change immediately; there is no Apply step.
Dialog {
    id: settingsDialog
    objectName: "settingsDialog"

    modal: true
    title: qsTr("Settings")
    standardButtons: Dialog.Close
    width: 540
    height: 480
    anchors.centerIn: parent
    padding: 0

    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: 6
    }

    contentItem: ColumnLayout {
        spacing: 0

        TabBar {
            id: pageBar
            Layout.fillWidth: true
            TabButton { text: qsTr("Appearance"); objectName: "appearanceTab" }
            TabButton { text: qsTr("Typography"); objectName: "typographyTab" }
            TabButton { text: qsTr("General"); objectName: "generalTab" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            currentIndex: pageBar.currentIndex

            // ---- Appearance (§10.1, §10.3) -------------------------
            ColumnLayout {
                spacing: 14

                Label {
                    text: qsTr("Theme")
                    font.bold: true
                    color: Theme.textSecondary
                }

                RowLayout {
                    spacing: 10
                    Repeater {
                        model: Theme.availableThemes
                        // A theme card: swatch above the name, the
                        // active one ringed in accent.
                        ColumnLayout {
                            required property string modelData
                            readonly property var preview:
                                Theme.themePreview(modelData)
                            spacing: 4

                            Rectangle {
                                objectName: "themeCard_" + parent.modelData
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 60
                                radius: 5
                                color: parent.preview.background
                                border.width:
                                    Theme.themeId === parent.modelData ? 2 : 1
                                border.color:
                                    Theme.themeId === parent.modelData
                                        ? Theme.accent : Theme.borderStrong

                                Rectangle { // panel stripe
                                    width: 26
                                    height: parent.height - 12
                                    x: 6; y: 6
                                    radius: 3
                                    color: parent.parent.preview.panel
                                }
                                Label {
                                    text: "Aa"
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 8
                                    font.pixelSize: 16
                                    color: parent.parent.preview.text
                                }
                                Rectangle { // accent dot
                                    width: 10; height: 10; radius: 5
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 8
                                    color: parent.parent.preview.accent
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: Theme.themeId
                                        = parent.parent.modelData
                                }
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: {
                                    var name = parent.modelData
                                    return name.charAt(0).toUpperCase()
                                        + name.slice(1)
                                }
                                font.pixelSize: 11
                                color: Theme.textMuted
                            }
                        }
                    }
                }

                // Accent color (§10.3): the theme's own accent, the
                // shared palette, or any hex value.
                Label {
                    text: qsTr("Accent color")
                    font.bold: true
                    color: Theme.textSecondary
                    Layout.topMargin: 6
                }
                RowLayout {
                    spacing: 6

                    Rectangle { // "theme default" swatch
                        objectName: "accentDefaultSwatch"
                        width: 24; height: 24; radius: 12
                        color: Theme.mutedGlyph
                        border.width: Theme.accentOverride === "" ? 2 : 1
                        border.color: Theme.accentOverride === ""
                            ? Theme.textPrimary : Theme.borderStrong
                        Label {
                            anchors.centerIn: parent
                            text: "✕"
                            font.pixelSize: 10
                            color: Theme.onAccent
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: Theme.accentOverride = ""
                        }
                        ToolTip.visible: accentDefaultHover.hovered
                        ToolTip.text: qsTr("Theme default")
                        HoverHandler { id: accentDefaultHover }
                    }
                    Repeater {
                        model: Theme.colorPalette
                        Rectangle {
                            required property string modelData
                            width: 24; height: 24; radius: 12
                            color: modelData
                            border.width:
                                Theme.accentOverride === modelData ? 2 : 1
                            border.color:
                                Theme.accentOverride === modelData
                                    ? Theme.textPrimary : Theme.borderStrong
                            MouseArea {
                                anchors.fill: parent
                                onClicked: Theme.accentOverride
                                    = parent.modelData
                            }
                        }
                    }
                    TextField {
                        objectName: "accentHexField"
                        Layout.preferredWidth: 84
                        implicitHeight: 26
                        font.pixelSize: 11
                        placeholderText: "#rrggbb"
                        text: Theme.accentOverride
                        onEditingFinished: Theme.accentOverride = text
                    }
                }

                // Highlight color (§10.3): the ==mark== background.
                Label {
                    text: qsTr("Highlight color")
                    font.bold: true
                    color: Theme.textSecondary
                    Layout.topMargin: 6
                }
                RowLayout {
                    spacing: 6

                    Rectangle {
                        objectName: "highlightDefaultSwatch"
                        width: 24; height: 24; radius: 12
                        color: Theme.mutedGlyph
                        border.width: Theme.highlightOverride === "" ? 2 : 1
                        border.color: Theme.highlightOverride === ""
                            ? Theme.textPrimary : Theme.borderStrong
                        Label {
                            anchors.centerIn: parent
                            text: "✕"
                            font.pixelSize: 10
                            color: Theme.onAccent
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: Theme.highlightOverride = ""
                        }
                    }
                    Repeater {
                        model: Theme.highlightPalette
                        Rectangle {
                            required property string modelData
                            width: 24; height: 24; radius: 12
                            color: modelData
                            border.width:
                                Theme.highlightOverride === modelData ? 2 : 1
                            border.color:
                                Theme.highlightOverride === modelData
                                    ? Theme.textPrimary : Theme.borderStrong
                            MouseArea {
                                anchors.fill: parent
                                onClicked: Theme.highlightOverride
                                    = parent.modelData
                            }
                        }
                    }
                    TextField {
                        objectName: "highlightHexField"
                        Layout.preferredWidth: 84
                        implicitHeight: 26
                        font.pixelSize: 11
                        placeholderText: "#rrggbb"
                        text: Theme.highlightOverride
                        onEditingFinished: Theme.highlightOverride = text
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Sample: normal, ==highlighted==, and "
                               + "[linked](x) text follow these choices.")
                    font.pixelSize: 11
                    color: Theme.textFaint
                    wrapMode: Text.Wrap
                }

                Item { Layout.fillHeight: true }
            }

            // ---- Typography (§10.2) --------------------------------
            GridLayout {
                columns: 2
                columnSpacing: 12
                rowSpacing: 10

                Label { text: qsTr("Editor font"); color: Theme.textSecondary }
                ComboBox {
                    id: familyCombo
                    objectName: "fontFamilyCombo"
                    Layout.fillWidth: true
                    model: [qsTr("System default")].concat(Qt.fontFamilies())
                    currentIndex: {
                        if (Typography.fontFamily === "")
                            return 0
                        var idx = Qt.fontFamilies()
                            .indexOf(Typography.fontFamily)
                        return idx < 0 ? 0 : idx + 1
                    }
                    onActivated: function(index) {
                        Typography.fontFamily =
                            index === 0 ? "" : model[index]
                    }
                }

                Label { text: qsTr("Font size"); color: Theme.textSecondary }
                RowLayout {
                    SpinBox {
                        objectName: "fontSizeSpin"
                        from: 10; to: 28
                        value: Typography.baseSize
                        onValueModified: Typography.baseSize = value
                    }
                    Label {
                        text: qsTr("px — headings scale with it")
                        font.pixelSize: 11
                        color: Theme.textFaint
                    }
                }

                Label { text: qsTr("Line height"); color: Theme.textSecondary }
                RowLayout {
                    Slider {
                        id: lineHeightSlider
                        objectName: "lineHeightSlider"
                        Layout.preferredWidth: 180
                        from: 1.0; to: 2.0; stepSize: 0.05
                        value: Typography.lineHeight
                        onMoved: Typography.lineHeight = value
                    }
                    Label {
                        text: "×" + Typography.lineHeight.toFixed(2)
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                }

                Label {
                    text: qsTr("Block spacing")
                    color: Theme.textSecondary
                }
                RowLayout {
                    SpinBox {
                        objectName: "paragraphSpacingSpin"
                        from: 0; to: 40
                        value: Typography.paragraphSpacing
                        onValueModified: Typography.paragraphSpacing = value
                    }
                    Label {
                        text: qsTr("px between blocks")
                        font.pixelSize: 11
                        color: Theme.textFaint
                    }
                }

                Label {
                    text: qsTr("Content width")
                    color: Theme.textSecondary
                }
                RowLayout {
                    CheckBox {
                        id: maxWidthCheck
                        objectName: "maxWidthCheck"
                        text: qsTr("Limit to")
                        checked: Typography.maxContentWidth > 0
                        onToggled: Typography.maxContentWidth =
                            checked ? maxWidthSpin.value : 0
                    }
                    SpinBox {
                        id: maxWidthSpin
                        objectName: "maxWidthSpin"
                        from: 300; to: 2000; stepSize: 50
                        enabled: maxWidthCheck.checked
                        value: Typography.maxContentWidth > 0
                            ? Typography.maxContentWidth : 700
                        onValueModified:
                            Typography.maxContentWidth = value
                    }
                    Label {
                        text: qsTr("px, centered")
                        font.pixelSize: 11
                        color: Theme.textFaint
                    }
                }

                Label { text: qsTr("Code font"); color: Theme.textSecondary }
                ComboBox {
                    objectName: "monoFamilyCombo"
                    Layout.fillWidth: true
                    model: Typography.monospaceFamilies()
                    currentIndex: {
                        var idx = model.indexOf(Typography.monoFamily)
                        return idx < 0 ? 0 : idx
                    }
                    onActivated: function(index) {
                        Typography.monoFamily = model[index]
                    }
                }

                Item { Layout.columnSpan: 2; Layout.fillHeight: true }

                Button {
                    objectName: "typographyResetButton"
                    Layout.columnSpan: 2
                    text: qsTr("Reset Typography")
                    onClicked: Typography.resetToDefaults()
                }
            }

            // ---- General (remote content and the opt-out update check) ----
            ColumnLayout {
                spacing: 14

                Label {
                    text: qsTr("Remote content")
                    font.bold: true
                    color: Theme.textSecondary
                }
                CheckBox {
                    objectName: "autoLoadRemoteToggle"
                    text: qsTr("Load remote images and previews automatically")
                    checked: EgressPolicy.autoLoadRemoteContent
                    onToggled: EgressPolicy.autoLoadRemoteContent = checked
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: Theme.textFaint
                    text: qsTr("Off by default. A note can name any address, "
                        + "so loading one on sight would tell that site you "
                        + "opened the note, and from where. With this off, "
                        + "each preview, image and media file offers a Load "
                        + "button and the site you approve is remembered.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    // allowedOrigins() is a plain function call, so both
                    // bindings read EgressPolicy.revision to re-evaluate when
                    // an approval is granted or forgotten.
                    readonly property int approvedCount: {
                        var r = EgressPolicy.revision
                        return EgressPolicy.allowedOrigins().length
                    }
                    Label {
                        Layout.fillWidth: true
                        font.pixelSize: 11
                        color: Theme.textFaint
                        text: parent.approvedCount === 0
                            ? qsTr("No sites approved.")
                            : qsTr("%n site(s) approved.", "", parent.approvedCount)
                    }
                    Button {
                        objectName: "forgetOriginsButton"
                        text: qsTr("Forget approved sites")
                        enabled: parent.approvedCount > 0
                        onClicked: EgressPolicy.forgetAllOrigins()
                    }
                }

                Label {
                    text: qsTr("Updates")
                    font.bold: true
                    color: Theme.textSecondary
                }
                CheckBox {
                    objectName: "updateCheckToggle"
                    text: qsTr("Check for new releases once a day")
                    checked: UpdateChecker.enabled
                    onToggled: UpdateChecker.enabled = checked
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: Theme.textFaint
                    text: qsTr("One request to the GitHub Releases API at "
                        + "startup, at most once per day, to show a notice "
                        + "when a newer version exists. Nothing is sent "
                        + "beyond the request itself, and nothing downloads "
                        + "automatically.")
                }

                Label {
                    visible: SystemTray.available
                    text: qsTr("System tray")
                    font.bold: true
                    color: Theme.textSecondary
                }
                CheckBox {
                    objectName: "closeToTrayToggle"
                    visible: SystemTray.available
                    text: qsTr("Keep running in the tray when the window is closed")
                    checked: SystemTray.closeToTray
                    onToggled: SystemTray.closeToTray = checked
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
