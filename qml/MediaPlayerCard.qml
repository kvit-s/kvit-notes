// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Kvit 1.0

// One local-media renderer for both a MediaBlock and a file opened directly
// from the tree. The source has already passed the caller's local/remote
// policy. Backend failures become a named card with a desktop escape hatch.
Rectangle {
    id: card

    property string source: ""
    property string displayName: source
    property string title: displayName
    property string forcedErrorMessage: ""
    property bool showDesktopAction: true
    // A host can replace the desktop fallback with a local action, used by a
    // document block while remote media is waiting for reader consent.
    property string fallbackActionText: ""
    property string desktopUrl: ""
    property bool audioHint: false
    property bool videoHint: false
    signal fallbackActionRequested()
    readonly property string extension: {
        var clean = displayName.split("?")[0]
        var dot = clean.lastIndexOf(".")
        return dot >= 0 ? clean.substring(dot + 1).toLowerCase() : ""
    }
    readonly property bool isAudio: audioHint
        || ["mp3", "wav", "ogg", "flac", "m4a"].indexOf(extension) !== -1
    readonly property bool isVideo: videoHint
        || ["mp4", "webm", "mkv", "mov"].indexOf(extension) !== -1
    readonly property bool hasError: forcedErrorMessage !== ""
        || source === "" || player.error !== MediaPlayer.NoError
    readonly property bool isPlaying:
        player.playbackState === MediaPlayer.PlayingState
    readonly property string errorMessage: forcedErrorMessage !== ""
        ? forcedErrorMessage
        : source === "" ? qsTr("File not found")
        : player.errorString !== "" ? qsTr("Cannot play this file: %1")
                                      .arg(player.errorString)
                                  : qsTr("Cannot play this file")
    implicitWidth: Interface.px(520)
    implicitHeight: contents.implicitHeight + Interface.px(18)
    radius: Interface.px(6)
    color: Theme.panelBackground
    border.color: Theme.border
    border.width: 1
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Media player for %1").arg(displayName)

    function fmtTime(ms) {
        if (ms <= 0)
            return "0:00"
        var total = Math.floor(ms / 1000)
        var minutes = Math.floor(total / 60)
        var seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" + seconds : seconds)
    }
    function togglePlay() {
        if (hasError)
            return
        if (player.playbackState === MediaPlayer.PlayingState)
            player.pause()
        else
            player.play()
    }
    function stop() { player.stop() }

    Keys.onSpacePressed: function(event) {
        togglePlay()
        event.accepted = true
    }

    MediaPlayer {
        id: player
        objectName: "mediaPlayer"
        source: card.source
        audioOutput: AudioOutput { id: audioOut; volume: 0.8 }
        videoOutput: card.isVideo ? videoFrame : null
    }

    ColumnLayout {
        id: contents
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Interface.px(9)
        spacing: Interface.px(7)

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Interface.px(4)
            visible: card.hasError
            Label {
                text: (card.isAudio ? "♪  " : "▷  ") + qsTr("Media unavailable")
                color: Theme.textPrimary
                font.bold: true
                font.pixelSize: Interface.strong
            }
            Label {
                Layout.fillWidth: true
                text: card.displayName
                color: Theme.textMuted
                font.pixelSize: Interface.small
                elide: Text.ElideMiddle
            }
            Label {
                Layout.fillWidth: true
                text: card.errorMessage
                color: Theme.danger
                font.pixelSize: Interface.small
                wrapMode: Text.Wrap
            }
            Button {
                objectName: "mediaFallbackAction"
                visible: card.fallbackActionText !== ""
                      || (card.showDesktopAction && card.displayName !== "")
                text: card.fallbackActionText !== ""
                    ? card.fallbackActionText : qsTr("Open with desktop")
                onClicked: {
                    if (card.fallbackActionText !== "") {
                        card.fallbackActionRequested()
                        return
                    }
                    UrlLauncher.open(card.desktopUrl !== ""
                        ? card.desktopUrl
                        : DocumentManager.toLocalFileUrl(
                              card.displayName).toString())
                }
            }
        }

        VideoOutput {
            id: videoFrame
            objectName: "mediaVideoOutput"
            visible: card.isVideo && !card.hasError
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? width * 9 / 16 : 0
            fillMode: VideoOutput.PreserveAspectFit
        }

        RowLayout {
            Layout.fillWidth: true
            visible: card.isAudio && !card.hasError
            spacing: Interface.px(8)
            Label {
                text: "♪"
                font.pixelSize: Interface.px(18)
                color: Theme.textMuted
            }
            Label {
                Layout.fillWidth: true
                text: card.title
                color: Theme.textPrimary
                font.pixelSize: Interface.body
                elide: Text.ElideMiddle
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: !card.hasError
            spacing: Interface.px(7)
            Button {
                objectName: "mediaPlayButton"
                text: card.isPlaying ? "Ⅱ" : "▶"
                Accessible.name: card.isPlaying ? qsTr("Pause") : qsTr("Play")
                onClicked: card.togglePlay()
            }
            Label {
                text: card.fmtTime(player.position)
                color: Theme.textMuted
                font.pixelSize: Interface.small
            }
            Slider {
                objectName: "mediaSeek"
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, player.duration)
                value: player.position
                onMoved: player.position = value
            }
            Label {
                text: card.fmtTime(player.duration)
                color: Theme.textMuted
                font.pixelSize: Interface.small
            }
            Label {
                text: audioOut.volume <= 0 ? "◀" : "◀))"
                color: Theme.textMuted
            }
            Slider {
                objectName: "mediaVolume"
                Layout.preferredWidth: Interface.px(72)
                from: 0
                to: 1
                value: audioOut.volume
                onMoved: audioOut.volume = value
            }
        }
    }
}
