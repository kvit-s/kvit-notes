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
KvitDialog {
    id: settingsDialog
    objectName: "settingsDialog"

    modal: true
    title: qsTr("Settings")
    standardButtons: Dialog.Close
    width: Interface.px(560)
    // Tall enough to show the Appearance page without scrolling at the
    // default interface size, and never taller than the window it sits in.
    // Past that the tab pages scroll, which is what a large interface size
    // needs anyway.
    height: Math.min(Interface.px(560),
                     settingsDialog.parent
                         ? settingsDialog.parent.height - Interface.px(40)
                         : Interface.px(560))
    padding: 0

    // The position is plain x/y rather than anchors.centerIn, because the
    // title bar below drags the dialog by assigning to them, and an anchor
    // would win over that assignment. onAboutToShow centres the first
    // opening and afterwards only keeps the dropped position on screen.
    property bool everPlaced: false
    onAboutToShow: {
        // macOS and Linux have no push notification for the two accessibility
        // preferences, so this is where a change made since startup is picked
        // up — the one place the answer is about to be read out loud.
        SystemAppearance.refresh()
        if (!parent)
            return
        var limitX = Math.max(0, parent.width - width)
        var limitY = Math.max(0, parent.height - height)
        if (!everPlaced) {
            x = Math.round(limitX / 2)
            y = Math.round(limitY / 2)
            everPlaced = true
        } else {
            x = Math.max(0, Math.min(x, limitX))
            y = Math.max(0, Math.min(y, limitY))
        }
    }

    background: Rectangle {
        color: Theme.popupBackground
        border.color: Theme.borderStrong
        border.width: 1
        radius: Interface.px(6)
    }

    // The title doubles as the drag handle: a Popup does not move on its own,
    // and a settings dialog is one a reader wants pushed aside to see the
    // document it is previewing changes on.
    header: Label {
        objectName: "settingsTitleBar"
        text: settingsDialog.title
        font.pixelSize: Interface.px(16)
        font.bold: true
        color: Theme.textPrimary
        padding: Interface.px(12)

        MouseArea {
            id: titleDrag
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            // Where in the title bar the press landed, so the dialog keeps
            // that point under the cursor for the whole drag.
            property real grabX: 0
            property real grabY: 0

            onPressed: function(mouse) {
                titleDrag.grabX = mouse.x
                titleDrag.grabY = mouse.y
            }
            onPositionChanged: function(mouse) {
                if (!titleDrag.pressed || !settingsDialog.parent)
                    return
                var limitX = Math.max(
                    0, settingsDialog.parent.width - settingsDialog.width)
                var limitY = Math.max(
                    0, settingsDialog.parent.height - settingsDialog.height)
                settingsDialog.x = Math.max(0, Math.min(
                    settingsDialog.x + mouse.x - titleDrag.grabX, limitX))
                settingsDialog.y = Math.max(0, Math.min(
                    settingsDialog.y + mouse.y - titleDrag.grabY, limitY))
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        TabBar {
            id: pageBar
            objectName: "settingsTabBar"
            Layout.fillWidth: true
            TabButton { text: qsTr("Appearance"); objectName: "appearanceTab" }
            TabButton { text: qsTr("Typography"); objectName: "typographyTab" }
            TabButton { text: qsTr("General"); objectName: "generalTab" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Interface.px(16)
            currentIndex: pageBar.currentIndex

            // ---- Appearance (§10.1, §10.3) -------------------------
            ScrollView {
                id: appearancePage
                // In a ScrollView, because a tab page is taller than the dialog
                // as soon as the interface size grows — and the Appearance page
                // already is at the default. Content that runs past the frame is
                // simply unreachable otherwise (accessibility.md Finding 4).
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                // Always shown when there is more below, rather than only
                // while the pointer is over the page: a settings section a
                // reader cannot see and has no reason to scroll for might as
                // well not be there.
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    // Bound to the scroll view rather than left to its own
                    // implicit width: a row with a fillWidth child would
                    // otherwise demand more than the view shows and push its
                    // last controls out past the right edge.
                    width: appearancePage.availableWidth
                    spacing: Interface.px(14)

                    Label {
                        text: qsTr("Theme")
                        font.bold: true
                        color: Theme.textSecondary
                    }

                    // A Flow, not a RowLayout: a row would demand the width of
                    // every theme card at once, and a dialog narrower than that
                    // sum has its tab bar pushed out past its own frame. Cards
                    // wrap onto a second line instead.
                    Flow {
                        Layout.fillWidth: true
                        spacing: Interface.px(10)
                        Repeater {
                            model: Theme.availableThemes
                            // A theme card: swatch above the name, the
                            // active one ringed in accent.
                            Column {
                                // Named so the children below reach it directly.
                                // Through `parent` they cannot be typed: qmllint
                                // sees only QQuickItem and cannot know `preview`
                                // or `modelData` are on it.
                                id: card
                                required property string modelData
                                readonly property var preview:
                                    Theme.themePreview(modelData)
                                spacing: Interface.px(4)

                                Rectangle {
                                    objectName: "themeCard_" + card.modelData
                                    width: Interface.px(96)
                                    height: Interface.px(60)
                                    radius: Interface.px(5)
                                    color: card.preview.background
                                    border.width:
                                        Theme.themeId === card.modelData ? 2 : 1
                                    border.color:
                                        Theme.themeId === card.modelData
                                            ? Theme.accent : Theme.borderStrong

                                    Rectangle { // panel stripe
                                        width: Interface.px(26)
                                        height: parent.height - 12
                                        x: 6; y: 6
                                        radius: Interface.px(3)
                                        color: card.preview.panel
                                    }
                                    Label {
                                        text: "Aa"
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: Interface.px(8)
                                        font.pixelSize: Interface.px(16)
                                        color: card.preview.text
                                    }
                                    Rectangle { // accent dot
                                        width: 10; height: 10; radius: 5
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: Interface.px(8)
                                        color: card.preview.accent
                                    }
                                    Accessible.role: Accessible.RadioButton
                                    Accessible.name:
                                        Theme.displayName(card.modelData)
                                    Accessible.checkable: true
                                    Accessible.checked:
                                        Theme.themeId === card.modelData
                                    Accessible.onPressAction:
                                        Theme.themeId = card.modelData
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: Theme.themeId
                                            = card.modelData
                                    }
                                }
                                Label {
                                    width: Interface.px(96)
                                    horizontalAlignment: Text.AlignHCenter
                                    // Theme.displayName, not a capitalised id:
                                    // that spelling reads "highContrast" out
                                    // as "HighContrast", and the accessible
                                    // name beside it already says
                                    // "High contrast".
                                    text: Theme.displayName(card.modelData)
                                    font.pixelSize: Interface.small
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
                        Layout.topMargin: Interface.px(6)
                    }
                    RowLayout {
                        spacing: Interface.px(6)

                        Rectangle { // "theme default" swatch
                            objectName: "accentDefaultSwatch"
                            width: 24; height: 24; radius: 12
                            color: Theme.mutedGlyph
                            border.width: Theme.accentOverride === "" ? 2 : 1
                            border.color: Theme.accentOverride === ""
                                ? Theme.textPrimary : Theme.borderStrong
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: qsTr("Theme default accent")
                            Accessible.checkable: true
                            Accessible.checked: Theme.accentOverride === ""
                            Accessible.onPressAction: Theme.accentOverride = ""
                            Label {
                                anchors.centerIn: parent
                                text: "✕"
                                font.pixelSize: Interface.caption
                                color: Theme.labelOn(Theme.mutedGlyph)
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
                                id: accentSwatch
                                required property string modelData
                                width: 24; height: 24; radius: 12
                                color: modelData
                                border.width:
                                    Theme.accentOverride === modelData ? 2 : 1
                                border.color:
                                    Theme.accentOverride === modelData
                                        ? Theme.textPrimary : Theme.borderStrong
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: Theme.colorName(accentSwatch.modelData)
                                Accessible.checkable: true
                                Accessible.checked:
                                    Theme.accentOverride === accentSwatch.modelData
                                Accessible.onPressAction:
                                    Theme.accentOverride = accentSwatch.modelData
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: Theme.accentOverride
                                        = parent.modelData
                                }
                            }
                        }
                        TextField {
                            objectName: "accentHexField"
                            Layout.preferredWidth: Interface.px(84)
                            implicitHeight: Interface.px(26)
                            font.pixelSize: Interface.small
                            placeholderText: "#rrggbb"
                            text: Theme.accentOverride
                            Accessible.name: qsTr("Accent colour, as a hex value")
                            onEditingFinished: Theme.accentOverride = text
                        }
                    }

                    // The accent is a fill — a selected row, a filled button, the
                    // to-do tick's box — so WCAG asks 3:1 of it against the page
                    // behind it. The label drawn ON it looks after itself
                    // (Theme.labelOn), but nothing else stops a pale custom
                    // accent from disappearing into the page, and a warning is
                    // the right answer rather than a refusal: it is the user's
                    // own preference (accessibility.md Finding 3).
                    Label {
                        objectName: "accentContrastWarning"
                        visible: Theme.accentOverride !== ""
                                 && Theme.contrastRatio(Theme.accent,
                                                        Theme.windowBackground) < 3.0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: Interface.small
                        color: Theme.warning
                        text: qsTr("This accent is faint against the page "
                                   + "(%1:1, below the 3:1 the guidelines ask for). "
                                   + "Selected rows and filled buttons will be hard "
                                   + "to make out.")
                              .arg(Theme.contrastRatio(Theme.accent,
                                                       Theme.windowBackground).toFixed(1))
                    }

                    // Highlight color (§10.3): the ==mark== background.
                    Label {
                        text: qsTr("Highlight color")
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.topMargin: Interface.px(6)
                    }
                    RowLayout {
                        spacing: Interface.px(6)

                        Rectangle {
                            objectName: "highlightDefaultSwatch"
                            width: 24; height: 24; radius: 12
                            color: Theme.mutedGlyph
                            border.width: Theme.highlightOverride === "" ? 2 : 1
                            border.color: Theme.highlightOverride === ""
                                ? Theme.textPrimary : Theme.borderStrong
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: qsTr("Theme default highlight")
                            Accessible.checkable: true
                            Accessible.checked: Theme.highlightOverride === ""
                            Accessible.onPressAction: Theme.highlightOverride = ""
                            Label {
                                anchors.centerIn: parent
                                text: "✕"
                                font.pixelSize: Interface.caption
                                color: Theme.labelOn(Theme.mutedGlyph)
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: Theme.highlightOverride = ""
                            }
                        }
                        Repeater {
                            model: Theme.highlightPalette
                            Rectangle {
                                id: highlightSwatch
                                required property string modelData
                                width: 24; height: 24; radius: 12
                                color: modelData
                                border.width:
                                    Theme.highlightOverride === modelData ? 2 : 1
                                border.color:
                                    Theme.highlightOverride === modelData
                                        ? Theme.textPrimary : Theme.borderStrong
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: Theme.colorName(highlightSwatch.modelData)
                                Accessible.checkable: true
                                Accessible.checked:
                                    Theme.highlightOverride === highlightSwatch.modelData
                                Accessible.onPressAction:
                                    Theme.highlightOverride = highlightSwatch.modelData
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: Theme.highlightOverride
                                        = parent.modelData
                                }
                            }
                        }
                        TextField {
                            objectName: "highlightHexField"
                            Layout.preferredWidth: Interface.px(84)
                            implicitHeight: Interface.px(26)
                            font.pixelSize: Interface.small
                            placeholderText: "#rrggbb"
                            text: Theme.highlightOverride
                            onEditingFinished: Theme.highlightOverride = text
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Sample: normal, ==highlighted==, and "
                                   + "[linked](x) text follow these choices.")
                        font.pixelSize: Interface.small
                        color: Theme.textFaint
                        wrapMode: Text.Wrap
                    }

                    // Interface size (accessibility.md Finding 4): the chrome's
                    // own type scale, separate from the editor font on the
                    // Typography tab. Two settings rather than one because the
                    // needs differ — a small, dense note list beside large body
                    // text is a coherent thing to want, and so is the reverse —
                    // and because Typography's frozen ratios are what keep the
                    // document rendering pixel-identically at the default.
                    Label {
                        text: qsTr("Interface size")
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.topMargin: Interface.px(6)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Interface.px(8)
                        Slider {
                            id: interfaceSizeSlider
                            objectName: "interfaceSizeSlider"
                            Layout.fillWidth: true
                            from: Interface.minFontSize
                            to: Interface.maxFontSize
                            stepSize: 1
                            snapMode: Slider.SnapAlways
                            value: Interface.fontSize
                            Accessible.name: qsTr("Interface size in pixels")
                            onMoved: Interface.fontSize = Math.round(value)
                        }
                        Label {
                            objectName: "interfaceSizeValue"
                            text: Interface.fontSize + " px"
                            font.pixelSize: Interface.small
                            color: Theme.textPrimary
                            Layout.minimumWidth: Interface.px(44)
                        }
                        Button {
                            objectName: "interfaceSizeReset"
                            text: qsTr("Reset")
                            font.pixelSize: Interface.small
                            onClicked: Interface.resetToDefaults()
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: Interface.small
                        color: Theme.textFaint
                        text: qsTr("This sizes the sidebar, note list, toolbar, "
                                   + "status bar and dialogs. The text of a note "
                                   + "follows the editor font on the Typography "
                                   + "tab instead.")
                    }

                    // Reduced motion (§14.3), as three states rather than a
                    // checkbox: on, off, or whatever the desktop asks for. Every
                    // desktop has a setting for this and a person who has turned
                    // it on there expects applications to follow, so following it
                    // is the default; an explicit choice here overrides it
                    // (accessibility.md Finding 7).
                    Label {
                        text: qsTr("Motion")
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.topMargin: Interface.px(6)
                    }
                    RowLayout {
                        spacing: Interface.px(6)
                        Repeater {
                            model: Theme.availableReducedMotionSettings()
                            RadioButton {
                                id: motionChoice
                                required property string modelData
                                objectName: "reducedMotion_" + modelData
                                text: {
                                    if (motionChoice.modelData === "on")
                                        return qsTr("Reduce motion")
                                    if (motionChoice.modelData === "off")
                                        return qsTr("Full motion")
                                    return qsTr("Follow the system")
                                }
                                font.pixelSize: Interface.body
                                checked: Theme.reducedMotionSetting
                                         === motionChoice.modelData
                                onClicked: Theme.reducedMotionSetting
                                           = motionChoice.modelData
                            }
                        }
                    }
                    Label {
                        objectName: "reducedMotionExplanation"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: Interface.small
                        color: Theme.textFaint
                        text: {
                            if (Theme.reducedMotionSetting !== "system")
                                return qsTr("Positional animation — blocks sliding "
                                            + "as a note is reordered — is what "
                                            + "this setting stills.")
                            if (!SystemAppearance.available)
                                return qsTr("This desktop does not say, so motion "
                                            + "stays on.")
                            return SystemAppearance.reducedMotion
                                ? qsTr("The desktop asks for reduced motion, so it "
                                       + "is on.")
                                : qsTr("The desktop does not ask for reduced "
                                       + "motion, so it is off.")
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // ---- Typography (§10.2) --------------------------------
            ScrollView {
                id: typographyPage
                // In a ScrollView, because a tab page is taller than the dialog
                // as soon as the interface size grows — and the Appearance page
                // already is at the default. Content that runs past the frame is
                // simply unreachable otherwise (accessibility.md Finding 4).
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                // Always shown when there is more below, rather than only
                // while the pointer is over the page: a settings section a
                // reader cannot see and has no reason to scroll for might as
                // well not be there.
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                GridLayout {
                    // Bound to the scroll view rather than left to its own
                    // implicit width: a row with a fillWidth child would
                    // otherwise demand more than the view shows and push its
                    // last controls out past the right edge.
                    width: typographyPage.availableWidth
                    columns: 2
                    columnSpacing: Interface.px(12)
                    rowSpacing: Interface.px(10)

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
                            font.pixelSize: Interface.small
                            color: Theme.textFaint
                        }
                    }

                    Label { text: qsTr("Line height"); color: Theme.textSecondary }
                    RowLayout {
                        Slider {
                            id: lineHeightSlider
                            objectName: "lineHeightSlider"
                            Layout.preferredWidth: Interface.px(180)
                            from: 1.0; to: 2.0; stepSize: 0.05
                            value: Typography.lineHeight
                            onMoved: Typography.lineHeight = value
                        }
                        Label {
                            text: "×" + Typography.lineHeight.toFixed(2)
                            font.pixelSize: Interface.small
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
                            font.pixelSize: Interface.small
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
                            font.pixelSize: Interface.small
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
            }

            // ---- General (remote content and the opt-out update check) ----
            ScrollView {
                id: generalPage
                // In a ScrollView, because a tab page is taller than the dialog
                // as soon as the interface size grows — and the Appearance page
                // already is at the default. Content that runs past the frame is
                // simply unreachable otherwise (accessibility.md Finding 4).
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                // Always shown when there is more below, rather than only
                // while the pointer is over the page: a settings section a
                // reader cannot see and has no reason to scroll for might as
                // well not be there.
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    // Bound to the scroll view rather than left to its own
                    // implicit width: a row with a fillWidth child would
                    // otherwise demand more than the view shows and push its
                    // last controls out past the right edge.
                    width: generalPage.availableWidth
                    spacing: Interface.px(14)

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
                        font.pixelSize: Interface.small
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
                            font.pixelSize: Interface.small
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
                        font.pixelSize: Interface.small
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
}
