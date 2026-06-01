import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtMultimedia
import "../../style"

Rectangle {
    id: root

    property string path: ""
    property string sourceUrl: ""
    property bool compact: false
    property bool mediaLoaded: false

    radius: Theme.radiusMd
    color: Theme.withAlpha(Theme.panelSurface, themeController.isDark ? 0.78 : 0.92)
    border.color: Theme.withAlpha(Theme.panelBorder, themeController.isDark ? 0.72 : 0.86)
    border.width: 1
    clip: true

    function timeText(ms) {
        if (!Number.isFinite(ms) || ms <= 0) return "0:00"
        const totalSeconds = Math.floor(ms / 1000)
        const minutes = Math.floor(totalSeconds / 60)
        const seconds = totalSeconds % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function ensureMediaLoaded() {
        if (mediaLoaded) return
        player.source = sourceUrl
        mediaLoaded = true
    }

    function releaseMedia() {
        player.stop()
        player.source = ""
        mediaLoaded = false
    }

    function resetMedia() {
        releaseMedia()
        progressRail.value = 0
    }

    onPathChanged: resetMedia()
    onSourceUrlChanged: resetMedia()
    Component.onDestruction: releaseMedia()

    AudioOutput {
        id: audioOutput
        volume: volumeRail.value
        muted: muteButton.checked || volumeRail.value <= 0
    }

    MediaPlayer {
        id: player
        audioOutput: audioOutput

        onErrorOccurred: (error, errorString) => {
            console.warn("AudioPlaybackControls error:", error, errorString, "source:", root.sourceUrl)
        }
    }

    Item {
        id: content

        anchors.fill: parent
        anchors.leftMargin: root.compact ? 12 : 16
        anchors.rightMargin: root.compact ? 12 : 16
        anchors.topMargin: root.compact ? 9 : 10
        anchors.bottomMargin: root.compact ? 9 : 10

        readonly property real gap: root.compact ? 8 : 10
        readonly property real playSize: root.compact ? 32 : 36
        readonly property real muteSize: root.compact ? 30 : 32
        readonly property real rowHeight: root.compact ? 32 : 36
        readonly property real progressHeight: root.compact ? 24 : 26
        readonly property real volumeWidth: Math.max(58, Math.min(root.compact ? 78 : 98, width * 0.26))
        readonly property real timeWidth: {
            const left = playSize + gap
            const right = width - volumeWidth - gap - muteSize - gap
            return Math.max(84, Math.min(root.compact ? 108 : 122, right - left - gap))
        }

        AudioIconButton {
            id: playButton
            x: 0
            y: Math.round((content.rowHeight - height) / 2)
            width: content.playSize
            height: content.playSize
            enabled: root.sourceUrl.length > 0
            iconSource: player.playbackState === MediaPlayer.PlayingState
                        ? "qrc:/qt/qml/FM/qml/assets/lucide-toolbar/pause.svg"
                        : "qrc:/qt/qml/FM/qml/assets/lucide-toolbar/play.svg"
            primary: true
            tooltip: player.playbackState === MediaPlayer.PlayingState ? "Pause" : "Play"
            onClicked: {
                if (player.playbackState === MediaPlayer.PlayingState) {
                    player.pause()
                } else {
                    root.ensureMediaLoaded()
                    Qt.callLater(() => player.play())
                }
            }
        }

        Rectangle {
            id: timePill
            x: playButton.x + playButton.width + content.gap
            y: Math.round((content.rowHeight - height) / 2)
            width: content.timeWidth
            height: root.compact ? 28 : 30
            radius: Theme.radiusSm
            color: Theme.withAlpha(Theme.bg, themeController.isDark ? 0.28 : 0.50)
            border.color: Theme.withAlpha(Theme.panelBorder, themeController.isDark ? 0.58 : 0.72)
            border.width: 1

            Row {
                id: timeRow
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                spacing: 5

                Label {
                    width: Math.max(30, (timeRow.width - separator.width - timeRow.spacing * 2) / 2)
                    height: parent.height
                    text: root.timeText(progressRail.dragging ? progressRail.value : player.position)
                    font.family: "Consolas"
                    font.pixelSize: 11
                    font.bold: true
                    color: Theme.textPrimary
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }

                Label {
                    id: separator
                    height: parent.height
                    text: "/"
                    font.pixelSize: 11
                    font.bold: true
                    color: Theme.textSecondary
                    opacity: 0.78
                    verticalAlignment: Text.AlignVCenter
                }

                Label {
                    width: Math.max(30, (timeRow.width - separator.width - timeRow.spacing * 2) / 2)
                    height: parent.height
                    text: root.timeText(player.duration)
                    font.family: "Consolas"
                    font.pixelSize: 11
                    font.bold: true
                    color: Theme.textPrimary
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        AudioRail {
            id: volumeRail
            x: content.width - width
            y: Math.round((content.rowHeight - height) / 2)
            width: content.volumeWidth
            height: 28
            from: 0
            to: 1
            value: 0.15
            liveWhileDragging: true
            accentColor: Theme.accent
            handleSize: 14
            trackHeight: 5
        }

        AudioIconButton {
            id: muteButton
            x: volumeRail.x - content.gap - width
            y: Math.round((content.rowHeight - height) / 2)
            width: content.muteSize
            height: content.muteSize
            checkable: true
            iconColor: "#38bdf8"
            iconSource: checked || volumeRail.value <= 0
                        ? "qrc:/qt/qml/FM/qml/assets/lucide-toolbar/volume-x.svg"
                        : "qrc:/qt/qml/FM/qml/assets/lucide-toolbar/volume-2.svg"
            tooltip: checked ? "Unmute" : "Mute"
        }

        AudioRail {
            id: progressRail
            x: playButton.x
            y: content.height - height
            width: Math.max(1, content.width * 0.95)
            height: content.progressHeight
            from: 0
            to: Math.max(1, player.duration)
            value: 0
            enabled: player.duration > 0
            liveWhileDragging: false
            accentColor: Theme.accent
            handleSize: 18
            trackHeight: 7
            onCommitted: (newValue) => player.setPosition(Math.round(newValue))
        }
    }

    Connections {
        target: player
        function onPositionChanged() {
            if (!progressRail.dragging) {
                progressRail.value = player.position
            }
        }
        function onDurationChanged() {
            if (!progressRail.dragging) {
                progressRail.value = player.position
            }
        }
    }

    component AudioIconButton: ToolButton {
        id: button

        property string iconSource: ""
        property string tooltip: ""
        property bool primary: false
        property color iconColor: Theme.textPrimary

        padding: 0
        hoverEnabled: true

        background: Rectangle {
            radius: width / 2
            color: {
                if (!button.enabled) return Theme.withAlpha(Theme.textSecondary, 0.08)
                if (button.primary) {
                    return button.down
                        ? Theme.withAlpha(Theme.accent, 0.88)
                        : (button.hovered ? Theme.withAlpha(Theme.accent, 0.78) : Theme.accent)
                }
                return button.down
                    ? Theme.withAlpha(button.iconColor, themeController.isDark ? 0.20 : 0.15)
                    : (button.hovered ? Theme.withAlpha(button.iconColor, themeController.isDark ? 0.14 : 0.10) : "transparent")
            }
            border.color: button.primary
                          ? Theme.withAlpha(Theme.accent, 0.35)
                          : Theme.withAlpha(button.iconColor, button.hovered ? 0.42 : 0.20)
            border.width: button.primary || button.hovered ? 1 : 0
        }

        contentItem: Item {
            Image {
                id: iconMask
                anchors.centerIn: parent
                width: button.primary ? 18 : 17
                height: width
                source: button.iconSource
                sourceSize: Qt.size(36, 36)
                fillMode: Image.PreserveAspectFit
                smooth: true
                visible: false
            }

            MultiEffect {
                anchors.fill: iconMask
                source: iconMask
                colorization: 1
                colorizationColor: button.primary ? Theme.bg : button.iconColor
                opacity: button.enabled ? 1 : 0.42
            }
        }

        ToolTip.visible: hovered && tooltip.length > 0
        ToolTip.text: tooltip
    }

    component AudioRail: Item {
        id: rail

        property real from: 0
        property real to: 1
        property real value: 0
        property bool liveWhileDragging: false
        property real railScale: 1.0
        property color accentColor: Theme.accent
        property int handleSize: 16
        property int trackHeight: 6
        readonly property bool dragging: inputArea.pressed
        readonly property real range: Math.max(0.0001, to - from)
        readonly property real progress: Math.max(0, Math.min(1, (value - from) / range))
        readonly property real usableWidth: Math.max(1, width - handleSize)
        readonly property real scaledWidth: Math.max(1, usableWidth * Math.max(0.1, Math.min(1, railScale)))
        readonly property real trackX: handleSize / 2 + (usableWidth - scaledWidth) / 2

        signal edited(real newValue)
        signal committed(real newValue)

        function valueAtX(x) {
            const ratio = Math.max(0, Math.min(1, (x - trackX) / scaledWidth))
            return from + ratio * range
        }

        function setValueFromX(x, commit) {
            if (!enabled) return
            value = valueAtX(x)
            if (liveWhileDragging) {
                edited(value)
            }
            if (commit) {
                committed(value)
            }
        }

        opacity: enabled ? 1 : 0.55

        Rectangle {
            id: baseTrack
            x: rail.trackX
            y: Math.round((rail.height - height) / 2)
            width: rail.scaledWidth
            height: rail.trackHeight
            radius: height / 2
            color: Theme.withAlpha(Theme.panelBorder, themeController.isDark ? 0.60 : 0.54)
        }

        Rectangle {
            x: baseTrack.x
            y: baseTrack.y
            width: baseTrack.width * rail.progress
            height: baseTrack.height
            radius: height / 2
            color: rail.accentColor
        }

        Rectangle {
            x: baseTrack.x + baseTrack.width * rail.progress - width / 2
            y: Math.round((rail.height - height) / 2)
            width: rail.dragging ? rail.handleSize + 4 : rail.handleSize
            height: width
            radius: width / 2
            color: Theme.panelSurface
            border.color: rail.accentColor
            border.width: 1.5

            Behavior on width { NumberAnimation { duration: Theme.motionFast } }
        }

        MouseArea {
            id: inputArea
            anchors.fill: parent
            enabled: rail.enabled
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.PointingHandCursor

            onPressed: (mouse) => {
                rail.setValueFromX(mouse.x, false)
                mouse.accepted = true
            }
            onPositionChanged: (mouse) => {
                if (pressed) {
                    rail.setValueFromX(mouse.x, false)
                }
            }
            onReleased: (mouse) => rail.setValueFromX(mouse.x, true)
            onCanceled: rail.committed(rail.value)
        }
    }
}
