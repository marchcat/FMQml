import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../style"

Item {
    id: root

    property string path: ""
    property string name: ""
    property string sizeText: ""
    property string modifiedText: ""
    property string mimeName: ""
    property string extension: ""
    property string audioTitle: ""
    property string audioArtist: ""
    property string audioAlbum: ""
    property string audioYear: ""
    property string audioTrack: ""
    property string audioGenre: ""
    property string audioComment: ""
    property string audioDuration: ""
    property string audioBitrate: ""
    property string audioSampleRate: ""
    property string audioChannels: ""
    property string mediaSourceUrl: ""
    property bool compact: false
    property bool showDetails: false
    property bool multimediaControlsAvailable: false
    property bool playbackControlsActive: true

    readonly property string titleText: audioTitle.length > 0 ? audioTitle : (name.length > 0 ? name : "Audio File")
    readonly property string subtitleText: audioArtist.length > 0 && audioAlbum.length > 0
                                           ? audioArtist + " - " + audioAlbum
                                           : (audioArtist.length > 0
                                              ? audioArtist
                                              : (audioAlbum.length > 0
                                                 ? audioAlbum
                                                 : (mimeName.length > 0 ? mimeName : "Audio")))
    readonly property string formatText: extension.length > 0 ? extension.toUpperCase() : "AUDIO"
    readonly property string metaText: audioDuration.length > 0 && audioBitrate.length > 0
                                       ? audioDuration + "  |  " + audioBitrate
                                       : (audioDuration.length > 0
                                          ? audioDuration
                                          : (audioBitrate.length > 0
                                             ? audioBitrate
                                             : (sizeText.length > 0 ? sizeText : formatText)))

    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.compact ? 10 : 18
        spacing: root.compact ? 10 : 14

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compact ? 150 : 230
            Layout.maximumHeight: parent.height
            radius: Theme.radiusMd
            color: Theme.withAlpha(Theme.secondaryAccent, themeController.isDark ? 0.12 : 0.09)
            border.color: Theme.withAlpha(Theme.secondaryAccent, themeController.isDark ? 0.34 : 0.26)
            border.width: 1
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.margins: root.compact ? 12 : 16
                spacing: root.compact ? 10 : 16

                Rectangle {
                    Layout.preferredWidth: root.compact ? 78 : 132
                    Layout.preferredHeight: width
                    radius: Theme.radiusSm
                    color: Theme.withAlpha(Theme.accent, themeController.isDark ? 0.16 : 0.12)
                    border.color: Theme.withAlpha(Theme.accent, themeController.isDark ? 0.42 : 0.30)
                    border.width: 1

                    Image {
                        id: coverArt
                        anchors.fill: parent
                        source: root.path.length > 0 ? "image://thumbnail/" + encodeURIComponent(root.path + "::cover") : ""
                        sourceSize: Qt.size(root.compact ? 256 : 512, root.compact ? 256 : 512)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: false
                        smooth: true
                        visible: status === Image.Ready
                    }

                    Image {
                        anchors.centerIn: parent
                        visible: coverArt.status !== Image.Ready
                        source: "qrc:/qt/qml/FM/qml/assets/icons/music.svg"
                        sourceSize: Qt.size(root.compact ? 38 : 58, root.compact ? 38 : 58)
                        opacity: 0.88
                        smooth: true
                    }

                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        width: formatLabel.implicitWidth + 12
                        height: 20
                        radius: Theme.radiusSm
                        color: Theme.withAlpha(Theme.bg, themeController.isDark ? 0.72 : 0.82)
                        visible: coverArt.status !== Image.Ready

                        Label {
                            id: formatLabel
                            anchors.centerIn: parent
                            text: root.formatText
                            font.pixelSize: 9
                            font.bold: true
                            color: Theme.textSecondary
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: root.compact ? 4 : 6

                    Label {
                        Layout.fillWidth: true
                        text: root.titleText
                        font.pixelSize: root.compact ? 16 : 24
                        font.bold: true
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.subtitleText
                        font.pixelSize: root.compact ? 12 : 15
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.metaText
                        font.pixelSize: root.compact ? 10 : 12
                        color: Theme.textSecondary
                        opacity: 0.84
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.formatText
                        font.pixelSize: 10
                        font.bold: true
                        color: Theme.warmAccent
                        elide: Text.ElideRight
                    }

                    Loader {
                        id: playbackControlsLoader
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        visible: root.showDetails && root.multimediaControlsAvailable && root.playbackControlsActive
                        active: root.showDetails && root.multimediaControlsAvailable && root.playbackControlsActive
                        source: "AudioPlaybackControls.qml"
                    }

                    Binding {
                        target: playbackControlsLoader.item
                        property: "path"
                        value: root.path
                        when: playbackControlsLoader.status === Loader.Ready
                    }

                    Binding {
                        target: playbackControlsLoader.item
                        property: "sourceUrl"
                        value: root.mediaSourceUrl
                        when: playbackControlsLoader.status === Loader.Ready
                    }

                    Binding {
                        target: playbackControlsLoader.item
                        property: "compact"
                        value: root.compact
                        when: playbackControlsLoader.status === Loader.Ready
                    }
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.showDetails
            background: null
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: parent.width
                spacing: 8

                DetailRow { label: "Title"; value: root.audioTitle }
                DetailRow { label: "Artist"; value: root.audioArtist }
                DetailRow { label: "Album"; value: root.audioAlbum }
                DetailRow { label: "Year"; value: root.audioYear }
                DetailRow { label: "Track"; value: root.audioTrack }
                DetailRow { label: "Genre"; value: root.audioGenre }
                DetailRow { label: "Comment"; value: root.audioComment }
                DetailRow { label: "Duration"; value: root.audioDuration }
                DetailRow { label: "Bitrate"; value: root.audioBitrate }
                DetailRow { label: "Sample Rate"; value: root.audioSampleRate }
                DetailRow { label: "Channels"; value: root.audioChannels }
                DetailRow { label: "Size"; value: root.sizeText }
                DetailRow { label: "Modified"; value: root.modifiedText }
            }
        }
    }

    component DetailRow: Rectangle {
        property string label: ""
        property string value: ""

        Layout.fillWidth: true
        visible: value.length > 0
        radius: 8
        color: themeController.isDark ? Qt.rgba(1, 1, 1, 0.035) : Qt.rgba(0, 0, 0, 0.025)
        border.color: Theme.border
        border.width: 1
        implicitHeight: valueColumn.implicitHeight + 22

        ColumnLayout {
            id: valueColumn
            anchors.fill: parent
            anchors.margins: 11
            spacing: 4

            Label {
                Layout.fillWidth: true
                text: parent.parent.label
                font.pixelSize: 10
                font.bold: true
                color: Theme.textSecondary
                opacity: 0.88
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: parent.parent.value
                color: Theme.textPrimary
                font.pixelSize: 12
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
        }
    }
}
